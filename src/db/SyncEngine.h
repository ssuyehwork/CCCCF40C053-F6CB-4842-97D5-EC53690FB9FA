#ifndef SYNCENGINE_H
#define SYNCENGINE_H

#include <QObject>
#include <QString>
#include <QDateTime>
#include <functional>
#include "../meta/AmMetaJson.h"

class SyncEngine : public QObject {
    Q_OBJECT
public:
    static SyncEngine& instance();

    // 2026-03-24 [NEW] 增量同步：仅处理 mtime > last_sync_time 的文件夹
    void startIncrementalSync();
    
    // 2026-03-24 [NEW] 全量扫描：用户手动触发，重建所有索引
    void startFullScan(std::function<void(int current, int total)> progressCallback = nullptr);

    // 解析并同步单个文件夹路径到数据库
    bool syncFolder(const QString& folderPath);

private:
    explicit SyncEngine(QObject* parent = nullptr);
    ~SyncEngine();

    void updateLastSyncTime();
    QStringList collectAllMetaFiles(const QString& rootPath);
};

#endif // SYNCENGINE_H
