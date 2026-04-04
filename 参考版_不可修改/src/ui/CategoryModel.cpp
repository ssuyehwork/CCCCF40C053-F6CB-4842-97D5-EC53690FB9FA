#include "CategoryModel.h"
#include "../db/CategoryRepo.h"
#include "../db/ItemRepo.h"
#include "UiHelper.h"
#include <QMimeData>
#include <QFileInfo>
#include <QFont>
#include <QTimer>
#include <QSet>
#include <QMap>
#include <QSettings>

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
            item->setIcon(UiHelper::getIcon(icon, QColor(color), 16));
            root->appendRow(item);
        };

        addSystemItem("全部数据", "all", "all_data", "#3498db");
        addSystemItem("今日数据", "today", "today", "#2ecc71");
        addSystemItem("昨日数据", "yesterday", "today", "#f39c12");
        addSystemItem("最近访问", "recently_visited", "clock", "#9b59b6");
        addSystemItem("回收站", "trash", "trash", "#e74c3c");
    }
    
    if (m_type == User || m_type == Both) {
        auto categories = CategoryRepo::getAll();
        
        // 物理还原：旧版统计逻辑 (总分类项 - 未分类项)
        int userUniqueCount = CategoryRepo::getUniqueItemCount();
        int uncatCount = CategoryRepo::getUncategorizedItemCount();
        int totalUserDisplay = qMax(0, userUniqueCount - uncatCount);

        QString userGroupName = QString("我的分类 (%1)").arg(totalUserDisplay);
        QStandardItem* userGroup = new QStandardItem(userGroupName);
        userGroup->setData("我的分类", NameRole);
        userGroup->setSelectable(false);
        userGroup->setEditable(false);
        userGroup->setFlags(userGroup->flags() | Qt::ItemIsDropEnabled);
        userGroup->setIcon(UiHelper::getIcon("branch", QColor("#FFFFFF"), 16));
        
        QFont font = userGroup->font();
        font.setBold(true);
        userGroup->setFont(font);
        userGroup->setForeground(QColor("#FFFFFF"));
        
        root->appendRow(userGroup);

        QMap<int, QStandardItem*> itemMap;
        QSettings settings("ArcMeta团队", "ArcMeta");
        int extensionTargetId = settings.value("Category/ExtensionTargetId", 0).toInt();

        // 1. 先建立所有节点 (保持 SQL 返回的 pinned DESC, sort_order ASC 顺序)
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
            item->setData(QString::fromStdWString(cat.encryptHint), EncryptHintRole);
            item->setFlags(item->flags() | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
            
            if (id == extensionTargetId) {
                item->setIcon(UiHelper::getIcon("toggle_right", QColor(color), 16));
            } else if (cat.encrypted && !m_unlockedIds.contains(id)) {
                // 物理补丁：根据解锁状态动态切换图标 (安全绿 #00A650 或默认灰)
                item->setIcon(UiHelper::getIcon("lock", QColor("#aaaaaa"), 16));
            } else if (cat.pinned) {
                item->setIcon(UiHelper::getIcon("pin_vertical", QColor(color), 16));
            } else {
                item->setIcon(UiHelper::getIcon("circle_filled", QColor(color), 14));
            }
            itemMap[id] = item;

            // [NEW] 按照用户需求 3：动态加载分类关联的文件子项以便直接点击预览
            std::vector<std::wstring> filePaths = CategoryRepo::getItemPathsInCategory(id);
            for (const auto& wp : filePaths) {
                QString fp = QString::fromStdWString(wp);
                QFileInfo fi(fp);
                if (!fi.exists()) continue;

                QStandardItem* fileItem = new QStandardItem(fi.fileName());
                fileItem->setData(fi.isDir() ? "folder" : "file", TypeRole);
                fileItem->setData(fp, PathRole);
                fileItem->setData(fi.fileName(), NameRole);
                fileItem->setIcon(UiHelper::getIcon(fi.isDir() ? "folder" : "text", QColor("#888888"), 14));
                item->appendRow(fileItem);
            }
        }

        // 2. 按照 SQL 排序顺序进行挂载 (由于 categories 已排序，appendRow 会维持该顺序)
        // 物理还原：如果分类被置顶，且它有父分类，它应该在父分类内部置顶。
        // 由于 CategoryRepo::getAll 已经是全局 Pinned DESC，这里按顺序插入即可。
        for (const auto& cat : categories) {
            int id = cat.id;
            int parentId = cat.parentId;
            if (parentId > 0 && itemMap.contains(parentId)) {
                itemMap[parentId]->appendRow(itemMap[id]);
            } else {
                // 如果该项已经被作为子项挂载过，则跳过
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
        
        // 2026-03-xx 物理兼容：处理分类重命名
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
        
        // 2026-03-xx 物理兼容：处理物理文件重命名 (暂不修改物理磁盘，仅修改数据库/显示名)
        if (type == "file" || type == "folder") {
            // 这里可以添加物理重命名逻辑，目前先通过刷新模型来恢复
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

bool CategoryModel::dropMimeData(const QMimeData* mimeData, Qt::DropAction action, int row, int column, const QModelIndex& parent) {
    Q_UNUSED(mimeData);
    Q_UNUSED(action);
    Q_UNUSED(row);
    Q_UNUSED(column);
    
    QModelIndex actualParent = parent;
    if (actualParent.isValid()) {
        QStandardItem* parentItem = itemFromIndex(actualParent);
        if (!parentItem) return false;
        
        QString type = parentItem->data(TypeRole).toString();
        QString name = parentItem->data(NameRole).toString();
        
        // 2026-03-xx 按照用户要求：物理放开“收藏”项的投放权限，允许接收外部文件拖入
        if (type != "category" && type != "bookmark" && name != "我的分类") {
            return false; 
        }
    }
    return QStandardItemModel::dropMimeData(mimeData, action, row, column, actualParent);
}

} // namespace ArcMeta
