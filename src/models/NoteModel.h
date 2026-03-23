#ifndef NOTEMODEL_H
#define NOTEMODEL_H

#include <QAbstractItemModel>
#include <QVariantMap>
#include <QList>
#include <QMimeData>

class NoteModel : public QAbstractItemModel {
    Q_OBJECT
public:
    enum NoteRoles {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        ContentRole,
        TagsRole,
        TimeRole,
        PinnedRole,
        LockedRole,
        FavoriteRole,
        TypeRole,
        RatingRole,
        CategoryIdRole,
        CategoryNameRole,
        ColorRole,
        SourceAppRole,
        SourceTitleRole,
        BlobRole,
        RemarkRole,
        PlainContentRole,
        IsCategoryRole // 新增角色：用于区分是分类节点还是笔记节点
    };

    explicit NoteModel(QObject* parent = nullptr);
    ~NoteModel();

    QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override { return 1; }
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QModelIndexList& indexes) const override;

    void setNotes(const QList<QVariantMap>& notes);
    void prependNote(const QVariantMap& note);
    void updateCategoryMap();

    // 辅助函数
    QModelIndex indexForNode(Node* node) const;
    int countNotes(Node* node) const;

private:
    struct Node {
        bool isCategory;
        QVariantMap data;
        int id;
        Node* parentNode = nullptr;
        QList<Node*> children;
        ~Node() { qDeleteAll(children); }
    };

    Node* m_rootNode;
    QMap<int, QString> m_categoryMap;
    QMap<int, Node*> m_categoryNodeMap; // 分类 ID 到节点的快速映射
    mutable QMap<int, QIcon> m_thumbnailCache;
    mutable QMap<int, QString> m_tooltipCache;
    mutable QMap<int, QString> m_plainContentCache;

    void clearNodes();
    void buildTree(const QList<QVariantMap>& notes);
};

#endif // NOTEMODEL_H
