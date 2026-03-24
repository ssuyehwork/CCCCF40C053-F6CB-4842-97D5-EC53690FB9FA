#ifndef SYNCQUEUE_H
#define SYNCQUEUE_H

#include <QString>
#include <QSet>
#include <QMutex>
#include <QThread>
#include <QTimer>
#include <QObject>

class SyncQueue : public QObject {
    Q_OBJECT
public:
    static SyncQueue& instance();

    // 2026-03-24 [NEW] 将变更路径加入同步队列（含防抖合并）
    void enqueue(const QString& folderPath);
    
    // 强制立即同步所有挂起的路径（用于关闭前）
    void flush();

signals:
    void syncRequired(const QStringList& paths);

private:
    explicit SyncQueue(QObject* parent = nullptr);
    ~SyncQueue();

    QSet<QString> m_pendingPaths;
    QMutex m_mutex;
    QTimer* m_debounceTimer;
};

#endif // SYNCQUEUE_H
