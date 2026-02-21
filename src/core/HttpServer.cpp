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
            if (dataBuffer->startsWith("OPTIONS")) {
                socket->write("HTTP/1.1 204 No Content\r\n"
                              "Access-Control-Allow-Origin: *\r\n"
                              "Access-Control-Allow-Methods: POST, OPTIONS\r\n"
                              "Access-Control-Allow-Headers: Content-Type, Authorization\r\n"
                              "Access-Control-Max-Age: 86400\r\n"
                              "\r\n");
                socket->flush();
                socket->disconnectFromHost();
                return;
            }

            if (dataBuffer->startsWith("POST /add_note")) {
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

                        DatabaseManager::instance().addNoteAsync(title, content, tags, "", -1, "text", QByteArray(), "Browser", pageTitle);
                    }
                    
                    socket->write("HTTP/1.1 200 OK\r\n"
                                  "Access-Control-Allow-Origin: *\r\n"
                                  "Content-Type: application/json\r\n"
                                  "Connection: close\r\n"
                                  "\r\n"
                                  "{\"status\":\"success\"}");
                    socket->flush();
                    socket->disconnectFromHost();
                } else if (err.error != QJsonParseError::NoError && body.size() > 1024 * 10) {
                    // 防止恶意大数据或解析错误导致死循环
                    socket->write("HTTP/1.1 400 Bad Request\r\n\r\n");
                    socket->disconnectFromHost();
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
