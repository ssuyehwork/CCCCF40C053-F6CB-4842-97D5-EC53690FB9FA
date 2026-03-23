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
    // 2026-03-24 按照用户要求，初始化 SQLite Schema
    q.exec("CREATE TABLE IF NOT EXISTS folders ("
           "path TEXT PRIMARY KEY, rating INTEGER, color TEXT, tags TEXT, pinned INTEGER, last_sync REAL)");
    q.exec("CREATE TABLE IF NOT EXISTS items ("
           "path TEXT PRIMARY KEY, type TEXT, rating INTEGER, color TEXT, tags TEXT, pinned INTEGER, parent_path TEXT)");

    return true;
}

void FileDatabase::close() {
    m_db.close();
}
