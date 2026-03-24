#ifndef DATABASE_H
#define DATABASE_H

#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QVariantMap>
#include <QMutex>
#include <QStringList>

class Database : public QObject {
    Q_OBJECT
public:
    static Database& instance();
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

    // 2026-03-24 [NEW] 物理筛选统计
    QVariantMap getPhysicalFilterStats(const QString& keyword = "");

private:
    explicit Database(QObject* parent = nullptr);
    ~Database();
    bool createTables();

    QSqlDatabase m_db;
    QMutex m_mutex;
    bool m_isInitialized = false;
};

#endif // DATABASE_H
