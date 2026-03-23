#include "Database.h"
#include <QDebug>

FileDatabase& FileDatabase::instance() {
    static FileDatabase inst;
    return inst;
}

bool FileDatabase::init(const QString& dbPath) {
    m_db = QSqlDatabase::addDatabase("QSQLITE", "file_index_db");
    m_db.setDatabaseName(dbPath);
    if (!m_db.open()) return false;

    QSqlQuery q(m_db);
    // 2026-03-24 按照用户要求：初始化文件索引核心 Schema
    // 文件夹元数据表
    q.exec(R"(
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

    // 文件与子文件夹元数据表
    q.exec(R"(
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

    // 标签聚合索引表
    q.exec(R"(
        CREATE TABLE IF NOT EXISTS tags (
            tag         TEXT PRIMARY KEY,
            item_count  INTEGER DEFAULT 0
        )
    )");

    // 文件虚拟分类表 (独立于笔记分类)
    q.exec(R"(
        CREATE TABLE IF NOT EXISTS file_categories (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            name        TEXT NOT NULL,
            parent_id   INTEGER,
            color       TEXT    DEFAULT '#808080',
            sort_order  INTEGER DEFAULT 0,
            is_pinned   INTEGER DEFAULT 0,
            updated_at  DATETIME DEFAULT CURRENT_TIMESTAMP
        )
    )");

    // 文件与分类的多对多关联表
    q.exec(R"(
        CREATE TABLE IF NOT EXISTS item_categories (
            item_path   TEXT,
            category_id INTEGER,
            PRIMARY KEY (item_path, category_id),
            FOREIGN KEY (category_id) REFERENCES file_categories(id) ON DELETE CASCADE
        )
    )");

    // 同步状态表
    q.exec(R"(
        CREATE TABLE IF NOT EXISTS sync_state (
            key         TEXT PRIMARY KEY,
            value       TEXT
        )
    )");

    // 创建索引以加速查询
    q.exec("CREATE INDEX IF NOT EXISTS idx_items_parent ON items(parent_path)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_items_rating ON items(rating)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_items_color  ON items(color)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_items_tags   ON items(tags)");

    return true;
}

void FileDatabase::close() {
    m_db.close();
}
