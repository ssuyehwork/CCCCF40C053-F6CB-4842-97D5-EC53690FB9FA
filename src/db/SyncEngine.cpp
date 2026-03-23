#include "SyncEngine.h"
#include "Database.h"
#include "../meta/AmMetaJson.h"
#include "../meta/SyncQueue.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>
#include <QFileInfo>
#include <QDirIterator>
#include <QDebug>
#include <unordered_map>

SyncEngine& SyncEngine::instance() {
    static SyncEngine inst;
    return inst;
}

void SyncEngine::incrementalSync() {
    QSqlDatabase db = QSqlDatabase::database("file_index_db");
    if (!db.isOpen()) return;

    QSqlQuery q(db);
    q.exec("SELECT value FROM sync_state WHERE key = 'last_sync_time'");
    double lastSyncTime = 0.0;
    if (q.next()) lastSyncTime = q.value(0).toDouble();

    q.exec("SELECT path FROM folders");
    while (q.next()) {
        QString path = q.value(0).toString();
        // 2026-03-24 按照用户要求：使用 Windows 风格反斜杠拼接路径
        QFileInfo fi(path + "\\.am_meta.json");
        if (fi.exists() && fi.lastModified().toMSecsSinceEpoch() / 1000.0 > lastSyncTime) {
            SyncQueue::instance().enqueue(path.toStdWString());
        }
    }

    // 更新最后同步时间
    q.prepare("INSERT OR REPLACE INTO sync_state (key, value) VALUES ('last_sync_time', ?)");
    q.addBindValue(QString::number(QDateTime::currentDateTime().toMSecsSinceEpoch() / 1000.0));
    q.exec();
}

void SyncEngine::fullScan(std::function<void(int current, int total)> progressCallback) {
    QSqlDatabase db = QSqlDatabase::database("file_index_db");
    if (!db.isOpen()) return;

    db.transaction();
    QSqlQuery q(db);
    q.exec("DELETE FROM folders");
    q.exec("DELETE FROM items");
    q.exec("DELETE FROM tags");
    db.commit();

    // 假设在 C 盘根目录下扫描 (示例，实际应用中由用户指定或预设)
    QString root = "C:/";
    QDirIterator it(root, QStringList() << ".am_meta.json", QDir::Files, QDirIterator::Subdirectories);
    std::vector<QString> metaFiles;
    while (it.hasNext()) {
        metaFiles.push_back(it.next());
    }

    int total = (int)metaFiles.size();
    for (int i = 0; i < total; ++i) {
        QString folderPath = QFileInfo(metaFiles[i]).absolutePath();
        SyncQueue::instance().enqueue(folderPath.toStdWString());
        if (progressCallback) progressCallback(i + 1, total);
    }

    // 扫描完成后聚合标签
    aggregateTags();
}

void SyncEngine::aggregateTags() {
    QSqlDatabase db = QSqlDatabase::database("file_index_db");
    if (!db.isOpen()) return;

    QSqlQuery q(db);
    std::unordered_map<QString, int> tagCounts;

    // 从 folders 统计
    q.exec("SELECT tags FROM folders");
    while (q.next()) {
        QJsonArray tags = QJsonDocument::fromJson(q.value(0).toString().toUtf8()).array();
        for (const auto& t : tags) tagCounts[t.toString()]++;
    }

    // 从 items 统计
    q.exec("SELECT tags FROM items");
    while (q.next()) {
        QJsonArray tags = QJsonDocument::fromJson(q.value(0).toString().toUtf8()).array();
        for (const auto& t : tags) tagCounts[t.toString()]++;
    }

    db.transaction();
    q.exec("DELETE FROM tags");
    for (auto const& [tag, count] : tagCounts) {
        q.prepare("INSERT INTO tags (tag, item_count) VALUES (?, ?)");
        q.addBindValue(tag);
        q.addBindValue(count);
        q.exec();
    }
    db.commit();
}
