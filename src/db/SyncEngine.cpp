#include "SyncEngine.h"
#include "Database.h"
#include "../meta/AmMetaJson.h"
#include <QFileInfo>
#include <QDirIterator>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDebug>
#include <QDateTime>

// 2026-03-24 按照用户要求：使用 SQLiteCpp 替代 QSql
#include <SQLiteCpp/SQLiteCpp.h>

SyncEngine& SyncEngine::instance() {
    static SyncEngine inst;
    return inst;
}

SyncEngine::SyncEngine(QObject* parent) : QObject(parent) {}
SyncEngine::~SyncEngine() {}

void SyncEngine::startIncrementalSync() {
    // 2026-03-24 [NEW] 实现增量同步逻辑：遍历 folders 表，对比 .am_meta.json 的 mtime
    QString lastSyncStr = Database::instance().getSyncState("last_sync_time");
    double lastSyncTime = lastSyncStr.toDouble();
    qDebug() << "[SyncEngine] 启动增量同步，上次同步基准时间戳:" << lastSyncTime;

    SQLite::Database* db = Database::instance().getRawDb();
    if (!db) return;

    QStringList foldersToUpdate;
    try {
        SQLite::Statement query(*db, "SELECT path FROM folders");
        while (query.executeStep()) {
            QString path = QString::fromStdString(query.getColumn(0).getText());
            QFileInfo metaInfo(path + "/.am_meta.json");
            if (metaInfo.exists()) {
                double mtime = metaInfo.lastModified().toMSecsSinceEpoch() / 1000.0;
                if (mtime > lastSyncTime) {
                    foldersToUpdate << path;
                }
            }
        }
    } catch (std::exception& e) {
        qCritical() << "[SyncEngine] 增量同步查询失败:" << e.what();
    }

    for (const QString& folder : foldersToUpdate) {
        syncFolder(folder);
    }

    updateLastSyncTime();
    Database::instance().rebuildTagsTable();
}

bool SyncEngine::syncFolder(const QString& folderPath) {
    AmMetaJson::FolderMeta fMeta;
    std::map<std::string, AmMetaJson::ItemMeta> items;

    if (!AmMetaJson::load(folderPath.toStdWString(), fMeta, items)) {
        return false;
    }

    QFileInfo info(folderPath + "/.am_meta.json");

    // 1. 更新文件夹元数据
    QVariantMap fMap;
    fMap["rating"] = fMeta.rating;
    fMap["color"] = QString::fromStdString(fMeta.color);
    fMap["pinned"] = fMeta.pinned ? 1 : 0;
    fMap["sort_by"] = QString::fromStdString(fMeta.sortBy);
    fMap["sort_order"] = QString::fromStdString(fMeta.sortOrder);
    fMap["last_sync"] = info.lastModified().toMSecsSinceEpoch() / 1000.0;

    QJsonArray tagsArr;
    for (const auto& t : fMeta.tags) tagsArr.append(QString::fromStdString(t));
    fMap["tags"] = QJsonDocument(tagsArr).toJson(QJsonDocument::Compact);

    Database::instance().updateFolderMeta(folderPath, fMap);

    // 2. 更新所有子项元数据
    for (const auto& [name, iMeta] : items) {
        QVariantMap iMap;
        iMap["type"] = QString::fromStdString(iMeta.type);
        iMap["rating"] = iMeta.rating;
        iMap["color"] = QString::fromStdString(iMeta.color);
        iMap["pinned"] = iMeta.pinned ? 1 : 0;
        iMap["parent_path"] = folderPath;

        QJsonArray iTagsArr;
        for (const auto& t : iMeta.tags) iTagsArr.append(QString::fromStdString(t));
        iMap["tags"] = QJsonDocument(iTagsArr).toJson(QJsonDocument::Compact);

        QString itemPath = folderPath + "\\" + QString::fromStdString(name);
        Database::instance().updateItemMeta(itemPath, iMap);
    }

    return true;
}

void SyncEngine::startFullScan(std::function<void(int current, int total)> progressCallback) {
    // 2026-03-24 按照用户要求：实现物理全量扫描逻辑
    qDebug() << "[SyncEngine] 启动全量扫描...";

    // 1. 获取所有逻辑驱动器
    QFileInfoList drives = QDir::drives();
    int current = 0;

    for (const QFileInfo& drive : drives) {
        QDirIterator it(drive.absoluteFilePath(), QStringList() << ".am_meta.json",
                         QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot,
                         QDirIterator::Subdirectories);

        while (it.hasNext()) {
            it.next();
            QString folderPath = it.fileInfo().absolutePath();
            syncFolder(folderPath);
            current++;
            if (progressCallback) progressCallback(current, -1);
        }
    }

    updateLastSyncTime();
    Database::instance().rebuildTagsTable();
    qDebug() << "[SyncEngine] 全量扫描完成，发现并同步了" << current << "个元数据文件夹。";
}

void SyncEngine::updateLastSyncTime() {
    QString now = QString::number(QDateTime::currentDateTime().toMSecsSinceEpoch() / 1000.0);
    Database::instance().updateSyncState("last_sync_time", now);
}
