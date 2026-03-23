#include "SyncEngine.h"
#include <QDateTime>
#include <QSqlQuery>
#include <QSqlError>
#include <QFileInfo>
#include <QDirIterator>
#include "Database.h"
#include "../meta/SyncQueue.h"
#include "../meta/AmMetaJson.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

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
    
#ifdef Q_OS_WIN
    // 枚举所有固定磁盘并递归扫描
    DWORD drives = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if (m_abort) break;
        if (drives & (1 << i)) {
            wchar_t driveRootW[] = { (wchar_t)(L'A' + i), L':', L'\\', L'\0' };
            if (GetDriveTypeW(driveRootW) == DRIVE_FIXED) {
                QString driveRoot = QString::fromWCharArray(driveRootW);
                QDirIterator it(driveRoot, QStringList() << meta::AmMetaJson::META_FILENAME, 
                                QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, 
                                QDirIterator::Subdirectories);
                
                while (it.hasNext()) {
                    if (m_abort) break;
                    it.next();
                    QString folderPath = it.fileInfo().absolutePath();
                    meta::SyncQueue::instance().enqueue(folderPath);
                }
            }
        }
    }
#endif
    
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
