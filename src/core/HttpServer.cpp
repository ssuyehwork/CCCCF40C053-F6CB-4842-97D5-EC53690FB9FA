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

    auto dataBuffer = new QByteArray();

    connect(socket, &QTcpSocket::readyRead, [this, socket, dataBuffer]() {
        dataBuffer->append(socket->readAll());
        
        if (dataBuffer->contains("\r\n\r\n")) {
            // 处理 OPTIONS 预检请求 (CORS)
            if (dataBuffer->contains("OPTIONS")) {
                socket->write("HTTP/1.1 204 No Content\r\n"
                              "Access-Control-Allow-Origin: *\r\n"
                              "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                              "Access-Control-Allow-Headers: Content-Type, Authorization\r\n"
                              "Access-Control-Max-Age: 86400\r\n"
                              "\r\n");
                socket->flush();
                socket->disconnectFromHost();
                dataBuffer->clear();
                return;
            }

            if (dataBuffer->contains("GET /get_extension_config")) {
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
                dataBuffer->clear();
                return;
            }

            if (dataBuffer->contains("POST /add_note")) {
                int headerEndIndex = dataBuffer->indexOf("\r\n\r\n");
                QByteArray headers = dataBuffer->left(headerEndIndex);
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

                if (dataBuffer->size() < bodyIndex + contentLength) {
                    return; // 数据尚未接收完整
                }

                QByteArray body = dataBuffer->mid(bodyIndex, contentLength);
                
                // 尝试解析 JSON
                QJsonParseError err;
                QJsonDocument doc = QJsonDocument::fromJson(body, &err);
                if (!doc.isNull() && doc.isObject()) {
                    QJsonObject obj = doc.object();
                    QString rawContent = obj.value("content").toString();
                    QString url = obj.value("url").toString();
                    QString pageTitle = obj.value("pageTitle").toString();
                    
                    auto pairs = StringUtils::smartSplitPairs(rawContent);
                    if (pairs.isEmpty() && !rawContent.isEmpty()) {
                        pairs.append({pageTitle.isEmpty() ? "未命名灵感" : pageTitle, rawContent});
                    }

                    for (const auto& pair : pairs) {
                        QString title = pair.first;
                        QString content = pair.second;
                        
                        if (!url.isEmpty()) {
                            content += "\n\n内容来源：- " + url;
                        }

                        QStringList tags = {"插件采集"};
                        if (StringUtils::containsThai(title) || StringUtils::containsThai(content)) {
                            tags << "泰文";
                        }

                        int targetCatId = DatabaseManager::instance().extensionTargetCategoryId();

                        // [CRITICAL] 避免重复创建：双重保险。1. 忽略接下来 2s 内的剪贴板变化；2. 显式开启 ignore 标记。
                        ClipboardMonitor::instance().skipNext();
                        ClipboardMonitor::instance().setIgnore(true);

                        DatabaseManager::instance().addNote(title, content, tags, "", targetCatId, "text", QByteArray(), "Browser", pageTitle);

                        // 稍微延迟恢复监听，确保剪贴板操作（如果有）已完成
                        QTimer::singleShot(500, [](){
                            ClipboardMonitor::instance().setIgnore(false);
                        });
                    }
                    
                    socket->write("HTTP/1.1 200 OK\r\n"
                                  "Access-Control-Allow-Origin: *\r\n"
                                  "Content-Type: application/json\r\n"
                                  "Connection: close\r\n"
                                  "\r\n"
                                  "{\"status\":\"success\"}");
                    socket->flush();
                    socket->disconnectFromHost();
                    dataBuffer->clear();
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
                        dataBuffer->clear();
                    }
                }
            } else {
                socket->write("HTTP/1.1 404 Not Found\r\n\r\n");
                socket->disconnectFromHost();
            }
        }
    });

    connect(socket, &QTcpSocket::disconnected, [socket, dataBuffer]() {
        delete dataBuffer;
        socket->deleteLater();
    });
}
