#include "HttpServer.h"
#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrlQuery>
#include <QDebug>
#include <QFile>
#include "../ui/StringUtils.h"
#include "../db/CategoryRepo.h"
#include "../db/ItemRepo.h"
#include "FileStorageHelper.h"
#include "FileResourceManager.h"

HttpServer& HttpServer::instance() {
    static HttpServer inst;
    return inst;
}

HttpServer::HttpServer(QObject *parent) : QTcpServer(parent) {}

bool HttpServer::start(quint16 port) {
    if (isListening()) return true;
    bool ok = listen(QHostAddress::LocalHost, port);
    if (!ok) {
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

    class BufferHolder : public QObject { public: QByteArray data; };
    BufferHolder* holder = new BufferHolder();
    holder->setParent(socket);

    connect(socket, &QTcpSocket::readyRead, [this, socket, holder]() {
        QByteArray& dataBuffer = holder->data;
        if (dataBuffer.size() > 12 * 1024 * 1024) {
            qWarning() << "[HttpServer] 缓冲区溢出，强制断开连接";
            socket->disconnectFromHost();
            return;
        }

        dataBuffer.append(socket->readAll());
        
        if (dataBuffer.contains("\r\n\r\n")) {
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
                // 2026-04-04 重构：暂时固定为 0 (根分类) 或从 QSettings 读取
                int targetId = 0;
                QString catName = "根分类";
                
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

            int lineEnd = dataBuffer.indexOf("\r\n");
            QByteArray requestLine = dataBuffer.left(lineEnd);
            QString reqStr = QString::fromUtf8(requestLine);
            
            auto sendJsonResponse = [&](const QJsonObject& obj, int code = 200) {
                QByteArray json = QJsonDocument(obj).toJson(QJsonDocument::Compact);
                QString header = QString("HTTP/1.1 %1 %2\r\n"
                                         "Access-Control-Allow-Origin: *\r\n"
                                         "Content-Type: application/json\r\n"
                                         "Connection: close\r\n"
                                         "\r\n").arg(code).arg(code == 200 ? "OK" : "Error");
                socket->write(header.toUtf8() + json);
                socket->flush();
                socket->disconnectFromHost();
                dataBuffer.clear();
            };

            // 2026-04-04 重构：只读查询接口待适配 ArcMeta 物理项模型
            if (reqStr.contains("GET /api/read/search") || reqStr.contains("GET /api/full/search")) {
                QJsonObject resp;
                resp["status"] = "success";
                resp["data"] = QJsonArray();
                resp["message"] = "Search API is being refactored for physical resources.";
                sendJsonResponse(resp);
                return;
            }

            if (dataBuffer.contains("POST /add_note") || dataBuffer.contains("POST /api/full/add")) {
                int headerEndIndex = dataBuffer.indexOf("\r\n\r\n");
                QByteArray headers = dataBuffer.left(headerEndIndex);
                int bodyIndex = headerEndIndex + 4;

                int contentLength = 0;
                QList<QByteArray> headerLines = headers.split('\n');
                for (const auto& line : headerLines) {
                    QByteArray trimmedLine = line.trimmed().toLower();
                    if (trimmedLine.startsWith("content-length:")) {
                        contentLength = trimmedLine.mid(15).trimmed().toInt();
                        break;
                    }
                }

                if (dataBuffer.size() < bodyIndex + contentLength) return;

                QByteArray body = dataBuffer.mid(bodyIndex, contentLength);
                QJsonDocument doc = QJsonDocument::fromJson(body);
                if (!doc.isNull() && doc.isObject()) {
                    QJsonObject obj = doc.object();
                    
                    if (reqStr.contains("/add") || reqStr.contains("/add_note")) {
                        QString rawContent = obj.value("content").toString();
                        QString url = obj.value("url").toString();
                        QString pageTitle = obj.value("pageTitle").toString();
                        
                        QString title = rawContent.trimmed().left(40).replace("\r", " ").replace("\n", " ").simplified();
                        if (title.isEmpty()) title = pageTitle.isEmpty() ? "未命名灵感" : pageTitle;

                        QString content = rawContent;
                        if (!url.isEmpty() && !content.contains(url)) {
                            content += "\n\n内容来源：" + url;
                        }

                        // 2026-04-04 重构：将插件采集内容存为物理 Markdown 文件
                        QString storageDir = FileStorageHelper::getStorageRoot();
                        QString safeTitle = title;
                        safeTitle.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");
                        QString destPath = FileStorageHelper::getUniqueFilePath(storageDir, safeTitle + ".md");

                        QFile f(destPath);
                        if (f.open(QIODevice::WriteOnly)) {
                            f.write(content.toUtf8());
                            f.close();

                            // 关联到“插件采集”分类 (如果不存在则创建)
                            int targetCatId = 0;
                            auto cats = ArcMeta::CategoryRepo::getAll();
                            for(const auto& c : cats) {
                                if (c.name == L"插件采集") { targetCatId = c.id; break; }
                            }
                            if (targetCatId == 0) {
                                ArcMeta::Category newCat;
                                newCat.name = L"插件采集";
                                ArcMeta::CategoryRepo::add(newCat);
                                targetCatId = newCat.id;
                            }

                            ArcMeta::CategoryRepo::addItemToCategory(targetCatId, destPath.toStdWString());

                            // 物理元数据初始化
                            ArcMeta::ItemMeta meta;
                            meta.type = L"file";
                            meta.originalName = (safeTitle + ".md").toStdWString();
                            meta.tags = { L"插件采集" };
                            ArcMeta::FileResourceManager::instance().setItemMeta(destPath, meta);

                            QJsonObject resp; resp["status"] = "success"; resp["path"] = destPath;
                            sendJsonResponse(resp);
                        } else {
                            QJsonObject resp; resp["status"] = "error"; resp["message"] = "failed to save file";
                            sendJsonResponse(resp, 500);
                        }
                        return;
                    }
                }
            } else {
                socket->write("HTTP/1.1 404 Not Found\r\n\r\n");
                socket->disconnectFromHost();
            }
        }
    });

    connect(socket, &QTcpSocket::disconnected, socket, [socket]() {
        socket->deleteLater();
    });
}
