#ifndef FILEDATABASE_H
#define FILEDATABASE_H

#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QVariantMap>
#include <QMutex>
#include <QStringList>

class FileDatabase : public QObject {
    Q_OBJECT
public:
    static FileDatabase& instance();
    bool init(const QString& dbPath);

    // 文件夹元数据 CRUD
    bool updateFolderMeta(const QString& path, const QVariantMap& meta);
    QVariantMap getFolderMeta(const QString& path);
    bool deleteFolderMeta(const QString& path);

    // 文件/子项元数据 CRUD
    bool updateItemMeta(const QString& path, const QVariantMap& meta);
    QVariantMap getItemMeta(const QString& path);
    bool deleteItemMeta(const QString& path);
    bool deleteItemsInFolder(const QString& parentPath);

    // 标签统计与同步状态
    bool updateSyncState(const QString& key, const QString& value);
    QString getSyncState(const QString& key);
    bool rebuildTagsTable();

private:
    explicit FileDatabase(QObject* parent = nullptr);
    ~FileDatabase();
    bool createTables();

    QSqlDatabase m_db;
    QMutex m_mutex;
    bool m_isInitialized = false;
};

#endif // FILEDATABASE_H
