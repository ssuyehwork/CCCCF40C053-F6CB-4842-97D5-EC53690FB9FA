#ifndef SYNCENGINE_H
#define SYNCENGINE_H

#include <string>
#include <vector>
#include <functional>
#include <QObject>

class SyncEngine : public QObject {
    Q_OBJECT
public:
    static SyncEngine& instance();

    // 启动时的增量同步
    void incrementalSync();

    // 用户手动触发的全量扫描
    void fullScan(std::function<void(int current, int total)> progressCallback = nullptr);

    // 重新聚合标签统计
    void aggregateTags();

private:
    SyncEngine() = default;
};

#endif // SYNCENGINE_H
