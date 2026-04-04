#ifndef CATEGORYMODEL_H
#define CATEGORYMODEL_H

#include <QStandardItemModel>
#include <QSet>

// 2026-04-04 按照用户要求：从笔记管理转型为超级资源管理器，对接 ArcMeta 核心逻辑
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
        PathRole,           // [NEW] 资源管理：物理路径角色
        EncryptedRole,      // [NEW] 资源管理：加密状态
        EncryptHintRole,    // [NEW] 资源管理：加密提示
        HasPasswordRole     // 兼容旧版 UI 逻辑
    };

    explicit CategoryModel(Type type, QObject* parent = nullptr);

    void setUnlockedIds(const QSet<int>& ids);

public slots:
    void refresh();
    void updateExtensionIcons();
    void setDraggingId(int id) { m_draggingId = id; }
    int draggingId() const { return m_draggingId; }

    // 编辑与展示逻辑
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;

    // D&D support
    Qt::DropActions supportedDropActions() const override;
    bool dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column, const QModelIndex& parent) override;

private:
    void syncOrders(const QModelIndex& parent);
    Type m_type;
    int m_draggingId = -1;
    QSet<int> m_unlockedIds;
};

} // namespace ArcMeta

#endif // CATEGORYMODEL_H
