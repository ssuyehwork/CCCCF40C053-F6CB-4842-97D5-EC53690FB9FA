#include "SyncEngine.h"
#include <QDateTime>
#include <QSqlQuery>
#include <QSqlError>
#include <QFileInfo>
#include <QDirIterator>
#include "Database.h"
#include "../meta/SyncQueue.h"
#include "../meta/AmMetaJson.h"

namespace db {

SyncEngine& SyncEngine::instance() {
    static SyncEngine inst;
    return inst;
}

void SyncEngine::startIncrementalSync() {
    m_abort = false;

    // 1. 获取上次同步时间
    QSqlQuery query(Database::instance().getDb());
    query.prepare("SELECT value FROM sync_state WHERE key = 'last_sync_time'");
    double lastSyncTime = 0;
    if (query.exec() && query.next()) {
        lastSyncTime = query.value(0).toDouble();
    }

    // 2. 遍历所有已知的文件夹路径 (从 folders 表获取)
    query.exec("SELECT path FROM folders");
    QStringList paths;
    while (query.next()) {
        paths.append(query.value(0).toString());
    }

    // 3. 检查 .am_meta.json 的 mtime 是否大于 lastSyncTime
    for (const QString& path : paths) {
        if (m_abort) break;

        QString metaPath = meta::AmMetaJson::getMetaPath(path);
        QFileInfo info(metaPath);
        if (info.exists() && info.lastModified().toMSecsSinceEpoch() / 1000.0 > lastSyncTime) {
            meta::SyncQueue::instance().enqueue(path);
        }
    }

    updateLastSyncTime();
}

void SyncEngine::startFullScan(std::function<void(int current, int total)> progressCallback) {
    m_abort = false;

    // 清空部分表 (保持 folders 和 items 可追溯，但同步时会 REPLACE)
    // 实际上全量扫描通常用于清理不存在的记录

    // 示例逻辑：递归查找所有磁盘上的 .am_meta.json
    // 这里以 C: 盘为例，实际开发中应支持配置盘符
    QDirIterator it("C:\\", QStringList() << meta::AmMetaJson::META_FILENAME,
                    QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);

    while (it.hasNext()) {
        if (m_abort) break;
        it.next();
        QString folderPath = it.fileInfo().absolutePath();
        meta::SyncQueue::instance().enqueue(folderPath);
    }

    updateLastSyncTime();
}

void SyncEngine::stop() {
    m_abort = true;
}

void SyncEngine::updateLastSyncTime() {
    double now = QDateTime::currentDateTime().toMSecsSinceEpoch() / 1000.0;
    QSqlQuery query(Database::instance().getDb());
    query.prepare("INSERT OR REPLACE INTO sync_state (key, value) VALUES ('last_sync_time', ?)");
    query.addBindValue(now);
    query.exec();
}

} // namespace db
