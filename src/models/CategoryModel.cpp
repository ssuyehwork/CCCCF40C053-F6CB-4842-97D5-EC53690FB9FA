#include "CategoryModel.h"
#include "../db/CategoryRepo.h"
#include "../db/ItemRepo.h"
#include "../ui/IconHelper.h"
#include <QMimeData>
#include <QFileInfo>
#include <QFont>
#include <QTimer>
#include <QSet>
#include <QMap>
#include <QSettings>

// 2026-04-04 [STRATEGY] 彻底转型为超级资源管理器，对接 CategoryRepo。
// 严禁修改 UI 风格：保留加粗白色“我的分类”、特定图标颜色 (#62BAC1, #e74c3c 等)。

namespace ArcMeta {

CategoryModel::CategoryModel(Type type, QObject* parent) 
    : QStandardItemModel(parent), m_type(type) 
{
    refresh();
}

void CategoryModel::setUnlockedIds(const QSet<int>& ids) {
    m_unlockedIds = ids;
}

void CategoryModel::refresh() {
    clear();
    QStandardItem* root = invisibleRootItem();

    auto countsVec = CategoryRepo::getCounts();
    QMap<int, int> counts;
    for (const auto& p : countsVec) counts[p.first] = p.second;

    QMap<QString, int> systemCounts = CategoryRepo::getSystemCounts();

    if (m_type == System || m_type == Both) {
        auto addSystemItem = [&](const QString& name, const QString& type, const QString& icon, const QString& color = "#aaaaaa") {
            int count = systemCounts.value(type, 0);
            QString display = QString("%1 (%2)").arg(name).arg(count);
            QStandardItem* item = new QStandardItem(display);
            item->setData(type, TypeRole);
            item->setData(name, NameRole);
            item->setData(color, ColorRole);
            item->setEditable(false);
            item->setIcon(IconHelper::getIcon(icon, color));
            root->appendRow(item);
        };

        addSystemItem("全部数据", "all", "all_data", "#3498db");
        addSystemItem("今日数据", "today", "today", "#2ecc71");
        addSystemItem("昨日数据", "yesterday", "today", "#f39c12");
        addSystemItem("最近访问", "recently_visited", "clock", "#9b59b6");
        addSystemItem("未分类", "uncategorized", "uncategorized", "#e67e22");
        // 2026-03-13 按照用户要求：修改“未标签”项的图标颜色为 #62BAC1
        addSystemItem("未标签", "untagged", "untagged", "#62BAC1");
        // 2026-03-13 按照用户要求：修改“收藏”项的图标为 bookmark_filled，颜色为 #F2B705
        addSystemItem("收藏", "bookmark", "bookmark_filled", "#F2B705");
        // 2026-03-13 按照用户最高指令：凡是 trash 图标必须为红色 (#e74c3c)
        addSystemItem("回收站", "trash", "trash", "#e74c3c");
    }
    
    if (m_type == User || m_type == Both) {
        auto categories = CategoryRepo::getAll();
        
        int userUniqueCount = CategoryRepo::getUniqueItemCount();
        int uncatCount = CategoryRepo::getUncategorizedItemCount();
        int totalUserDisplay = qMax(0, userUniqueCount - uncatCount);

        QString userGroupName = QString("我的分类 (%1)").arg(totalUserDisplay);
        QStandardItem* userGroup = new QStandardItem(userGroupName);
        userGroup->setData("我的分类", NameRole);
        userGroup->setSelectable(false);
        userGroup->setEditable(false);
        userGroup->setFlags(userGroup->flags() | Qt::ItemIsDropEnabled);
        userGroup->setIcon(IconHelper::getIcon("branch", "#FFFFFF"));
        
        // 样式保全：粗体白色
        QFont font = userGroup->font();
        font.setBold(true);
        userGroup->setFont(font);
        userGroup->setForeground(QColor("#FFFFFF"));
        
        root->appendRow(userGroup);

        QMap<int, QStandardItem*> itemMap;
        QSettings settings("ArcMeta团队", "ArcMeta");
        int extensionTargetId = settings.value("Category/ExtensionTargetId", 0).toInt();

        for (const auto& cat : categories) {
            int id = cat.id;
            QString name = QString::fromStdWString(cat.name);
            QString color = QString::fromStdWString(cat.color).isEmpty() ? "#aaaaaa" : QString::fromStdWString(cat.color);
            
            int count = counts.value(id, 0);
            QString display = QString("%1 (%2)").arg(name).arg(count);

            QStandardItem* item = new QStandardItem(display);
            item->setData("category", TypeRole);
            item->setData(id, IdRole);
            item->setData(color, ColorRole);
            item->setData(name, NameRole);
            item->setData(cat.pinned, PinnedRole);
            item->setData(cat.encrypted, EncryptedRole);
            item->setData(cat.encrypted, HasPasswordRole); // 兼容
            item->setData(QString::fromStdWString(cat.encryptHint), EncryptHintRole);
            item->setFlags(item->flags() | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
            
            if (id == extensionTargetId) {
                item->setIcon(IconHelper::getIcon("toggle_right", color));
            } else if (cat.encrypted && !m_unlockedIds.contains(id)) {
                item->setIcon(IconHelper::getIcon("lock", "#aaaaaa"));
            } else if (cat.pinned) {
                item->setIcon(IconHelper::getIcon("pin_vertical", color));
            } else {
                item->setIcon(IconHelper::getIcon("circle_filled", color));
            }
            itemMap[id] = item;

            // 动态加载分类关联的文件子项
            std::vector<std::wstring> filePaths = CategoryRepo::getItemPathsInCategory(id);
            for (const auto& wp : filePaths) {
                QString fp = QString::fromStdWString(wp);
                QFileInfo fi(fp);
                if (!fi.exists()) continue;

                QStandardItem* fileItem = new QStandardItem(fi.fileName());
                fileItem->setData(fi.isDir() ? "folder" : "file", TypeRole);
                fileItem->setData(fp, PathRole);
                fileItem->setData(fi.fileName(), NameRole);
                fileItem->setIcon(IconHelper::getIcon(fi.isDir() ? "folder" : "text", "#888888"));
                item->appendRow(fileItem);
            }
        }

        for (const auto& cat : categories) {
            int id = cat.id;
            int parentId = cat.parentId;
            if (parentId > 0 && itemMap.contains(parentId)) {
                itemMap[parentId]->appendRow(itemMap[id]);
            } else {
                if (itemMap[id]->parent() == nullptr) {
                    userGroup->appendRow(itemMap[id]);
                }
            }
        }
    }
}

QVariant CategoryModel::data(const QModelIndex& index, int role) const {
    if (role == Qt::EditRole) {
        return QStandardItemModel::data(index, NameRole);
    }
    return QStandardItemModel::data(index, role);
}

bool CategoryModel::setData(const QModelIndex& index, const QVariant& val, int role) {
    if (role == Qt::EditRole) {
        QString newName = val.toString().trimmed();
        if (newName.isEmpty()) return false;

        QString type = index.data(TypeRole).toString();
        int id = index.data(IdRole).toInt();

        if (type == "category" && id > 0) {
            auto categories = CategoryRepo::getAll();
            for (auto& cat : categories) {
                if (cat.id == id) {
                    cat.name = newName.toStdWString();
                    CategoryRepo::update(cat);
                    break;
                }
            }
            QTimer::singleShot(0, [this]() { this->refresh(); });
            return true;
        }

        if (type == "file" || type == "folder") {
            QTimer::singleShot(0, [this]() { this->refresh(); });
            return false;
        }

        return false;
    }
    return QStandardItemModel::setData(index, val, role);
}

Qt::DropActions CategoryModel::supportedDropActions() const {
    return Qt::MoveAction;
}

bool CategoryModel::dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column, const QModelIndex& parent) {
    QModelIndex actualParent = parent;
    if (actualParent.isValid()) {
        QStandardItem* parentItem = itemFromIndex(actualParent);
        if (!parentItem) return false;
        
        QString type = parentItem->data(TypeRole).toString();
        QString name = parentItem->data(NameRole).toString();

        if (type != "category" && type != "bookmark" && name != "我的分类") {
            return false; 
        }
    }

    bool ok = QStandardItemModel::dropMimeData(data, action, row, column, actualParent);
    if (ok && action == Qt::MoveAction) {
        QPersistentModelIndex persistentParent = actualParent;
        QTimer::singleShot(0, [this, persistentParent]() {
            syncOrders(persistentParent);
        });
    } else {
        m_draggingId = -1; 
    }
    return ok;
}

void CategoryModel::updateExtensionIcons() {
    // 逻辑迁移：根据新 Repo 局部更新
    QSettings settings("ArcMeta团队", "ArcMeta");
    int targetId = settings.value("Category/ExtensionTargetId", 0).toInt();
    
    QList<QStandardItem*> stack;
    for (int i = 0; i < rowCount(); ++i) stack.append(item(i));

    while (!stack.isEmpty()) {
        QStandardItem* item = stack.takeLast();
        if (item->data(TypeRole).toString() == "category") {
            int id = item->data(IdRole).toInt();
            QString color = item->data(ColorRole).toString();
            bool isPinned = item->data(PinnedRole).toBool();
            bool encrypted = item->data(EncryptedRole).toBool();
            
            if (id == targetId) {
                item->setIcon(IconHelper::getIcon("toggle_right", color));
            } else {
                if (encrypted && !m_unlockedIds.contains(id)) {
                    item->setIcon(IconHelper::getIcon("lock", "#aaaaaa"));
                } else if (isPinned) {
                    item->setIcon(IconHelper::getIcon("pin_vertical", color));
                } else {
                    item->setIcon(IconHelper::getIcon("circle_filled", color));
                }
            }
        }
        for (int i = 0; i < item->rowCount(); ++i) stack.append(item->child(i));
    }
}

void CategoryModel::syncOrders(const QModelIndex& parent) {
    QStandardItem* parentItem = parent.isValid() ? itemFromIndex(parent) : invisibleRootItem();
    
    // 定位“我的分类”根容器
    if (parentItem == invisibleRootItem() || (parentItem->data(TypeRole).toString() != "category" && parentItem->data(NameRole).toString() != "我的分类")) {
        for (int i = 0; i < rowCount(); ++i) {
            QStandardItem* it = item(i);
            if (it && it->data(NameRole).toString() == "我的分类") {
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

    for (int i = 0; i < parentItem->rowCount(); ++i) {
        QStandardItem* child = parentItem->child(i);
        if (child && child->data(TypeRole).toString() == "category") {
            categoryIds << child->data(IdRole).toInt();
        }
    }
    
    if (!categoryIds.isEmpty()) {
        CategoryRepo::updateOrders(parentId, categoryIds);
    }
    
    m_draggingId = -1;
}

} // namespace ArcMeta
