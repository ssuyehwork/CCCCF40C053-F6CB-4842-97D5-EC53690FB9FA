#include "HttpServer.h"
#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
#include "DatabaseManager.h"
#include "../ui/StringUtils.h"

HttpServer& HttpServer::instance() {
    static HttpServer inst;
    return inst;
}

HttpServer::HttpServer(QObject *parent) : QTcpServer(parent) {}

bool HttpServer::start(quint16 port) {
    if (isListening()) return true;
    bool ok = listen(QHostAddress::LocalHost, port);
    if (ok) {
        qDebug() << "[HttpServer] 服务已启动，监听端口:" << port;
    } else {
        qWarning() << "[HttpServer] 服务启动失败:" << errorString();
    }
    return ok;
}

void HttpServer::incomingConnection(qintptr socketDescriptor) {
    QTcpSocket *socket = new QTcpSocket(this);
    if (!socket->setSocketDescriptor(socketDescriptor)) {
        delete socket;
        return;
    }

    // [FIX] 使用 QObject 包装 QByteArray 以利用 deleteLater 安全释放内存，防止 Use-After-Free 导致的闪退
    class BufferHolder : public QObject { public: QByteArray data; };
    BufferHolder* holder = new BufferHolder();
    holder->setParent(socket);

    connect(socket, &QTcpSocket::readyRead, [this, socket, holder]() {
        QByteArray& dataBuffer = holder->data;
        // [SECURITY] 限制总接收缓冲区大小，防止恶意连接耗尽内存导致闪退
        if (dataBuffer.size() > 12 * 1024 * 1024) {
            qWarning() << "[HttpServer] 缓冲区溢出，强制断开连接";
            socket->disconnectFromHost();
            return;
        }

        dataBuffer.append(socket->readAll());
        
        if (dataBuffer.contains("\r\n\r\n")) {
            // 处理 OPTIONS 预检请求 (CORS)
            if (dataBuffer.contains("OPTIONS")) {
                socket->write("HTTP/1.1 204 No Content\r\n"
                              "Access-Control-Allow-Origin: *\r\n"
                              "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                              "Access-Control-Allow-Headers: Content-Type, Authorization\r\n"
                              "Access-Control-Max-Age: 86400\r\n"
                              "\r\n");
                socket->flush();
                socket->disconnectFromHost();
                dataBuffer.clear();
                return;
            }

            if (dataBuffer.contains("GET /get_extension_config")) {
                int targetId = DatabaseManager::instance().extensionTargetCategoryId();
                QString catName = DatabaseManager::instance().getCategoryNameById(targetId);
                
                QJsonObject resp;
                resp["targetCategoryId"] = targetId;
                resp["targetCategoryName"] = catName;
                
                QByteArray json = QJsonDocument(resp).toJson(QJsonDocument::Compact);
                socket->write("HTTP/1.1 200 OK\r\n"
                              "Access-Control-Allow-Origin: *\r\n"
                              "Content-Type: application/json\r\n"
                              "Connection: close\r\n"
                              "\r\n" + json);
                socket->flush();
                socket->disconnectFromHost();
                dataBuffer.clear();
                return;
            }

            if (dataBuffer.contains("POST /add_note")) {
                qDebug() << "[HttpServer] 收到 POST /add_note 请求";
                int headerEndIndex = dataBuffer.indexOf("\r\n\r\n");
                QByteArray headers = dataBuffer.left(headerEndIndex);
                int bodyIndex = headerEndIndex + 4;

                // 获取 Content-Length (更加健壮的解析)
                int contentLength = 0;
                QList<QByteArray> headerLines = headers.split('\n');
                for (const auto& line : headerLines) {
                    QByteArray trimmedLine = line.trimmed().toLower();
                    if (trimmedLine.startsWith("content-length:")) {
                        contentLength = trimmedLine.mid(15).trimmed().toInt();
                        break;
                    }
                }

                // [SECURITY] 防御性校验：限制 Body 最大长度为 10MB，防止 OOM 闪退
                if (contentLength > 10 * 1024 * 1024) {
                    qWarning() << "[HttpServer] 拒绝超大数据包:" << contentLength;
                    socket->write("HTTP/1.1 413 Payload Too Large\r\nConnection: close\r\n\r\n");
                    socket->disconnectFromHost();
                    dataBuffer.clear();
                    return;
                }

                if (dataBuffer.size() < bodyIndex + contentLength) {
                    // [SECURITY] 如果缓冲区堆积过大（即使还没达到 contentLength），也应强制清理，防止缓慢攻击
                    if (dataBuffer.size() > 11 * 1024 * 1024) {
                        socket->disconnectFromHost();
                        dataBuffer.clear();
                    }
                    return; // 数据尚未接收完整
                }

                QByteArray body = dataBuffer.mid(bodyIndex, contentLength);
                
                // 尝试解析 JSON
                QJsonParseError err;
                QJsonDocument doc = QJsonDocument::fromJson(body, &err);
                if (!doc.isNull() && doc.isObject()) {
                    QJsonObject obj = doc.object();
                    qDebug() << "[HttpServer] JSON 解析成功，Body 长度:" << body.size();
                    QString rawContent = obj.value("content").toString();
                    QString url = obj.value("url").toString();
                    QString pageTitle = obj.value("pageTitle").toString();
                    
                    auto pairs = StringUtils::smartSplitPairs(rawContent);
                    qDebug() << "[HttpServer] 智能拆分得到对数:" << pairs.size();
                    if (pairs.isEmpty() && !rawContent.isEmpty()) {
                        pairs.append({pageTitle.isEmpty() ? "未命名灵感" : pageTitle, rawContent});
                    }

                    // [FIX] 开启批量模式：解决由于高频写入导致的授权一致性冲突(Race Condition)而触发的闪退
                    DatabaseManager::instance().beginBatch();
                    for (const auto& pair : pairs) {
                        qDebug() << "[HttpServer] 正在处理笔记:" << pair.first;
                        QString title = pair.first;
                        QString content = pair.second;
                        
                        if (!url.isEmpty()) {
                            // [CRITICAL] 必须与插件端生成的后缀完全一致，以触发数据库 content_hash 查重去重
                            content += "\n\n内容来源：- " + url;
                        }

                        QStringList tags = {"插件采集"};
                        if (StringUtils::containsThai(title) || StringUtils::containsThai(content)) {
                            tags << "泰文";
                        }

                        int targetCatId = DatabaseManager::instance().extensionTargetCategoryId();
                        
                        // [CRITICAL] 避免重复创建：多重互斥机制。
                        // 1. 显式开启 ignore 标记，此时任何剪贴板变化都不会入库。
                        // 2. 增加延迟至 1500ms，为浏览器的剪贴板写入和主程序的异步信号处理提供充足的互斥覆盖窗口。
                        ClipboardMonitor::instance().setIgnore(true);
                        
                        int noteId = DatabaseManager::instance().addNote(title, content, tags, "", targetCatId, "text", QByteArray(), "Browser", pageTitle);
                        qDebug() << "[HttpServer] 笔记入库完成，ID:" << noteId;
                        
                        QTimer::singleShot(1500, [](){
                            ClipboardMonitor::instance().setIgnore(false);
                        });
                    }
                    DatabaseManager::instance().endBatch(); // 提交事务并同步授权状态
                    
                    socket->write("HTTP/1.1 200 OK\r\n"
                                  "Access-Control-Allow-Origin: *\r\n"
                                  "Content-Type: application/json\r\n"
                                  "Connection: close\r\n"
                                  "\r\n"
                                  "{\"status\":\"success\"}");
                    socket->flush();
                    socket->disconnectFromHost();
                    dataBuffer.clear();
                } else if (err.error != QJsonParseError::NoError) {
                    // [NEW] 增强鲁棒性：如果 JSON 解析失败且数据已接收一定长度，则清理并报错，防止逻辑死锁
                    if (body.size() > 500) {
                        qWarning() << "[HttpServer] JSON 解析失败:" << err.errorString();
                        socket->write("HTTP/1.1 400 Bad Request\r\n"
                                      "Access-Control-Allow-Origin: *\r\n"
                                      "\r\n"
                                      "{\"status\":\"error\",\"message\":\"invalid json\"}");
                        socket->flush();
                        socket->disconnectFromHost();
                        dataBuffer.clear();
                    }
                }
            } else {
                socket->write("HTTP/1.1 404 Not Found\r\n\r\n");
                socket->disconnectFromHost();
            }
        }
    });

    connect(socket, &QTcpSocket::disconnected, [socket]() {
        socket->deleteLater();
    });
}
