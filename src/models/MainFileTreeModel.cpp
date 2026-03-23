#include "MainFileTreeModel.h"
#include "../core/DatabaseManager.h"
#include "../ui/IconHelper.h"
#include <QFont>
#include <QColor>

MainFileTreeModel::MainFileTreeModel(QObject* parent) : QStandardItemModel(parent) {
}

void MainFileTreeModel::setNotes(const QList<QVariantMap>& notes) {
    clear();
    QStandardItem* root = invisibleRootItem();
    for (const auto& note : notes) {
        addNoteToItem(root, note);
    }
}

void MainFileTreeModel::clearNotes() {
    clear();
}

void MainFileTreeModel::rebuildTree(const QString& filterType, const QVariant& filterValue, const QString& keyword, const QVariantMap& criteria) {
    clear();
    QStandardItem* root = invisibleRootItem();
    QVariantMap counts = DatabaseManager::instance().getCounts();

    // 2026-03-23 [NEW] 核心重构：支持无限级分类递归及笔记挂载
    // 1. 获取所有分类并构建分类树索引
    auto allCategories = DatabaseManager::instance().getAllCategories();
    QMap<int, QStandardItem*> catItemMap;

    // 先创建所有分类节点 (暂不挂载)
    for (const auto& cat : allCategories) {
        int id = cat["id"].toInt();
        int count = counts.value("cat_" + QString::number(id), 0).toInt();
        QString name = cat["name"].toString();
        QString color = cat["color"].toString();

        QString display = QString("%1 (%2)").arg(name).arg(count);
        QStandardItem* catItem = new QStandardItem(display);
        catItem->setData("category", TypeRole);
        catItem->setData(id, CategoryIdRole);
        catItem->setData(name, NameRole);
        catItem->setData(color, ColorRole);
        catItem->setData(cat["is_pinned"], PinnedRole);

        // 图标逻辑：按照图片样式，分类使用实心圆点或分支图标
        if (!color.isEmpty()) {
            catItem->setIcon(IconHelper::getIcon("circle_filled", color));
        } else {
            catItem->setIcon(IconHelper::getIcon("branch", "#aaaaaa"));
        }

        catItem->setEditable(false);
        catItemMap[id] = catItem;
    }

    // 2. 处理分类层级挂载
    for (const auto& cat : allCategories) {
        int id = cat["id"].toInt();
        int parentId = cat["parent_id"].toInt();

        if (parentId > 0 && catItemMap.contains(parentId)) {
            catItemMap[parentId]->appendRow(catItemMap[id]);
        } else {
            // 顶级分类，如果当前不是特定分类过滤，则直接挂在根部
            if (filterType != "category") {
                root->appendRow(catItemMap[id]);
            } else if (id == filterValue.toInt()) {
                // 如果当前正在过滤此分类，则它是树根
                root->appendRow(catItemMap[id]);
            }
        }
    }

    // 3. 获取笔记并挂载到对应分类下
    auto notes = DatabaseManager::instance().searchNotes(keyword, filterType, filterValue, -1, -1, criteria);
    for (const auto& note : notes) {
        int catId = note.value("category_id").toInt();
        QStandardItem* parentItem = root;

        if (catId > 0 && catItemMap.contains(catId)) {
            parentItem = catItemMap[catId];
        }

        addNoteToItem(parentItem, note);
    }

    // [UX] 如果是“今日数据”等系统模式，且根部没有内容，则尝试将所有包含数据的分类显示出来
    if (root->rowCount() == 0 && filterType != "all" && filterType != "category") {
        for (auto* item : catItemMap.values()) {
            if (item->rowCount() > 0 || item->parent() == nullptr) {
                 // 简单平铺有内容的分类到根部，以防视图空白
                 if (item->parent() == nullptr && root->rowCount() < 50) root->appendRow(item);
            }
        }
    }
}

void MainFileTreeModel::addNoteToItem(QStandardItem* parent, const QVariantMap& note) {
    QString title = note.value("title").toString();
    QString type = note.value("item_type").toString();
    QString color = note.value("color").toString();
    int rating = note.value("rating").toInt();

    // 格式化显示：名称 (如果有统计/额外信息)
    QStandardItem* item = new QStandardItem(title);

    item->setData(note.value("id"), IdRole);
    item->setData(type, TypeRole);
    item->setData(color, ColorRole);
    item->setData(title, NameRole);
    item->setData(note.value("is_pinned"), PinnedRole);
    item->setData(note.value("is_favorite"), FavoriteRole);
    item->setData(rating, RatingRole);
    item->setData(note.value("tags"), TagsRole);
    item->setData(note.value("created_at"), TimeRole);
    item->setData(note.value("remark"), RemarkRole);
    item->setData(note.value("data_blob"), BlobRole);
    item->setData(note.value("category_id"), CategoryIdRole);
    item->setData(note.value("category_name"), CategoryNameRole);

    // 根据类型设置图标
    QString iconName = "file_text";
    if (type == "image") iconName = "image";
    else if (type == "link") iconName = "link";
    else if (type.startsWith("local_")) iconName = "folder";

    item->setIcon(IconHelper::getIcon(iconName, color.isEmpty() ? "#aaaaaa" : color));

    parent->appendRow(item);
}
