#ifndef DATABASE_H
#define DATABASE_H

#include <QString>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QVariantList>
#include <QMutex>

namespace db {

class Database {
public:
    static Database& instance();

    bool init(const QString& dbPath);
    void close();

    QSqlDatabase& getDb() { return m_db; }

    // 基础事务支持
    bool beginTransaction();
    bool commit();
    bool rollback();

private:
    Database() = default;
    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    bool createSchema();

    QSqlDatabase m_db;
    QMutex m_mutex;
};

} // namespace db

#endif // DATABASE_H
