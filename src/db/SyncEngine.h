#ifndef SYNCENGINE_H
#define SYNCENGINE_H

#include <QString>
#include <QObject>
#include <functional>
#include <atomic>

namespace db {

class SyncEngine : public QObject {
    Q_OBJECT
public:
    static SyncEngine& instance();

    // 增量同步：基于 last_sync_time 和文件 mtime
    void startIncrementalSync();

    // 全量扫描：忽略时间戳，重建全量索引
    void startFullScan(std::function<void(int current, int total)> progressCallback = nullptr);

    // 停止同步
    void stop();

private:
    SyncEngine() = default;
    ~SyncEngine() = default;
    SyncEngine(const SyncEngine&) = delete;
    SyncEngine& operator=(const SyncEngine&) = delete;

    void updateLastSyncTime();

    std::atomic<bool> m_abort{false};
};

} // namespace db

#endif // SYNCENGINE_H
