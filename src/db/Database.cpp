#include "Database.h"
#include <QDebug>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QDir>
#include <QCoreApplication>

namespace db {

Database& Database::instance() {
    static Database inst;
    return inst;
}

Database::~Database() {
    close();
}

bool Database::init(const QString& dbPath) {
    QMutexLocker locker(&m_mutex);
    
    // 确保目录存在
    QFileInfo dbInfo(dbPath);
    QDir().mkpath(dbInfo.absolutePath());

    m_db = QSqlDatabase::addDatabase("QSQLITE", "FileManagerConnection");
    m_db.setDatabaseName(dbPath);

    if (!m_db.open()) {
        qWarning() << "Failed to open database:" << m_db.lastError().text();
        return false;
    }

    // 启用高性能设置
    QSqlQuery query(m_db);
    query.exec("PRAGMA journal_mode = WAL;");
    query.exec("PRAGMA synchronous = NORMAL;");
    query.exec("PRAGMA foreign_keys = ON;");

    return createSchema();
}

void Database::close() {
    QMutexLocker locker(&m_mutex);
    if (m_db.isOpen()) {
        m_db.close();
    }
}

bool Database::beginTransaction() {
    return m_db.transaction();
}

bool Database::commit() {
    return m_db.commit();
}

bool Database::rollback() {
    return m_db.rollback();
}

bool Database::createSchema() {
    QSqlQuery query(m_db);

    // 1. 文件夹元数据表
    if (!query.exec(R"(
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
    )")) {
        qWarning() << "Failed to create folders table:" << query.lastError().text();
        return false;
    }

    // 2. 文件与子文件夹元数据表
    if (!query.exec(R"(
        CREATE TABLE IF NOT EXISTS items (
            path        TEXT PRIMARY KEY,
            type        TEXT,                -- 'file' | 'folder'
            rating      INTEGER DEFAULT 0,
            color       TEXT    DEFAULT '',
            tags        TEXT    DEFAULT '',  -- JSON 数组字符串
            pinned      INTEGER DEFAULT 0,
            parent_path TEXT
        )
    )")) {
        qWarning() << "Failed to create items table:" << query.lastError().text();
        return false;
    }

    // 3. 标签聚合索引表
    if (!query.exec(R"(
        CREATE TABLE IF NOT EXISTS tags (
            tag         TEXT PRIMARY KEY,
            item_count  INTEGER DEFAULT 0
        )
    )")) {
        qWarning() << "Failed to create tags table:" << query.lastError().text();
        return false;
    }

    // 4. 同步状态表
    if (!query.exec(R"(
        CREATE TABLE IF NOT EXISTS sync_state (
            key         TEXT PRIMARY KEY,
            value       TEXT
        )
    )")) {
        qWarning() << "Failed to create sync_state table:" << query.lastError().text();
        return false;
    }

    // 5. 索引
    query.exec("CREATE INDEX IF NOT EXISTS idx_items_parent ON items(parent_path);");
    query.exec("CREATE INDEX IF NOT EXISTS idx_items_rating ON items(rating);");
    query.exec("CREATE INDEX IF NOT EXISTS idx_items_color  ON items(color);");
    query.exec("CREATE INDEX IF NOT EXISTS idx_items_tags   ON items(tags);");

    return true;
}

} // namespace db
