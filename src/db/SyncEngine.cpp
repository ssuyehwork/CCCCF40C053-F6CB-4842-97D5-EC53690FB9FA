#include "SyncEngine.h"
#include "Database.h"
#include "../meta/SyncQueue.h"
#include <windows.h>
#include <QDateTime>
#include <QFileInfo>
#include <QDir>
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlDatabase>
#include <QJsonDocument>
#include <QJsonArray>
#include <filesystem>
#include <map>

namespace ArcMeta {

SyncEngine& SyncEngine::instance() {
    static SyncEngine inst;
    return inst;
}

/**
 * @brief 增量同步：只处理 mtime > last_sync_time 的 .am_meta.json
 */
void SyncEngine::runIncrementalSync() {
    double lastSyncTime = 0;
    
    // 2026-03-xx 修复：通过 getThreadDatabase 获取当前线程专属连接，消除异步任务中的跨线程数据库警告。
    QSqlDatabase db = ArcMeta::Database::instance().getThreadDatabase();
    if (!db.isOpen()) return;

    // 获取上次同步时间
    QSqlQuery st(db);
    st.exec("SELECT value FROM sync_state WHERE key = 'last_sync_time'");
    if (st.next()) {
        lastSyncTime = st.value(0).toDouble();
    }

    // 执行全表增量扫描逻辑
    QSqlQuery query(db);
    query.exec("SELECT path FROM folders");
    while (query.next()) {
        std::wstring path = query.value(0).toString().toStdWString();
        std::wstring jsonPath = path + L"\\.am_meta.json";
        
        QFileInfo info(QString::fromStdWString(jsonPath));
        if (info.exists() && info.lastModified().toMSecsSinceEpoch() > lastSyncTime) {
            SyncQueue::instance().enqueue(path);
        }
    }
}

/**
 * @brief 全量扫描：递归所有盘符搜集元数据
 * 2026-03-xx 物理修复：后台全量扫描必须使用独立线程连接，防止 UI 挂起。
 */
void SyncEngine::runFullScan(std::function<void(int current, int total)> onProgress) {
    std::vector<std::wstring> metaFiles;
    
    // 枚举所有固定驱动器
    DWORD drives = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if (drives & (1 << i)) {
            wchar_t drivePath[] = { (wchar_t)(L'A' + i), L':', L'\\', L'\0' };
            if (GetDriveTypeW(drivePath) == DRIVE_FIXED) {
                scanDirectory(std::filesystem::path(drivePath), metaFiles);
            }
        }
    }

    QSqlDatabase db = ArcMeta::Database::instance().getThreadDatabase();
    if (!db.isOpen()) return;

    // 清理并重建核心表
    QSqlQuery q(db);
    q.exec("DELETE FROM folders");
    q.exec("DELETE FROM items");
    q.exec("DELETE FROM tags");

    int total = (int)metaFiles.size();
    for (int i = 0; i < total; ++i) {
        // 提取父目录并加入队列进行解析同步
        std::wstring parentDir = std::filesystem::path(metaFiles[i]).parent_path().wstring();
        SyncQueue::instance().enqueue(parentDir);
        
        if (onProgress) onProgress(i + 1, total);
    }
    
    // 强制刷空队列
    SyncQueue::instance().flush();
    // 更新同步时间
    QSqlQuery updateSync(db);
    updateSync.prepare("INSERT OR REPLACE INTO sync_state (key, value) VALUES ('last_sync_time', ?)");
    updateSync.addBindValue((double)QDateTime::currentMSecsSinceEpoch());
    updateSync.exec();
    
    rebuildTagStats();
}

/**
 * @brief 标签聚合统计逻辑
 * 2026-03-xx 物理修复：聚合任务属于重度 IO 操作，强制要求在后台线程独立连接中运行事务。
 */
void SyncEngine::rebuildTagStats() {
    QSqlDatabase db = ArcMeta::Database::instance().getThreadDatabase();
    if (!db.isOpen()) return;

    db.transaction();
    
    QSqlQuery qDelete(db);
    qDelete.exec("DELETE FROM tags");
    
    // 聚合 items 表中的标签
    QSqlQuery query("SELECT tags FROM items WHERE tags != ''", db);
    std::map<std::string, int> tagCounts;
    while (query.next()) {
        QByteArray jsonData = query.value(0).toByteArray();
        QJsonDocument doc = QJsonDocument::fromJson(jsonData);
        if (doc.isArray()) {
            for (const auto& val : doc.array()) {
                QString t = val.toString();
                if (!t.isEmpty()) tagCounts[t.toStdString()]++;
            }
        }
    }

    for (const auto& [tag, count] : tagCounts) {
        QSqlQuery ins(db);
        ins.prepare("INSERT INTO tags (tag, item_count) VALUES (?, ?)");
        ins.addBindValue(QString::fromStdString(tag));
        ins.addBindValue(count);
        ins.exec();
    }

    db.commit();
}

/**
 * @brief 递归扫描目录（排除系统隐藏文件夹）
 */
void SyncEngine::scanDirectory(const std::filesystem::path& root, std::vector<std::wstring>& metaFiles) {
    try {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(root, std::filesystem::directory_options::skip_permission_denied)) {
            if (entry.is_regular_file() && entry.path().filename() == ".am_meta.json") {
                metaFiles.push_back(entry.path().wstring());
            }
        }
    } catch (...) {
        // 无权限访问目录
    }
}

} // namespace ArcMeta
