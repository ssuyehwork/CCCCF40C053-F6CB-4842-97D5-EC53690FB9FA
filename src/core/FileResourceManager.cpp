#include "FileResourceManager.h"
#include "../meta/AmMetaJson.h"
#include "../meta/SyncQueue.h"
#include <QFileInfo>
#include <QDir>
#include <QFile>

namespace ArcMeta {

FileResourceManager& FileResourceManager::instance() {
    static FileResourceManager inst;
    return inst;
}

FileResourceManager::FileResourceManager(QObject* parent) : QObject(parent) {}

ItemMeta FileResourceManager::getItemMeta(const QString& fullPath) {
    QFileInfo info(fullPath);
    std::wstring parentDir = info.absolutePath().toStdWString();
    std::wstring fileName = info.fileName().toStdWString();

    AmMetaJson json(parentDir);
    if (json.load()) {
        auto it = json.items().find(fileName);
        if (it != json.items().end()) {
            return it->second;
        }
    }
    return ItemMeta();
}

bool FileResourceManager::setItemMeta(const QString& fullPath, const ItemMeta& meta) {
    QFileInfo info(fullPath);
    std::wstring parentDir = info.absolutePath().toStdWString();
    std::wstring fileName = info.fileName().toStdWString();

    AmMetaJson json(parentDir);
    json.load();
    json.items()[fileName] = meta;
    
    if (json.save()) {
        ::ArcMeta::SyncQueue::instance().enqueue(parentDir);
        return true;
    }
    return false;
}

bool FileResourceManager::renameFile(const QString& oldPath, const QString& newPath) {
    QFileInfo oldInfo(oldPath);
    QFileInfo newInfo(newPath);
    
    if (AmMetaJson::renameItem(oldInfo.absolutePath(), oldInfo.fileName(), newInfo.fileName())) {
        return QFile::rename(oldPath, newPath);
    }
    return false;
}

bool FileResourceManager::moveFile(const QString& srcPath, const QString& dstDir) {
    QFileInfo srcInfo(srcPath);
    QString dstPath = QDir(dstDir).filePath(srcInfo.fileName());
    
    ItemMeta meta = getItemMeta(srcPath);
    
    if (QFile::rename(srcPath, dstPath)) {
        setItemMeta(dstPath, meta);
        
        AmMetaJson oldJson(srcInfo.absolutePath().toStdWString());
        if (oldJson.load()) {
            oldJson.items().erase(srcInfo.fileName().toStdWString());
            oldJson.save();
            ::ArcMeta::SyncQueue::instance().enqueue(srcInfo.absolutePath().toStdWString());
        }
        return true;
    }
    return false;
}

} // namespace ArcMeta
