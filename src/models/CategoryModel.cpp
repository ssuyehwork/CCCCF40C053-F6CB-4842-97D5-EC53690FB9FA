#include "CategoryModel.h"
#include "../core/DatabaseManager.h"
#include "../ui/IconHelper.h"
#include <QMimeData>
#include <QFont>
#include <QTimer>
#include <QSet>

CategoryModel::CategoryModel(Type type, QObject* parent) 
    : QStandardItemModel(parent), m_type(type) 
{
    refresh();
}

void CategoryModel::refresh() {
    clear();
    QStandardItem* root = invisibleRootItem();
    QVariantMap counts = DatabaseManager::instance().getCounts();

    if (m_type == System || m_type == Both) {
        auto addSystemItem = [&](const QString& name, const QString& type, const QString& icon, const QString& color = "#aaaaaa") {
            int count = counts.value(type, 0).toInt();
            QString display = QString("%1 (%2)").arg(name).arg(count);
            QStandardItem* item = new QStandardItem(display);
            item->setData(type, TypeRole);
            item->setData(name, NameRole);
            item->setData(color, ColorRole); // 设置颜色角色
            item->setEditable(false); // 系统项目不可重命名
            item->setIcon(IconHelper::getIcon(icon, color));
            root->appendRow(item);
        };

        addSystemItem("全部数据", "all", "all_data", "#3498db");
        addSystemItem("今日数据", "today", "today", "#2ecc71");
        addSystemItem("昨日数据", "yesterday", "today", "#f39c12"); // 使用橙色区分
        addSystemItem("最近访问", "recently_visited", "clock", "#9b59b6");
        addSystemItem("未分类", "uncategorized", "uncategorized", "#e67e22");
        addSystemItem("未标签", "untagged", "untagged", "#95a5a6");
        addSystemItem("书签", "bookmark", "bookmark", "#e74c3c");
        addSystemItem("回收站", "trash", "trash", "#7f8c8d");
    }
    
    if (m_type == User || m_type == Both) {
        // 用户分类：不再使用“我的分区”节点，直接挂载到 root
        auto categories = DatabaseManager::instance().getAllCategories();
        QMap<int, QStandardItem*> itemMap;

        for (const auto& cat : categories) {
            int id = cat["id"].toInt();
            int count = counts.value("cat_" + QString::number(id), 0).toInt();
            QString name = cat["name"].toString();
            QString display = QString("%1 (%2)").arg(name).arg(count);
            QStandardItem* item = new QStandardItem(display);
            item->setData("category", TypeRole);
            item->setData(id, IdRole);
            item->setData(cat["color"], ColorRole);
            item->setData(name, NameRole);
            item->setFlags(item->flags() | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
            
            if (DatabaseManager::instance().isCategoryLocked(id)) {
                item->setIcon(IconHelper::getIcon("lock", "#aaaaaa"));
            } else {
                item->setIcon(IconHelper::getIcon("circle_filled", cat["color"].toString()));
            }
            itemMap[cat["id"].toInt()] = item;
        }

        QSet<int> addedToTree; // 防止重复添加或循环引用导致 QStandardItemModel 崩溃
        for (const auto& cat : categories) {
            int id = cat["id"].toInt();
            int parentId = cat["parent_id"].toInt();

            // 环路自愈：如果父 ID 等于自身，视为顶级分类
            if (parentId == id) parentId = -1;

            if (parentId > 0 && itemMap.contains(parentId)) {
                if (!addedToTree.contains(id)) {
                    itemMap[parentId]->appendRow(itemMap[id]);
                    addedToTree.insert(id);
                }
            } else {
                if (!addedToTree.contains(id)) {
                    root->appendRow(itemMap[id]);
                    addedToTree.insert(id);
                }
            }
        }
    }
}

Qt::DropActions CategoryModel::supportedDropActions() const {
    return Qt::MoveAction;
}

bool CategoryModel::dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column, const QModelIndex& parent) {
    QModelIndex actualParent = parent;
    
    // 核心修改：不再重定向到“我的分区”，如果释放到根部或非分类项，默认接受为顶级分类
    if (m_draggingId != -1) {
        if (actualParent.isValid()) {
            QStandardItem* targetItem = itemFromIndex(actualParent);
            QString type = targetItem->data(TypeRole).toString();
            if (type != "category") {
                actualParent = QModelIndex(); // 重定向到根节点
            }
        }
    }

    // 再次检查合法性：仅允许释放到分类项或根部
    if (actualParent.isValid()) {
        QStandardItem* parentItem = itemFromIndex(actualParent);
        QString type = parentItem->data(TypeRole).toString();
        if (type != "category") {
            return false;
        }
    }

    bool ok = QStandardItemModel::dropMimeData(data, action, row, column, actualParent);
    if (ok && action == Qt::MoveAction) {
        QPersistentModelIndex persistentParent = actualParent;
        QTimer::singleShot(0, this, [this, persistentParent]() {
            syncOrders(persistentParent);
        });
    } else {
        m_draggingId = -1; 
    }
    return ok;
}

void CategoryModel::syncOrders(const QModelIndex& parent) {
    QStandardItem* parentItem = parent.isValid() ? itemFromIndex(parent) : invisibleRootItem();
    
    QList<int> categoryIds;
    int parentId = -1;
    
    // 再次确认父节点类型，确保同步到正确的数据库父 ID
    QString parentType = parentItem->data(TypeRole).toString();
    if (parentType == "category") {
        parentId = parentItem->data(IdRole).toInt();
    } else if (parentItem == invisibleRootItem()) {
        // [CRITICAL] 锁定：直接挂载到根节点时，parentId 为 -1
        parentId = -1; 
    } else {
        return; // 依然找不到有效的用户分类容器，放弃同步以防破坏数据
    }

    // 收集所有分类 ID，维护当前物理顺序
    QSet<int> seenIds;
    for (int i = 0; i < parentItem->rowCount(); ++i) {
        QStandardItem* child = parentItem->child(i);
        if (child->data(TypeRole).toString() == "category") {
            int id = child->data(IdRole).toInt();
            if (seenIds.contains(id)) continue; // 深度防御：跳过重复项
            seenIds.insert(id);
            categoryIds << id;
        }
    }
    
    if (!categoryIds.isEmpty()) {
        DatabaseManager::instance().updateCategoryOrder(parentId, categoryIds);
    }
    
    m_draggingId = -1; // 完成同步后重置
}
