#include "FileSystemTreeModel.h"
#include "../ui/IconHelper.h"
#include "../mft/PathBuilder.h"
#include <QDir>
#include <QStorageInfo>

FileSystemTreeModel::FileSystemTreeModel(MftReader* mft, QObject* parent)
    : QStandardItemModel(parent), m_mft(mft) {
}

void FileSystemTreeModel::initDrives() {
    clear();
    QStandardItem* root = invisibleRootItem();

    // 1. 桌面 (Shell 模拟)
    QStandardItem* desktop = new QStandardItem("桌面");
    desktop->setIcon(IconHelper::getIcon("desktop", "#3498db"));
    desktop->setData("Desktop", PathRole);
    root->appendRow(desktop);

    // 2. 此电脑
    QStandardItem* pc = new QStandardItem("此电脑");
    pc->setIcon(IconHelper::getIcon("pc", "#aaaaaa"));
    pc->setData("PC", PathRole);
    root->appendRow(pc);

    // 枚举逻辑驱动器
    for (const QStorageInfo& storage : QStorageInfo::mountedVolumes()) {
        if (storage.isValid() && storage.isReady()) {
            QString name = storage.displayName();
            if (name.isEmpty()) name = storage.rootPath();

            QStandardItem* driveItem = new QStandardItem(QString("%1 (%2)").arg(name).arg(storage.rootPath().left(2)));
            driveItem->setIcon(IconHelper::getIcon("drive", "#aaaaaa"));
            driveItem->setData(storage.rootPath(), PathRole);
            driveItem->setData(true, IsDirRole);

            // 默认驱动器下挂一个占位符，以支持展开箭头
            driveItem->setChild(0, new QStandardItem("正在加载..."));

            pc->appendRow(driveItem);
        }
    }
}

bool FileSystemTreeModel::canFetchMore(const QModelIndex& parent) const {
    if (!parent.isValid()) return false;
    QStandardItem* item = itemFromIndex(parent);
    // 如果还没加载过子节点（只有一个占位符），则可以 fetch
    return item && item->rowCount() == 1 && item->child(0)->text() == "正在加载...";
}

void FileSystemTreeModel::fetchMore(const QModelIndex& parent) {
    QStandardItem* parentItem = itemFromIndex(parent);
    if (!parentItem) return;

    // 移除占位符
    parentItem->removeRow(0);

    // TODO: 这里将来对接 MftReader::getChildren
    // 目前先通过 QDir 模拟，后期 MFT 索引完成后替换为 MFT 检索
    QString path = parentItem->data(PathRole).toString();
    QDir dir(path);
    QFileInfoList entries = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);

    for (const QFileInfo& info : entries) {
        QStandardItem* item = new QStandardItem(info.fileName());
        item->setIcon(IconHelper::getIcon("folder", "#f1c40f"));
        item->setData(info.absoluteFilePath(), PathRole);
        item->setData(true, IsDirRole);

        // 递归探测是否有子目录
        QDir subDir(info.absoluteFilePath());
        if (!subDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty()) {
            item->setChild(0, new QStandardItem("正在加载..."));
        }

        parentItem->appendRow(item);
    }
}
