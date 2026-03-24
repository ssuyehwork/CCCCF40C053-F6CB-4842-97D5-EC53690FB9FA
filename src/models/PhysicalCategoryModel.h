#ifndef PHYSICALCATEGORYMODEL_H
#define PHYSICALCATEGORYMODEL_H

#include <QStandardItemModel>

class PhysicalCategoryModel : public QStandardItemModel {
    Q_OBJECT
public:
    enum Type { System, Tag, Both };
    enum Roles {
        TypeRole = Qt::UserRole,
        PathRole,
        ColorRole,
        NameRole,
        IsTagRole
    };

    explicit PhysicalCategoryModel(Type type, QObject* parent = nullptr);

public slots:
    void refresh();

private:
    Type m_type;
};

#endif // PHYSICALCATEGORYMODEL_H
