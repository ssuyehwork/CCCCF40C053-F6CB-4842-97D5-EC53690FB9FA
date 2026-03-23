#ifndef FILECATEGORYMODEL_H
#define FILECATEGORYMODEL_H

#include <QStandardItemModel>

class FileCategoryModel : public QStandardItemModel {
    Q_OBJECT
public:
    enum Type { System, User };
    enum Roles {
        TypeRole = Qt::UserRole,
        IdRole,
        ColorRole,
        NameRole,
        PinnedRole
    };
    explicit FileCategoryModel(Type type, QObject* parent = nullptr);

public slots:
    void refresh();

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;

    Qt::DropActions supportedDropActions() const override;
    bool dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column, const QModelIndex& parent) override;

private:
    Type m_type;
};

#endif // FILECATEGORYMODEL_H
