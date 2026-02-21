#include "ExtensionServer.h"
#include "DatabaseManager.h"
#include "../ui/ToolTipOverlay.h"
#include "../ui/StringUtils.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QCursor>

ExtensionServer& ExtensionServer::instance() {
    static ExtensionServer inst;
    return inst;
}

ExtensionServer::ExtensionServer(QObject* parent) : QObject(parent) {
    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection, this, &ExtensionServer::onNewConnection);
}

ExtensionServer::~ExtensionServer() {
    stop();
}

void ExtensionServer::start(uint16_t port) {
    if (m_server->isListening()) return;
    if (m_server->listen(QHostAddress::LocalHost, port)) {
        qDebug() << "[ExtensionServer] 服务已启动，监听端口:" << port;
    } else {
        qWarning() << "[ExtensionServer] 服务启动失败:" << m_server->errorString();
    }
}

void ExtensionServer::stop() {
    if (m_server->isListening()) {
        m_server->close();
        qDebug() << "[ExtensionServer] 服务已停止";
    }
}

void ExtensionServer::onNewConnection() {
    while (m_server->hasPendingConnections()) {
        QTcpSocket* socket = m_server->nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, this, &ExtensionServer::onReadyRead);
        connect(socket, &QTcpSocket::disconnected, this, &ExtensionServer::onDisconnected);
    }
}

void ExtensionServer::onReadyRead() {
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;

    QByteArray data = socket->readAll();
    processRequest(socket, data);
}

void ExtensionServer::onDisconnected() {
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (socket) {
        socket->deleteLater();
    }
}

void ExtensionServer::processRequest(QTcpSocket* socket, const QByteArray& data) {
    QString request = QString::fromUtf8(data);

    // 处理 CORS 预检请求 (OPTIONS)
    if (request.startsWith("OPTIONS")) {
        socket->write("HTTP/1.1 204 No Content\r\n"
                      "Access-Control-Allow-Origin: *\r\n"
                      "Access-Control-Allow-Methods: POST, OPTIONS\r\n"
                      "Access-Control-Allow-Headers: Content-Type\r\n"
                      "\r\n");
        socket->flush();
        return;
    }

    // 简单的解析 POST Body (仅处理 POST 请求)
    if (!request.startsWith("POST")) return;

    int bodyIndex = data.indexOf("\r\n\r\n");
    if (bodyIndex == -1) return;

    QByteArray body = data.mid(bodyIndex + 4);
    QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject()) return;

    QJsonObject obj = doc.object();
    QString title = obj.value("title").toString().trimmed();
    QString content = obj.value("content").toString().trimmed();
    QString url = obj.value("url").toString();
    QJsonArray tagsArray = obj.value("tags").toArray();

    QStringList tags;
    for (int i = 0; i < tagsArray.size(); ++i) {
        tags << tagsArray.at(i).toString();
    }
    if (!tags.contains("插件采集")) tags << "插件采集";

    if (content.isEmpty()) {
        socket->write("HTTP/1.1 400 Bad Request\r\n\r\n");
        socket->flush();
        return;
    }

    if (title.isEmpty()) {
        title = content.left(30);
        if (content.length() > 30) title += "...";
    }

    // [BYPASS CLIPBOARD] 直接入库
    DatabaseManager::instance().addNoteAsync(title, content, tags, "", -1, "text", QByteArray(), "BrowserExtension", url);

    // 发送响应
    socket->write("HTTP/1.1 200 OK\r\n"
                  "Access-Control-Allow-Origin: *\r\n"
                  "Content-Type: application/json\r\n"
                  "\r\n"
                  "{\"status\":\"success\"}");
    socket->flush();

    // 反馈 UI
    QString feedback = "✔ 插件已直接保存灵感: " + (title.length() > 20 ? title.left(17) + "..." : title);
    ToolTipOverlay::instance()->showText(QCursor::pos(), feedback);
}
