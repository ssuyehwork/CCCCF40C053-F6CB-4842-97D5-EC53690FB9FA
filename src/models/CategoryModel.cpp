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
    beginResetModel();
    removeRows(0, rowCount());
    
    DatabaseManager& db = DatabaseManager::instance();
    QVariantMap counts = DatabaseManager::instance().getCounts();

    // 1. 系统模块 (System Group)
    if (m_type == System || m_type == Both) {
        auto addSystemItem = [&](const QString& name, const QString& type, const QString& icon, const QString& color = "#aaaaaa") {
            int count = counts.value(type, 0).toInt();
            QString display = QString("%1 (%2)").arg(name).arg(count);
            QStandardItem* item = new QStandardItem(display);
            item->setData(type, TypeRole);
            item->setData(name, NameRole);
            item->setData(color, ColorRole); 
            item->setEditable(false); 
            item->setIcon(IconHelper::getIcon(icon, color));
            invisibleRootItem()->appendRow(item);
        };

        addSystemItem("全部数据", "all", "all_data", "#3498db");
        addSystemItem("今日数据", "today", "today", "#2ecc71");
        addSystemItem("昨日数据", "yesterday", "today", "#f39c12");
        addSystemItem("最近访问", "recently_visited", "clock", "#9b59b6");
        addSystemItem("未分类", "uncategorized", "uncategorized", "#e67e22");
        addSystemItem("未标签", "untagged", "untagged", "#62BAC1");
        addSystemItem("回收站", "trash", "trash", "#e74c3c");
        addSystemItem("收藏", "bookmark", "bookmark_filled", "#2ECC71"); // 修改为翠绿色
    }
    
    // 2. 快速访问与我的分类 (User & Both Mode)
    if (m_type == User || m_type == Both) {
        QStandardItem* root = invisibleRootItem();
        // [A] 定义容器组
        QStandardItem* favGroup = new QStandardItem("快速访问");
        favGroup->setData("快速访问", NameRole);
        favGroup->setSelectable(false);
        favGroup->setEditable(false);
        favGroup->setIcon(IconHelper::getIcon("zap", "#f1c40f")); // 修改为黄金色
        QFont groupFont = favGroup->font();        groupFont.setBold(true);
        favGroup->setFont(groupFont);
        favGroup->setForeground(QColor("#FFFFFF"));
        root->appendRow(favGroup);

        int allCount = counts.value("all", 0).toInt();
        int uncatCount = counts.value("uncategorized", 0).toInt();
        int userTotalCount = qMax(0, allCount - uncatCount);
        
        QStandardItem* userGroup = new QStandardItem(QString("我的分类 (%1)").arg(userTotalCount));
        userGroup->setData("我的分类", NameRole);
        userGroup->setSelectable(false);
        userGroup->setEditable(false);
        userGroup->setFlags(userGroup->flags() | Qt::ItemIsDropEnabled);
        userGroup->setIcon(IconHelper::getIcon("branch", "#FFFFFF"));
        userGroup->setFont(groupFont);
        userGroup->setForeground(QColor("#FFFFFF"));
        root->appendRow(userGroup);

        auto categories = DatabaseManager::instance().getAllCategories();
        QMap<int, QStandardItem*> itemMap;
        bool hideLocked = DatabaseManager::instance().isLockedCategoriesHidden();
        
        // --- 第一步：构建物理实体节点 ---
        for (const auto& cat : categories) {
            int id = cat["id"].toInt();
            bool hasPassword = !cat["password"].toString().isEmpty();
            if (hideLocked && hasPassword) continue;

            int count = counts.value("cat_" + QString::number(id), 0).toInt();
            QString name = cat["name"].toString();
            QString color = cat["color"].toString();
            bool isPinned = cat["is_pinned"].toBool();
            
            QStandardItem* item = new QStandardItem(QString("%1 (%2)").arg(name).arg(count));
            item->setData("category", TypeRole);
            item->setData(id, IdRole);
            item->setData(color, ColorRole);
            item->setData(name, NameRole);
            item->setData(isPinned, PinnedRole);
            item->setData(hasPassword, HasPasswordRole);
            item->setFlags(item->flags() | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
            
            bool isLocked = DatabaseManager::instance().isCategoryLocked(id);
            if (hasPassword) {
                item->setIcon(IconHelper::getIcon(isLocked ? "lock" : "unlock", isLocked ? "#aaaaaa" : color));
            } else {
                // 实体树中始终显示圆圈，置顶状态由镜像体现
                item->setIcon(IconHelper::getIcon("circle_filled", color));
            }
            itemMap[id] = item;
        }

        // --- 第二步：挂载物理层级树 ---
        for (const auto& cat : categories) {
            int id = cat["id"].toInt();
            if (!itemMap.contains(id)) continue;

            QStandardItem* item = itemMap[id];
            bool isPinned = cat["is_pinned"].toBool();
            QString name = cat["name"].toString();
            QString color = cat["color"].toString();
            bool hasPassword = !cat["password"].toString().isEmpty();
            int count = counts.value("cat_" + QString::number(id), 0).toInt();

            // A. [镜像模式逻辑]：如果是置顶项，在“快速访问”中创建一个指向同一 ID 的虚拟镜像
            if (isPinned) {
                QString display = QString("%1 (%2)").arg(name).arg(count);
                QStandardItem* mirror = new QStandardItem(display); 
                mirror->setData("category", TypeRole);
                mirror->setData(id, IdRole);
                mirror->setData(color, ColorRole);
                mirror->setData(name, NameRole);
                mirror->setData(true, PinnedRole);
                mirror->setData(hasPassword, HasPasswordRole);
                mirror->setEditable(false); 
                
                // 镜像项使用置顶图标 (锁定 pin_vertical)
                mirror->setIcon(IconHelper::getIcon("pin_vertical", color));
                favGroup->appendRow(mirror);
            }

            // B. [物理挂载逻辑]：保持分类原有的父子层级
            int parentId = cat["parent_id"].toInt();
            if (parentId > 0 && itemMap.contains(parentId)) {
                itemMap[parentId]->appendRow(item);
            } else {
                userGroup->appendRow(item);
            }
        }
    }
    endResetModel();
}

QVariant CategoryModel::data(const QModelIndex& index, int role) const {
    if (role == Qt::EditRole) {
        return QStandardItemModel::data(index, NameRole);
    }
    return QStandardItemModel::data(index, role);
}

bool CategoryModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (role == Qt::EditRole) {
        QString newName = value.toString().trimmed();
        int id = index.data(IdRole).toInt();
        if (!newName.isEmpty() && id > 0) {
            // 2026-04-18 移除冗余的异步刷新，DatabaseManager::renameCategory 已发出 categoriesChanged 信号
            if (DatabaseManager::instance().renameCategory(id, newName)) {
                return true;
            }
        }
        return false;
    }
    return QStandardItemModel::setData(index, value, role);
}

Qt::DropActions CategoryModel::supportedDropActions() const {
    return Qt::MoveAction;
}

bool CategoryModel::dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column, const QModelIndex& parent) {
    QModelIndex actualParent = parent;
    if (m_draggingId != -1) {
        bool needsRedirect = false;
        if (!actualParent.isValid()) {
            needsRedirect = true;
        } else {
            QStandardItem* targetItem = itemFromIndex(actualParent);
            if (targetItem) {
                QString type = targetItem->data(TypeRole).toString();
                if (type != "category" && targetItem->data(NameRole).toString() != "我的分类") {
                    needsRedirect = true;
                }
            } else {
                needsRedirect = true;
            }
        }

        if (needsRedirect) {
            for (int i = 0; i < rowCount(); ++i) {
                QStandardItem* it = item(i);
                if (it && it->data(NameRole).toString() == "我的分类") {
                    actualParent = index(i, 0);
                    break;
                }
            }
        }
    }

    if (actualParent.isValid()) {
        QStandardItem* parentItem = itemFromIndex(actualParent);
        if (!parentItem) return false;
        
        QString type = parentItem->data(TypeRole).toString();
        QString name = parentItem->data(NameRole).toString();
        if (type != "category" && name != "我的分类") {
            return false; 
        }
    } else {
        return false; 
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
    
    if (parentItem == invisibleRootItem() || (parentItem->data(TypeRole).toString() != "category" && parentItem->data(NameRole).toString() != "我的分类")) {
        for (int i = 0; i < rowCount(); ++i) {
            QStandardItem* it = item(i);
            if (it->data(NameRole).toString() == "我的分类") {
                parentItem = it;
                break;
            }
        }
    }

    QList<int> categoryIds;
    int parentId = -1;
    QString parentType = parentItem->data(TypeRole).toString();
    if (parentType == "category") {
        parentId = parentItem->data(IdRole).toInt();
    } else if (parentItem->data(NameRole).toString() == "我的分类") {
        parentId = -1; 
    } else {
        return;
    }

    QSet<int> seenIds;
    for (int i = 0; i < parentItem->rowCount(); ++i) {
        QStandardItem* child = parentItem->child(i);
        if (child->data(TypeRole).toString() == "category") {
            int id = child->data(IdRole).toInt();
            if (seenIds.contains(id)) continue;
            seenIds.insert(id);
            categoryIds << id;
        }
    }
    
    if (!categoryIds.isEmpty()) {
        DatabaseManager::instance().updateCategoryOrder(parentId, categoryIds);
    }
    m_draggingId = -1;
}
