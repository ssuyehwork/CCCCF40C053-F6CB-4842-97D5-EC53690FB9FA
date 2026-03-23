#ifndef SYNCQUEUE_H
#define SYNCQUEUE_H

#include <QString>
#include <QSet>
#include <QMutex>
#include <QThread>
#include <QTimer>
#include <QQueue>
#include <QObject>

namespace meta {

class SyncQueue : public QObject {
    Q_OBJECT
public:
    static SyncQueue& instance();

    // 添加到更新队列，支持去重
    void enqueue(const QString& folderPath);

    // 立即刷新队列（程序退出前必须调用）
    void flush();

signals:
    // 同步完成信号
    void syncFinished(const QString& folderPath);

private:
    SyncQueue(QObject* parent = nullptr);
    ~SyncQueue();
    SyncQueue(const SyncQueue&) = delete;
    SyncQueue& operator=(const SyncQueue&) = delete;

    void processQueue();

    QSet<QString> m_pendingPaths; // 使用 QSet 实现防抖合并
    QMutex m_mutex;
    QTimer* m_syncTimer;
    bool m_isProcessing = false;
};

} // namespace meta

#endif // SYNCQUEUE_H
