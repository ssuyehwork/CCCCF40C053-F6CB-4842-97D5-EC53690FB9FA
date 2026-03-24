#include "Database.h"
#include <QSqlRecord>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonArray>
#include <QSet>

Database& Database::instance() {
    static Database inst;
    return inst;
}

Database::Database(QObject* parent) : QObject(parent) {}

Database::~Database() {
    if (m_db.isOpen()) m_db.close();
}

bool Database::init(const QString& dbPath) {
    QMutexLocker locker(&m_mutex);
    if (m_db.isOpen()) m_db.close();

    QString connName = "FileIndex_Conn";
    if (QSqlDatabase::contains(connName)) {
        m_db = QSqlDatabase::database(connName);
    } else {
        m_db = QSqlDatabase::addDatabase("QSQLITE", connName);
    }
    
    m_db.setDatabaseName(dbPath);
    if (!m_db.open()) {
        qCritical() << "[FileDB] 无法打开资源数据库:" << m_db.lastError().text();
        return false;
    }

    QSqlQuery q(m_db);
    q.exec("PRAGMA journal_mode = WAL;");
    q.exec("PRAGMA synchronous = NORMAL;");

    if (!createTables()) return false;

    m_isInitialized = true;
    return true;
}

bool Database::createTables() {
    QSqlQuery q(m_db);
    
    // 按照用户需求 SQL Schema
    // 1. folders 表
    if (!q.exec(R"(
        CREATE TABLE IF NOT EXISTS folders (
            path        TEXT PRIMARY KEY,
            rating      INTEGER DEFAULT 0,
            color       TEXT    DEFAULT '',
            tags        TEXT    DEFAULT '',  -- JSON 数组字符串
            pinned      INTEGER DEFAULT 0,
            sort_by     TEXT    DEFAULT 'name',
            sort_order  TEXT    DEFAULT 'asc',
            last_sync   REAL                 -- 对应 .am_meta.json 的 mtime
        )
    )")) return false;

    // 2. items 表
    if (!q.exec(R"(
        CREATE TABLE IF NOT EXISTS items (
            path        TEXT PRIMARY KEY,
            type        TEXT,                -- 'file' | 'folder'
            rating      INTEGER DEFAULT 0,
            color       TEXT    DEFAULT '',
            tags        TEXT    DEFAULT '',  -- JSON 数组字符串
            pinned      INTEGER DEFAULT 0,
            parent_path TEXT
        )
    )")) return false;

    // 3. tags 表
    if (!q.exec(R"(
        CREATE TABLE IF NOT EXISTS tags (
            tag         TEXT PRIMARY KEY,
            item_count  INTEGER DEFAULT 0
        )
    )")) return false;

    // 4. sync_state 表
    if (!q.exec(R"(
        CREATE TABLE IF NOT EXISTS sync_state (
            key         TEXT PRIMARY KEY,
            value       TEXT
        )
    )")) return false;

    // 5. 索引
    q.exec("CREATE INDEX IF NOT EXISTS idx_items_parent ON items(parent_path);");
    q.exec("CREATE INDEX IF NOT EXISTS idx_items_rating ON items(rating);");
    q.exec("CREATE INDEX IF NOT EXISTS idx_items_color  ON items(color);");
    q.exec("CREATE INDEX IF NOT EXISTS idx_items_tags   ON items(tags);");

    return true;
}

bool Database::updateFolderMeta(const QString& path, const QVariantMap& meta) {
    QMutexLocker locker(&m_mutex);
    QSqlQuery q(m_db);
    q.prepare(R"(
        INSERT OR REPLACE INTO folders (path, rating, color, tags, pinned, sort_by, sort_order, last_sync)
        VALUES (:path, :rating, :color, :tags, :pinned, :sort_by, :sort_order, :last_sync)
    )");
    q.bindValue(":path", path);
    q.bindValue(":rating", meta.value("rating", 0).toInt());
    q.bindValue(":color", meta.value("color", "").toString());
    q.bindValue(":tags", meta.value("tags", "").toString());
    q.bindValue(":pinned", meta.value("pinned", 0).toInt());
    q.bindValue(":sort_by", meta.value("sort_by", "name").toString());
    q.bindValue(":sort_order", meta.value("sort_order", "asc").toString());
    q.bindValue(":last_sync", meta.value("last_sync", 0).toDouble());
    return q.exec();
}

QVariantMap Database::getFolderMeta(const QString& path) {
    QMutexLocker locker(&m_mutex);
    QVariantMap map;
    QSqlQuery q(m_db);
    q.prepare("SELECT * FROM folders WHERE path = :path");
    q.bindValue(":path", path);
    if (q.exec() && q.next()) {
        QSqlRecord rec = q.record();
        for (int i = 0; i < rec.count(); ++i) map[rec.fieldName(i)] = q.value(i);
    }
    return map;
}

bool Database::deleteFolderMeta(const QString& path) {
    QMutexLocker locker(&m_mutex);
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM folders WHERE path = :path");
    q.bindValue(":path", path);
    return q.exec();
}

bool Database::updateItemMeta(const QString& path, const QVariantMap& meta) {
    QMutexLocker locker(&m_mutex);
    QSqlQuery q(m_db);
    q.prepare(R"(
        INSERT OR REPLACE INTO items (path, type, rating, color, tags, pinned, parent_path)
        VALUES (:path, :type, :rating, :color, :tags, :pinned, :parent_path)
    )");
    q.bindValue(":path", path);
    q.bindValue(":type", meta.value("type", "file").toString());
    q.bindValue(":rating", meta.value("rating", 0).toInt());
    q.bindValue(":color", meta.value("color", "").toString());
    q.bindValue(":tags", meta.value("tags", "").toString());
    q.bindValue(":pinned", meta.value("pinned", 0).toInt());
    q.bindValue(":parent_path", meta.value("parent_path", "").toString());
    return q.exec();
}

QVariantMap Database::getItemMeta(const QString& path) {
    QMutexLocker locker(&m_mutex);
    QVariantMap map;
    QSqlQuery q(m_db);
    q.prepare("SELECT * FROM items WHERE path = :path");
    q.bindValue(":path", path);
    if (q.exec() && q.next()) {
        QSqlRecord rec = q.record();
        for (int i = 0; i < rec.count(); ++i) map[rec.fieldName(i)] = q.value(i);
    }
    return map;
}

bool Database::deleteItemMeta(const QString& path) {
    QMutexLocker locker(&m_mutex);
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM items WHERE path = :path");
    q.bindValue(":path", path);
    return q.exec();
}

bool Database::deleteItemsInFolder(const QString& parentPath) {
    QMutexLocker locker(&m_mutex);
    QSqlQuery q(m_db);
    // [FIX] 2026-03-24 按照级联删除要求：使用 LIKE 匹配该路径下的所有子孙项
    QString pathPattern = parentPath;
    if (!pathPattern.endsWith("\\")) pathPattern += "\\";
    pathPattern += "%";
    
    q.prepare("DELETE FROM items WHERE path LIKE :pattern OR parent_path = :parent");
    q.bindValue(":pattern", pathPattern);
    q.bindValue(":parent", parentPath);
    return q.exec();
}

bool Database::updateSyncState(const QString& key, const QString& value) {
    QMutexLocker locker(&m_mutex);
    QSqlQuery q(m_db);
    q.prepare("INSERT OR REPLACE INTO sync_state (key, value) VALUES (:key, :value)");
    q.bindValue(":key", key);
    q.bindValue(":value", value);
    return q.exec();
}

QString Database::getSyncState(const QString& key) {
    QMutexLocker locker(&m_mutex);
    QSqlQuery q(m_db);
    q.prepare("SELECT value FROM sync_state WHERE key = :key");
    q.bindValue(":key", key);
    if (q.exec() && q.next()) return q.value(0).toString();
    return "";
}

bool Database::rebuildTagsTable() {
    QMutexLocker locker(&m_mutex);
    m_db.transaction();
    
    QSqlQuery q(m_db);
    q.exec("DELETE FROM tags;");
    
    QMap<QString, int> tagCounts;
    auto aggregateTags = [&](const QString& sql) {
        QSqlQuery fetch(m_db);
        if (fetch.exec(sql)) {
            while (fetch.next()) {
                QString tagsJson = fetch.value(0).toString();
                if (tagsJson.isEmpty()) continue;
                
                QJsonDocument doc = QJsonDocument::fromJson(tagsJson.toUtf8());
                if (doc.isArray()) {
                    QJsonArray arr = doc.array();
                    for (auto t : arr) {
                        QString tag = t.toString().trimmed();
                        if (!tag.isEmpty()) tagCounts[tag]++;
                    }
                }
            }
        }
    };
    
    aggregateTags("SELECT tags FROM items WHERE tags != '' AND tags != '[]'");
    aggregateTags("SELECT tags FROM folders WHERE tags != '' AND tags != '[]'");
    
    for (auto it = tagCounts.begin(); it != tagCounts.end(); ++it) {
        q.prepare("INSERT INTO tags (tag, item_count) VALUES (:tag, :count)");
        q.bindValue(":tag", it.key());
        q.bindValue(":count", it.value());
        q.exec();
    }
    
    return m_db.commit();
}

QVariantMap Database::getPhysicalFilterStats(const QString& keyword) {
    QMutexLocker locker(&m_mutex);
    QVariantMap result;

    auto getCountMap = [&](const QString& sql) {
        QVariantMap map;
        QSqlQuery q(m_db);
        if (q.exec(sql)) {
            while (q.next()) map[q.value(0).toString()] = q.value(1).toInt();
        }
        return map;
    };

    result["stars"] = getCountMap("SELECT CAST(rating AS TEXT), COUNT(*) FROM items GROUP BY rating");
    result["colors"] = getCountMap("SELECT color, COUNT(*) FROM items WHERE color != '' GROUP BY color");
    result["types"] = getCountMap("SELECT type, COUNT(*) FROM items GROUP BY type");

    QVariantMap tagStats;
    QSqlQuery q(m_db);
    if (q.exec("SELECT tag, item_count FROM tags")) {
        while (q.next()) tagStats[q.value(0).toString()] = q.value(1).toInt();
    }
    result["tags"] = tagStats;

    return result;
}
