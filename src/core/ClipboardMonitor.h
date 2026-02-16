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
    static ClipboardMonitor& instance();
    void skipNext() { m_skipNext = true; }
    void forceNext(const QString& type = "") { m_forceNext = true; m_forcedType = type; }

signals:
    void newContentDetected(const QString& content, const QString& type, const QByteArray& data = QByteArray(),
                            const QString& sourceApp = "", const QString& sourceTitle = "");
    void clipboardChanged();

private slots:
    void onClipboardChanged();

private:
    ClipboardMonitor(QObject* parent = nullptr);
    QString m_lastHash;
    bool m_skipNext = false;
    bool m_forceNext = false;
    QString m_forcedType;
};

#endif // CLIPBOARDMONITOR_H