#ifndef CATEGORYMODEL_H
#define CATEGORYMODEL_H

#include <QStandardItemModel>

class CategoryModel : public QStandardItemModel {
    Q_OBJECT
public:
    enum Type { System, User, Both };
    enum Roles {
        TypeRole = Qt::UserRole,
        IdRole,
        ColorRole,
        NameRole
    };
    explicit CategoryModel(Type type, QObject* parent = nullptr);
    void refresh();
    void setDraggingId(int id) { m_draggingId = id; }

    // D&D support
    Qt::DropActions supportedDropActions() const override;
    bool dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column, const QModelIndex& parent) override;

private:
    void syncOrders(const QModelIndex& parent);
    Type m_type;
    int m_draggingId = -1;
};

#endif // CATEGORYMODEL_H
