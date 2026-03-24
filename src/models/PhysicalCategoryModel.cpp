#include "PhysicalCategoryModel.h"
#include "../ui/IconHelper.h"
#include "../db/Database.h"
#include <QStorageInfo>
#include <QStandardItem>
#include <QFont>
#include <QStandardPaths>

// 2026-03-24 按照用户要求：使用 SQLiteCpp
#include <SQLiteCpp/SQLiteCpp.h>

PhysicalCategoryModel::PhysicalCategoryModel(Type type, QObject* parent)
    : QStandardItemModel(parent), m_type(type)
{
    refresh();
}

void PhysicalCategoryModel::refresh() {
    // 2026-03-24 按照用户要求：MainWindow 专用物理分类模型，彻底脱离笔记库
    clear();
    QStandardItem* root = invisibleRootItem();

    if (m_type == System || m_type == Both) {
        // 1. 此电脑组
        QStandardItem* pcGroup = new QStandardItem("此电脑");
        pcGroup->setData("pc_group", TypeRole);
        pcGroup->setIcon(IconHelper::getIcon("pc", "#aaaaaa"));
        pcGroup->setEditable(false);
        root->appendRow(pcGroup);

        // 枚举磁盘驱动器
        for (const QStorageInfo& storage : QStorageInfo::mountedVolumes()) {
            if (storage.isValid() && storage.isReady()) {
                QString name = storage.displayName();
                if (name.isEmpty()) name = storage.rootPath();
                
                QStandardItem* driveItem = new QStandardItem(QString("%1 (%2)").arg(name).arg(storage.rootPath().left(2)));
                driveItem->setIcon(IconHelper::getIcon("drive", "#3498db"));
                driveItem->setData("drive", TypeRole);
                driveItem->setData(storage.rootPath(), PathRole);
                driveItem->setEditable(false);
                pcGroup->appendRow(driveItem);
            }
        }

        // 2. 快速访问 (模拟)
        QStandardItem* quickGroup = new QStandardItem("快速访问");
        quickGroup->setIcon(IconHelper::getIcon("star", "#f1c40f"));
        root->appendRow(quickGroup);
        
        auto addQuickItem = [&](const QString& name, QStandardPaths::StandardLocation loc, const QString& icon) {
            QString path = QStandardPaths::writableLocation(loc);
            QStandardItem* item = new QStandardItem(name);
            item->setData("quick_access", TypeRole);
            item->setData(path, PathRole);
            item->setIcon(IconHelper::getIcon(icon, "#aaaaaa"));
            quickGroup->appendRow(item);
        };
        
        addQuickItem("桌面", QStandardPaths::DesktopLocation, "desktop");
        addQuickItem("文档", QStandardPaths::DocumentsLocation, "file");
        addQuickItem("下载", QStandardPaths::DownloadLocation, "download");
    }

    if (m_type == Tag || m_type == Both) {
        // 3. 物理标签组
        QStandardItem* tagGroup = new QStandardItem("物理标签");
        tagGroup->setData("tag_group", NameRole);
        tagGroup->setSelectable(false);
        tagGroup->setEditable(false);
        
        QFont font = tagGroup->font();
        font.setBold(true);
        tagGroup->setFont(font);
        tagGroup->setForeground(QColor("#FFFFFF"));
        tagGroup->setIcon(IconHelper::getIcon("tag", "#FFFFFF"));
        root->appendRow(tagGroup);

        // 从物理数据库查询聚合标签
        SQLite::Database* db = Database::instance().getRawDb();
        if (db) {
            try {
                SQLite::Statement query(*db, "SELECT tag, item_count FROM tags ORDER BY item_count DESC");
                while (query.executeStep()) {
                    QString tagName = QString::fromStdString(query.getColumn(0).getText());
                    int count = query.getColumn(1).getInt();
                    
                    QStandardItem* item = new QStandardItem(QString("%1 (%2)").arg(tagName).arg(count));
                    item->setData("physical_tag", TypeRole);
                    item->setData(tagName, NameRole);
                    item->setData(true, IsTagRole);
                    item->setIcon(IconHelper::getIcon("circle_filled", "#2ecc71"));
                    tagGroup->appendRow(item);
                }
            } catch (...) {}
        }
    }
}
