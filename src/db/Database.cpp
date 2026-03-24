#include "Database.h"
#include <QDebug>
#include <QJsonDocument>
#include <QJsonArray>
#include <QSet>
#include <QDateTime>

Database& Database::instance() {
    static Database inst;
    return inst;
}

Database::Database(QObject* parent) : QObject(parent) {}

Database::~Database() {
    // SQLiteCpp 自动关闭
}

bool Database::init(const QString& dbPath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    try {
        m_db = std::make_unique<SQLite::Database>(dbPath.toStdString(), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        m_db->exec("PRAGMA journal_mode = WAL;");
        m_db->exec("PRAGMA synchronous = NORMAL;");
        
        if (!createTables()) return false;
        
        m_isInitialized = true;
        return true;
    } catch (std::exception& e) {
        qCritical() << "[FileDB] 无法初始化资源数据库:" << e.what();
        return false;
    }
}

bool Database::createTables() {
    try {
        m_db->exec(R"(
            CREATE TABLE IF NOT EXISTS folders (
                path        TEXT PRIMARY KEY,
                rating      INTEGER DEFAULT 0,
                color       TEXT    DEFAULT '',
                tags        TEXT    DEFAULT '',
                pinned      INTEGER DEFAULT 0,
                sort_by     TEXT    DEFAULT 'name',
                sort_order  TEXT    DEFAULT 'asc',
                last_sync   REAL
            )
        )");

        m_db->exec(R"(
            CREATE TABLE IF NOT EXISTS items (
                path        TEXT PRIMARY KEY,
                type        TEXT,
                rating      INTEGER DEFAULT 0,
                color       TEXT    DEFAULT '',
                tags        TEXT    DEFAULT '',
                pinned      INTEGER DEFAULT 0,
                parent_path TEXT
            )
        )");

        m_db->exec(R"(
            CREATE TABLE IF NOT EXISTS tags (
                tag         TEXT PRIMARY KEY,
                item_count  INTEGER DEFAULT 0
            )
        )");

        m_db->exec(R"(
            CREATE TABLE IF NOT EXISTS sync_state (
                key         TEXT PRIMARY KEY,
                value       TEXT
            )
        )");

        m_db->exec("CREATE INDEX IF NOT EXISTS idx_items_parent ON items(parent_path);");
        m_db->exec("CREATE INDEX IF NOT EXISTS idx_items_rating ON items(rating);");
        return true;
    } catch (std::exception& e) {
        qCritical() << "[FileDB] 创建表失败:" << e.what();
        return false;
    }
}

bool Database::updateFolderMeta(const QString& path, const QVariantMap& meta) {
    std::lock_guard<std::mutex> lock(m_mutex);
    try {
        SQLite::Statement query(*m_db, R"(
            INSERT OR REPLACE INTO folders (path, rating, color, tags, pinned, sort_by, sort_order, last_sync)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?)
        )");
        query.bind(1, path.toStdString());
        query.bind(2, meta.value("rating", 0).toInt());
        query.bind(3, meta.value("color", "").toString().toStdString());
        query.bind(4, meta.value("tags", "").toString().toStdString());
        query.bind(5, meta.value("pinned", 0).toInt());
        query.bind(6, meta.value("sort_by", "name").toString().toStdString());
        query.bind(7, meta.value("sort_order", "asc").toString().toStdString());
        query.bind(8, meta.value("last_sync", 0).toDouble());
        return query.exec() > 0;
    } catch (...) { return false; }
}

QVariantMap Database::getFolderMeta(const QString& path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    QVariantMap map;
    try {
        SQLite::Statement query(*m_db, "SELECT * FROM folders WHERE path = ?");
        query.bind(1, path.toStdString());
        if (query.executeStep()) {
            map["path"] = QString::fromStdString(query.getColumn("path").getText());
            map["rating"] = query.getColumn("rating").getInt();
            map["color"] = QString::fromStdString(query.getColumn("color").getText());
            map["tags"] = QString::fromStdString(query.getColumn("tags").getText());
            map["pinned"] = query.getColumn("pinned").getInt();
            map["sort_by"] = QString::fromStdString(query.getColumn("sort_by").getText());
            map["sort_order"] = QString::fromStdString(query.getColumn("sort_order").getText());
        }
    } catch (...) {}
    return map;
}

bool Database::deleteFolderMeta(const QString& path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    try {
        SQLite::Statement query(*m_db, "DELETE FROM folders WHERE path = ?");
        query.bind(1, path.toStdString());
        return query.exec() > 0;
    } catch (...) { return false; }
}

bool Database::updateItemMeta(const QString& path, const QVariantMap& meta) {
    std::lock_guard<std::mutex> lock(m_mutex);
    try {
        SQLite::Statement query(*m_db, R"(
            INSERT OR REPLACE INTO items (path, type, rating, color, tags, pinned, parent_path)
            VALUES (?, ?, ?, ?, ?, ?, ?)
        )");
        query.bind(1, path.toStdString());
        query.bind(2, meta.value("type", "file").toString().toStdString());
        query.bind(3, meta.value("rating", 0).toInt());
        query.bind(4, meta.value("color", "").toString().toStdString());
        query.bind(5, meta.value("tags", "").toString().toStdString());
        query.bind(6, meta.value("pinned", 0).toInt());
        query.bind(7, meta.value("parent_path", "").toString().toStdString());
        return query.exec() > 0;
    } catch (...) { return false; }
}

QVariantMap Database::getItemMeta(const QString& path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    QVariantMap map;
    try {
        SQLite::Statement query(*m_db, "SELECT * FROM items WHERE path = ?");
        query.bind(1, path.toStdString());
        if (query.executeStep()) {
            map["path"] = QString::fromStdString(query.getColumn("path").getText());
            map["type"] = QString::fromStdString(query.getColumn("type").getText());
            map["rating"] = query.getColumn("rating").getInt();
            map["color"] = QString::fromStdString(query.getColumn("color").getText());
            map["tags"] = QString::fromStdString(query.getColumn("tags").getText());
            map["pinned"] = query.getColumn("pinned").getInt();
        }
    } catch (...) {}
    return map;
}

bool Database::deleteItemMeta(const QString& path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    try {
        SQLite::Statement query(*m_db, "DELETE FROM items WHERE path = ?");
        query.bind(1, path.toStdString());
        return query.exec() > 0;
    } catch (...) { return false; }
}

bool Database::deleteItemsInFolder(const QString& parentPath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    try {
        QString pattern = parentPath;
        if (!pattern.endsWith("\\")) pattern += "\\";
        pattern += "%";
        
        SQLite::Statement query(*m_db, "DELETE FROM items WHERE path LIKE ? OR parent_path = ?");
        query.bind(1, pattern.toStdString());
        query.bind(2, parentPath.toStdString());
        return query.exec() > 0;
    } catch (...) { return false; }
}

bool Database::updateSyncState(const QString& key, const QString& value) {
    std::lock_guard<std::mutex> lock(m_mutex);
    try {
        SQLite::Statement query(*m_db, "INSERT OR REPLACE INTO sync_state (key, value) VALUES (?, ?)");
        query.bind(1, key.toStdString());
        query.bind(2, value.toStdString());
        return query.exec() > 0;
    } catch (...) { return false; }
}

QString Database::getSyncState(const QString& key) {
    std::lock_guard<std::mutex> lock(m_mutex);
    try {
        SQLite::Statement query(*m_db, "SELECT value FROM sync_state WHERE key = ?");
        query.bind(1, key.toStdString());
        if (query.executeStep()) return QString::fromStdString(query.getColumn(0).getText());
    } catch (...) {}
    return "";
}

bool Database::rebuildTagsTable() {
    std::lock_guard<std::mutex> lock(m_mutex);
    try {
        SQLite::Transaction transaction(*m_db);
        m_db->exec("DELETE FROM tags");
        
        std::map<std::string, int> tagCounts;
        auto agg = [&](const char* sql) {
            SQLite::Statement query(*m_db, sql);
            while (query.executeStep()) {
                std::string tagsJson = query.getColumn(0).getText();
                QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(tagsJson));
                if (doc.isArray()) {
                    for (auto t : doc.array()) {
                        std::string tag = t.toString().toStdString();
                        if (!tag.empty()) tagCounts[tag]++;
                    }
                }
            }
        };

        agg("SELECT tags FROM items WHERE tags != ''");
        agg("SELECT tags FROM folders WHERE tags != ''");

        for (const auto& [tag, count] : tagCounts) {
            SQLite::Statement ins(*m_db, "INSERT INTO tags (tag, item_count) VALUES (?, ?)");
            ins.bind(1, tag);
            ins.bind(2, count);
            ins.exec();
        }
        transaction.commit();
        return true;
    } catch (...) { return false; }
}

QVariantMap Database::getPhysicalFilterStats(const QString& keyword) {
    std::lock_guard<std::mutex> lock(m_mutex);
    QVariantMap result;
    try {
        auto getMap = [&](const char* sql) {
            QVariantMap m;
            SQLite::Statement q(*m_db, sql);
            while (q.executeStep()) m[QString::fromStdString(q.getColumn(0).getText())] = q.getColumn(1).getInt();
            return m;
        };

        result["stars"] = getMap("SELECT CAST(rating AS TEXT), COUNT(*) FROM items GROUP BY rating");
        result["colors"] = getMap("SELECT color, COUNT(*) FROM items WHERE color != '' GROUP BY color");
        result["types"] = getMap("SELECT type, COUNT(*) FROM items GROUP BY type");
        result["tags"] = getMap("SELECT tag, item_count FROM tags");
    } catch (...) {}
    return result;
}
