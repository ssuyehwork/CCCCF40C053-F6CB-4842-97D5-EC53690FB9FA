#ifndef FILE_DATABASE_H
#define FILE_DATABASE_H

#include <string>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>

class FileDatabase {
public:
    static FileDatabase& instance();
    bool init(const QString& dbPath);
    void close();

private:
    FileDatabase() = default;
    QSqlDatabase m_db;
};

#endif // FILE_DATABASE_H
