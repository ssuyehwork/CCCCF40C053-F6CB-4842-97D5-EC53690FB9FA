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
        // 用户分类
        // [CRITICAL] 锁定：必须使用“我的分区”纯文本定义，严禁绑定 TypeRole 或 NameRole，防止逻辑漂移
        QStandardItem* userGroup = new QStandardItem("我的分区");
        userGroup->setSelectable(false);
        userGroup->setEditable(false);
        userGroup->setFlags(userGroup->flags() | Qt::ItemIsDropEnabled);
        userGroup->setIcon(IconHelper::getIcon("branch", "#FFFFFF"));
        
        // 设为粗体白色
        QFont font = userGroup->font();
        font.setBold(true);
        userGroup->setFont(font);
        userGroup->setForeground(QColor("#FFFFFF"));
        
        root->appendRow(userGroup);

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

        for (const auto& cat : categories) {
            int id = cat["id"].toInt();
            int parentId = cat["parent_id"].toInt();
            if (parentId > 0 && itemMap.contains(parentId)) {
                itemMap[parentId]->appendRow(itemMap[id]);
            } else {
                userGroup->appendRow(itemMap[id]);
            }
        }
    }
}

QVariant CategoryModel::data(const QModelIndex& index, int role) const {
    // [CRITICAL] 必须在此处拦截 EditRole 并返回 NameRole（不含计数），否则会因 QStandardItem 的默认逻辑导致 DisplayRole 计数丢失。
    if (role == Qt::EditRole) {
        return QStandardItemModel::data(index, NameRole);
    }
    return QStandardItemModel::data(index, role);
}

bool CategoryModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    // [CRITICAL] 拦截编辑提交，同步更新数据库
    if (role == Qt::EditRole) {
        QString newName = value.toString().trimmed();
        int id = index.data(IdRole).toInt();
        if (!newName.isEmpty() && id > 0) {
            if (DatabaseManager::instance().renameCategory(id, newName)) {
                // 异步刷新以确保 UI 显示最新的名称及计数
                QTimer::singleShot(0, [this]() { this->refresh(); });
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
    
    // 核心修复：处理正在拖拽分类的情况（m_draggingId != -1）
    if (m_draggingId != -1) {
        bool needsRedirect = false;
        if (!actualParent.isValid()) {
            needsRedirect = true;
        } else {
            QStandardItem* targetItem = itemFromIndex(actualParent);
            QString type = targetItem->data(TypeRole).toString();
            // [CRITICAL] 锁定：此处必须使用 text() 直接匹配“我的分区”，确保拖拽重定向逻辑的唯一性
            if (type != "category" && targetItem->text() != "我的分区") {
                needsRedirect = true;
            }
        }

        if (needsRedirect) {
            // 寻找 "我的分区" 容器索引
            for (int i = 0; i < rowCount(); ++i) {
                QStandardItem* it = item(i);
                if (it->text() == "我的分区") {
                    actualParent = index(i, 0);
                    break;
                }
            }
        }
    }

    // 再次检查重定向后的合法性
    if (actualParent.isValid()) {
        QStandardItem* parentItem = itemFromIndex(actualParent);
        QString type = parentItem->data(TypeRole).toString();
        if (type != "category" && parentItem->text() != "我的分区") {
            return false; // 依然非法则拒绝，防止回弹
        }
    } else {
        return false; // 根部非法释放
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
    
    // [CRITICAL] 锁定：核心同步逻辑，必须通过文本匹配“我的分区”来定位用户分类根节点
    if (parentItem == invisibleRootItem() || (parentItem->data(TypeRole).toString() != "category" && parentItem->text() != "我的分区")) {
        for (int i = 0; i < rowCount(); ++i) {
            QStandardItem* it = item(i);
            if (it->text() == "我的分区") {
                parentItem = it;
                break;
            }
        }
    }

    QList<int> categoryIds;
    int parentId = -1;
    
    // 再次确认父节点类型，确保同步到正确的数据库父 ID
    QString parentType = parentItem->data(TypeRole).toString();
    if (parentType == "category") {
        parentId = parentItem->data(IdRole).toInt();
    } else if (parentItem->text() == "我的分区") {
        // [CRITICAL] 锁定：匹配文本“我的分区”时，强制将 parentId 设为 -1，代表顶级分类
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
