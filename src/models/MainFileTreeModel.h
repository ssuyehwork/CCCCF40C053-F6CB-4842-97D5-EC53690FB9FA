#ifndef MAINFILETREEMODEL_H
#define MAINFILETREEMODEL_H

#include <QStandardItemModel>
#include <QVariantMap>

class MainFileTreeModel : public QStandardItemModel {
    Q_OBJECT
public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        ContentRole,
        TypeRole,
        ColorRole,
        NameRole,
        PinnedRole,
        FavoriteRole,
        RatingRole,
        TagsRole,
        TimeRole,
        RemarkRole,
        BlobRole,
        CategoryIdRole,
        CategoryNameRole
    };

    explicit MainFileTreeModel(QObject* parent = nullptr);

    void setNotes(const QList<QVariantMap>& notes);
    void clearNotes();

    // 2026-03-23 [NEW] 按照用户要求：支持递归构建层级结构
    void rebuildTree(const QString& filterType, const QVariant& filterValue, const QString& keyword, const QVariantMap& criteria);

private:
    void addNoteToItem(QStandardItem* parent, const QVariantMap& note);
};

#endif // MAINFILETREEMODEL_H
