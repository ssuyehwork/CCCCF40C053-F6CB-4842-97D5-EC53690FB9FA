#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H

#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDateTime>
#include <QVariant>
#include <QVariantList>
#include <QRecursiveMutex>
#include <QStringList>
#include <QSet>
#include <QMutex>

class DatabaseManager : public QObject {
    Q_OBJECT
public:
    static DatabaseManager& instance();

    bool init(const QString& dbPath = "rapid_notes.db");
    void closeAndPack();
    
    // 核心 CRUD 操作
    bool addNote(const QString& title, const QString& content, const QStringList& tags = QStringList(), 
                 const QString& color = "", int categoryId = -1, 
                 const QString& itemType = "text", const QByteArray& dataBlob = QByteArray(),
                 const QString& sourceApp = "", const QString& sourceTitle = "");
    bool updateNote(int id, const QString& title, const QString& content, const QStringList& tags, 
                    const QString& color = "", int categoryId = -1);
    bool deleteNote(int id);
    bool deleteNotesBatch(const QList<int>& ids);
    bool updateNoteState(int id, const QString& column, const QVariant& value);
    bool updateNoteStateBatch(const QList<int>& ids, const QString& column, const QVariant& value);
    // 批量软删除 (放入回收站)
    bool softDeleteNotes(const QList<int>& ids);
    bool toggleNoteState(int id, const QString& column);
    bool moveNoteToCategory(int noteId, int catId);
    bool moveNotesToCategory(const QList<int>& noteIds, int catId);
    bool recordAccess(int id);

    // 分类管理
    enum MoveDirection { Up, Down, Top, Bottom };
    int addCategory(const QString& name, int parentId = -1, const QString& color = "");
    bool renameCategory(int id, const QString& name);
    bool setCategoryColor(int id, const QString& color);
    bool deleteCategory(int id);
    bool moveCategory(int id, MoveDirection direction);
    bool reorderCategories(int parentId, bool ascending);
    bool reorderAllCategories(bool ascending);
    bool updateCategoryOrder(int parentId, const QList<int>& categoryIds);
    QList<QVariantMap> getAllCategories();
    bool emptyTrash();
    bool restoreAllFromTrash();

    // 分类密码保护
    bool setCategoryPassword(int id, const QString& password, const QString& hint);
    bool removeCategoryPassword(int id);
    bool verifyCategoryPassword(int id, const QString& password);
    bool isCategoryLocked(int id);
    void lockCategory(int id);
    void unlockCategory(int id);

    // 预设标签
    bool setCategoryPresetTags(int catId, const QString& tags);
    QString getCategoryPresetTags(int catId);

    // 标签管理
    bool addTagsToNote(int noteId, const QStringList& tags);
    bool removeTagFromNote(int noteId, const QString& tag);
    bool renameTagGlobally(const QString& oldName, const QString& newName);
    bool deleteTagGlobally(const QString& tagName);

    // 搜索与查询
    QList<QVariantMap> searchNotes(const QString& keyword, const QString& filterType = "all", const QVariant& filterValue = -1, int page = -1, int pageSize = 20, const QVariantMap& criteria = QVariantMap());
    int getNotesCount(const QString& keyword, const QString& filterType = "all", const QVariant& filterValue = -1, const QVariantMap& criteria = QVariantMap());
    QList<QVariantMap> getAllNotes();
    QStringList getAllTags();
    QList<QVariantMap> getRecentTagsWithCounts(int limit = 20);
    QVariantMap getNoteById(int id);

    // 统计
    QVariantMap getCounts();
    QVariantMap getFilterStats(const QString& keyword = "", const QString& filterType = "all", const QVariant& filterValue = -1, const QVariantMap& criteria = QVariantMap());

    // 试用期与使用次数管理
    QVariantMap getTrialStatus();
    void incrementUsageCount();

    // 异步操作
    void addNoteAsync(const QString& title, const QString& content, const QStringList& tags = QStringList(),
                      const QString& color = "", int categoryId = -1,
                      const QString& itemType = "text", const QByteArray& dataBlob = QByteArray(),
                      const QString& sourceApp = "", const QString& sourceTitle = "");

signals:
    // 【修改】现在信号携带具体数据，实现增量更新
    void noteAdded(const QVariantMap& note);
    void noteUpdated(); // 用于普通刷新
    void categoriesChanged();

private:
    DatabaseManager(QObject* parent = nullptr);
    ~DatabaseManager();
    DatabaseManager(const DatabaseManager&) = delete;
    DatabaseManager& operator=(const DatabaseManager&) = delete;

    bool createTables();
    void syncFts(int id, const QString& title, const QString& content);
    void removeFts(int id);
    QString stripHtml(const QString& html);
    void applySecurityFilter(QString& whereClause, QVariantList& params, const QString& filterType);
    
    QSqlDatabase m_db;
    QString m_dbPath;      // 当前正在使用的内核路径 (.notes_core)
    QString m_realDbPath;  // 最终持久化的外壳路径 (notes.db)
    QRecursiveMutex m_mutex;

    QSet<int> m_unlockedCategories; // 仅存储当前会话已解锁的分类 ID

    // 标签剪贴板 (全局静态)
    static QStringList s_tagClipboard;
    static QMutex s_tagClipboardMutex;

public:
    static void setTagClipboard(const QStringList& tags);
    static QStringList getTagClipboard();
};

#endif // DATABASEMANAGER_H