#ifndef CLIPBOARDMONITOR_H
#define CLIPBOARDMONITOR_H

#include <QObject>
#include <QClipboard>
#include <QGuiApplication>
#include <QCryptographicHash>
#include <QStringList>

class ClipboardMonitor : public QObject {
    Q_OBJECT
public:
    explicit ClipboardMonitor(QObject* parent = nullptr);
    void skipNext() { m_skipNext = true; }

signals:
    void newContentDetected(const QString& content, const QString& type, const QByteArray& data = QByteArray(),
                            const QString& sourceApp = "", const QString& sourceTitle = "");
    void clipboardChanged();

private slots:
    void onClipboardChanged();

private:
    QString m_lastHash;
    bool m_skipNext = false;
};

#endif // CLIPBOARDMONITOR_H