#include "Database.h"
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QDir>
#include <QStandardPaths>
#include <QThread>

namespace ArcMeta {

struct Database::Impl {
    QSqlDatabase db;
    std::wstring dbPath;
};

Database& Database::instance() {
    static Database inst;
    return inst;
}

Database::Database() : m_impl(std::make_unique<Impl>()) {}
Database::~Database() = default;

bool Database::init(const std::wstring& dbPath) {
    m_impl->dbPath = dbPath;
    m_impl->db = QSqlDatabase::addDatabase("QSQLITE");
    m_impl->db.setDatabaseName(QString::fromStdWString(dbPath));

    if (!m_impl->db.open()) return false;

    // 关键红线：防死锁配置
    QSqlQuery query(m_impl->db);
    query.exec("PRAGMA journal_mode=WAL;");
    query.exec("PRAGMA synchronous=NORMAL;");
    query.exec("PRAGMA busy_timeout=5000;");

    createTables();
    createIndexes();
    return true;
}

std::wstring Database::getDbPath() const {
    return m_impl->dbPath;
}

QSqlDatabase Database::getThreadDatabase() {
    QString connectionName = QString("conn_%1").arg((quintptr)QThread::currentThreadId());

    // 如果该线程已经建立过连接，直接返回现有连接
    if (QSqlDatabase::contains(connectionName)) {
        return QSqlDatabase::database(connectionName);
    }

    // 否则，为新线程建立独立连接
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
    db.setDatabaseName(QString::fromStdWString(m_impl->dbPath));
    if (db.open()) {
        // 对新连接同样应用 WAL 优化，防止写入锁死
        QSqlQuery query(db);
        query.exec("PRAGMA journal_mode=WAL;");
        query.exec("PRAGMA synchronous=NORMAL;");
        query.exec("PRAGMA busy_timeout=5000;");
    }
    return db;
}

void Database::createTables() {
    QSqlQuery q(m_impl->db);
    q.exec("CREATE TABLE IF NOT EXISTS folders (path TEXT PRIMARY KEY, rating INTEGER DEFAULT 0, color TEXT DEFAULT '', tags TEXT DEFAULT '', pinned INTEGER DEFAULT 0, note TEXT DEFAULT '', sort_by TEXT DEFAULT 'name', sort_order TEXT DEFAULT 'asc', last_sync REAL)");
    q.exec("CREATE TABLE IF NOT EXISTS items (volume TEXT NOT NULL, frn TEXT NOT NULL, path TEXT, parent_path TEXT, type TEXT, rating INTEGER DEFAULT 0, color TEXT DEFAULT '', tags TEXT DEFAULT '', pinned INTEGER DEFAULT 0, note TEXT DEFAULT '', encrypted INTEGER DEFAULT 0, encrypt_salt TEXT DEFAULT '', encrypt_iv TEXT DEFAULT '', encrypt_verify_hash TEXT DEFAULT '', original_name TEXT DEFAULT '', ctime REAL DEFAULT 0, mtime REAL DEFAULT 0, atime REAL DEFAULT 0, deleted INTEGER DEFAULT 0, PRIMARY KEY (volume, frn))");
    q.exec("CREATE TABLE IF NOT EXISTS tags (tag TEXT PRIMARY KEY, item_count INTEGER DEFAULT 0)");
    q.exec("CREATE TABLE IF NOT EXISTS favorites (path TEXT PRIMARY KEY, type TEXT, name TEXT, sort_order INTEGER DEFAULT 0, added_at REAL)");
    q.exec("CREATE TABLE IF NOT EXISTS categories (id INTEGER PRIMARY KEY AUTOINCREMENT, parent_id INTEGER DEFAULT 0, name TEXT NOT NULL, color TEXT DEFAULT '', preset_tags TEXT DEFAULT '', sort_order INTEGER DEFAULT 0, pinned INTEGER DEFAULT 0, encrypted INTEGER DEFAULT 0, encrypt_salt TEXT DEFAULT '', encrypt_iv TEXT DEFAULT '', encrypt_verify_hash TEXT DEFAULT '', encrypt_hint TEXT DEFAULT '', created_at REAL)");
    q.exec("CREATE TABLE IF NOT EXISTS category_items (category_id INTEGER, item_path TEXT, added_at REAL, PRIMARY KEY (category_id, item_path))");
    q.exec("CREATE TABLE IF NOT EXISTS sync_state (key TEXT PRIMARY KEY, value TEXT)");

    // 紧急补丁：由于 CREATE TABLE IF NOT EXISTS 不会修改已存在的表，
    // 显式检查并添加缺失的 encrypt_hint 字段，防止查询挂掉。
    q.exec("ALTER TABLE categories ADD COLUMN encrypt_hint TEXT DEFAULT ''");
}

void Database::createIndexes() {
    QSqlQuery q(m_impl->db);
    q.exec("CREATE INDEX IF NOT EXISTS idx_items_path ON items(path)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_items_parent ON items(parent_path)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_items_deleted ON items(deleted)");
}

} // namespace ArcMeta
