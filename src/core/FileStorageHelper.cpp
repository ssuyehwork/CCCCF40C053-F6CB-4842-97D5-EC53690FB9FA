#include "FileStorageHelper.h"
#include "DatabaseManager.h"
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QApplication>
#include <QProgressDialog>

int FileStorageHelper::processImport(const QStringList& paths, int targetCategoryId, bool fromClipboard) {
    if (paths.isEmpty()) return 0;

    qint64 totalSize = calculateTotalSize(paths);
    qint64 processedSize = 0;

    QProgressDialog* progress = nullptr;
    const qint64 threshold = 50 * 1024 * 1024; // 50MB

    if (totalSize >= threshold) {
        progress = new QProgressDialog("正在导入文件和目录结构...", "取消", 0, totalSize);
        progress->setWindowTitle("导入进度");
        progress->setWindowModality(Qt::WindowModal);
        progress->setMinimumDuration(500);
        progress->setValue(0);
    }

    int totalCount = 0;
    for (const QString& path : paths) {
        if (progress && progress->wasCanceled()) break;

        QFileInfo info(path);
        if (info.isDir()) {
            totalCount += importFolderRecursive(path, targetCategoryId, progress, &processedSize);
        } else {
            if (storeFile(path, targetCategoryId, progress, &processedSize, fromClipboard)) {
                totalCount++;
            }
        }
    }

    if (progress) {
        progress->setValue(totalSize);
        delete progress;
    }

    return totalCount;
}

qint64 FileStorageHelper::calculateTotalSize(const QStringList& paths) {
    qint64 total = 0;
    for (const QString& path : paths) {
        QFileInfo info(path);
        if (info.isDir()) {
            QDir dir(path);
            QStringList subPaths;
            for (const auto& entry : dir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
                subPaths << dir.absoluteFilePath(entry);
            }
            total += calculateTotalSize(subPaths);
        } else {
            total += info.size();
        }
    }
    return total;
}

int FileStorageHelper::importFolderRecursive(const QString& folderPath, int parentCategoryId, QProgressDialog* progress, qint64* processedSize) {
    QFileInfo info(folderPath);
    // 1. 创建分类
    int catId = DatabaseManager::instance().addCategory(info.fileName(), parentCategoryId);
    if (catId <= 0) return 0;

    int count = 1; // 包含分类自身
    QDir dir(folderPath);

    // 2. 导入文件
    for (const QString& fileName : dir.entryList(QDir::Files)) {
        if (progress && progress->wasCanceled()) break;
        if (storeFile(dir.filePath(fileName), catId, progress, processedSize)) {
            count++;
        }
    }

    // 3. 递归导入子文件夹
    for (const QString& subDirName : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (progress && progress->wasCanceled()) break;
        count += importFolderRecursive(dir.filePath(subDirName), catId, progress, processedSize);
    }

    return count;
}

bool FileStorageHelper::storeFile(const QString& path, int categoryId, QProgressDialog* progress, qint64* processedSize, bool fromClipboard) {
    QFileInfo info(path);
    QString storageDir = getStorageRoot();
    QString destPath = getUniqueFilePath(storageDir, info.fileName());

    // 执行物理拷贝
    bool ok = QFile::copy(path, destPath);

    if (ok) {
        if (processedSize) {
            *processedSize += info.size();
            if (progress) {
                 progress->setValue(*processedSize);
                 progress->setLabelText(QString("正在导入: %1").arg(info.fileName()));
            }
        }

        QFileInfo destInfo(destPath);
        QString relativePath = "attachments/" + destInfo.fileName();

        QString title = info.fileName();
        if (fromClipboard) {
            title = "Copied File - " + title;
        }

        DatabaseManager::instance().addNote(
            title,
            relativePath,
            {"导入文件"},
            "#2c3e50",
            categoryId,
            "local_file",
            QByteArray(),
            "FileStorage",
            info.absoluteFilePath()
        );

        QApplication::processEvents();
    }

    return ok;
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
