#pragma once

#include <QStandardItemModel>
#include <QSet>

namespace ArcMeta {

class CategoryModel : public QStandardItemModel {
    Q_OBJECT
public:
    enum Type { System, User, Both };
    enum Roles {
        TypeRole = Qt::UserRole,
        IdRole,
        ColorRole,
        NameRole,
        PinnedRole,
        PathRole,
        EncryptedRole,
        EncryptHintRole
    };
    explicit CategoryModel(Type type, QObject* parent = nullptr);

    void setUnlockedIds(const QSet<int>& ids);

public slots:
    void refresh();

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex& index, const QVariant& val, int role = Qt::EditRole) override;

    Qt::DropActions supportedDropActions() const override;
    bool dropMimeData(const QMimeData* mimeData, Qt::DropAction action, int row, int column, const QModelIndex& parent) override;

private:
    Type m_type;
    QSet<int> m_unlockedIds;
};

} // namespace ArcMeta
