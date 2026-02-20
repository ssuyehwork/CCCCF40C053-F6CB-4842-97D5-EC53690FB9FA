#include "FileStorageHelper.h"
#include "DatabaseManager.h"
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>

int FileStorageHelper::processImport(const QStringList& paths, int targetCategoryId) {
    if (paths.isEmpty()) return 0;

    if (paths.size() == 1) {
        QFileInfo info(paths.first());
        if (info.isDir()) {
            // 如果拖入的是单个文件夹，将其转为分类，内部文件转为数据
            storeFolderAsCategory(paths.first(), targetCategoryId);
            return 1;
        } else {
            // 单个文件
            storeFile(paths.first(), targetCategoryId);
            return 1;
        }
    } else {
        // 多个项目，作为批量导入处理
        storeItemsAsBatch(paths, targetCategoryId);
        return paths.size();
    }
}

QString FileStorageHelper::getStorageRoot() {
    QString path = QCoreApplication::applicationDirPath() + "/attachments";
    QDir dir(path);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    return path;
}

QString FileStorageHelper::getUniqueFilePath(const QString& dirPath, const QString& fileName) {
    QDir dir(dirPath);
    QString baseName = QFileInfo(fileName).completeBaseName();
    QString suffix = QFileInfo(fileName).suffix();
    if (!suffix.isEmpty()) suffix = "." + suffix;

    QString finalName = fileName;
    int counter = 1;

    while (dir.exists(finalName)) {
        finalName = QString("%1_%2%3").arg(baseName).arg(counter).arg(suffix);
        counter++;
    }
    return dir.filePath(finalName);
}

bool FileStorageHelper::copyRecursively(const QString& srcStr, const QString& dstStr) {
    QDir srcDir(srcStr);
    if (!srcDir.exists()) return false;

    QDir dstDir(dstStr);
    if (!dstDir.exists()) {
        dstDir.mkpath(".");
    }

    for (const QString& file : srcDir.entryList(QDir::Files)) {
        QString srcFile = srcDir.filePath(file);
        QString dstFile = dstDir.filePath(file);
        if (!QFile::copy(srcFile, dstFile)) {
            return false;
        }
    }

    for (const QString& dir : srcDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QString srcSub = srcDir.filePath(dir);
        QString dstSub = dstDir.filePath(dir);
        if (!copyRecursively(srcSub, dstSub)) {
            return false;
        }
    }
    return true;
}

void FileStorageHelper::storeFile(const QString& path, int categoryId) {
    QFileInfo info(path);
    QString storageDir = getStorageRoot();
    QString destPath = getUniqueFilePath(storageDir, info.fileName());

    if (QFile::copy(path, destPath)) {
        QFileInfo destInfo(destPath);
        QString relativePath = "attachments/" + destInfo.fileName();

        DatabaseManager::instance().addNote(
            info.fileName(),
            relativePath,
            {"文件链接"},
            "#2c3e50",
            categoryId,
            "local_file",
            QByteArray(),
            "FileStorage",
            info.absoluteFilePath()
        );
    }
}

void FileStorageHelper::storeFolderAsCategory(const QString& path, int parentCategoryId) {
    QFileInfo info(path);
    // 1. 创建分类
    int catId = DatabaseManager::instance().addCategory(info.fileName(), parentCategoryId);
    if (catId <= 0) return;

    // 2. 遍历并导入其下的一级文件 (目前按扁平化处理，不递归创建子分类以保持简洁)
    QDir dir(path);
    QStringList files = dir.entryList(QDir::Files);
    for (const QString& fileName : files) {
        storeFile(dir.filePath(fileName), catId);
    }

    // 如果有子文件夹，将其作为 local_folder 笔记存入该分类
    QStringList subDirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& subDirName : subDirs) {
        QString subPath = dir.filePath(subDirName);
        QFileInfo subInfo(subPath);
        QString storageDir = getStorageRoot();
        QString destDir = getUniqueFilePath(storageDir, subInfo.fileName());

        if (copyRecursively(subPath, destDir)) {
            QDir d(destDir);
            QString relativePath = "attachments/" + d.dirName();
            DatabaseManager::instance().addNote(
                subInfo.fileName(),
                relativePath,
                {"文件夹链接"},
                "#8e44ad",
                catId,
                "local_folder",
                QByteArray(),
                "FileStorage",
                subInfo.absoluteFilePath()
            );
        }
    }
}

void FileStorageHelper::storeItemsAsBatch(const QStringList& paths, int categoryId) {
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString folderName = "批量导入_" + timestamp;

    QString storageRoot = getStorageRoot();
    QString destDir = storageRoot + "/" + folderName;

    if (!QDir().mkpath(destDir)) return;

    int successCount = 0;
    for (const QString& srcPath : paths) {
        QFileInfo srcInfo(srcPath);
        QString destPath = destDir + "/" + srcInfo.fileName();

        bool copyOk = false;
        if (srcInfo.isDir()) {
            copyOk = copyRecursively(srcPath, destPath);
        } else {
            copyOk = QFile::copy(srcPath, destPath);
        }
        if (copyOk) successCount++;
    }

    if (successCount > 0) {
        QString relativePath = "attachments/" + folderName;
        QStringList names;
        for (const QString& p : paths) names << QFileInfo(p).fileName();
        QString descriptiveTitle = QString("[%1个项目] %2").arg(paths.size()).arg(names.join(", "));
        if (descriptiveTitle.length() > 120) descriptiveTitle = descriptiveTitle.left(117) + "...";

        DatabaseManager::instance().addNote(
            descriptiveTitle,
            relativePath,
            {"批量导入"},
            "#34495e",
            categoryId,
            "local_batch",
            QByteArray(),
            "FileStorage",
            ""
        );
    } else {
        QDir(destDir).removeRecursively();
    }
}
