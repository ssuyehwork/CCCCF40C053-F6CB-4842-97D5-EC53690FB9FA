#ifndef EXTENSIONSERVER_H
#define EXTENSIONSERVER_H

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QMap>

class ExtensionServer : public QObject {
    Q_OBJECT
public:
    static ExtensionServer& instance();
    void start(uint16_t port = 9090);
    void stop();

private slots:
    void onNewConnection();
    void onReadyRead();
    void onDisconnected();

private:
    ExtensionServer(QObject* parent = nullptr);
    ~ExtensionServer();

    QTcpServer* m_server;
    QMap<QTcpSocket*, QByteArray> m_buffers;
    void processRequest(QTcpSocket* socket, const QByteArray& data);
};

#endif // EXTENSIONSERVER_H
