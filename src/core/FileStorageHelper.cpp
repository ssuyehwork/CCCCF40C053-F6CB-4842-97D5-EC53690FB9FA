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
        // 使用 1000 作为精度，防止 qint64 字节数超出 QProgressDialog 的 int 范围
        progress = new QProgressDialog("正在导入文件和目录结构...", "取消", 0, 1000);
        progress->setProperty("totalSize", totalSize);
        progress->setWindowTitle("导入进度");
        progress->setWindowModality(Qt::WindowModal);
        progress->setMinimumDuration(500);
        progress->setValue(0);

        // 设置无边框且置顶
        progress->setWindowFlags(progress->windowFlags() | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        progress->setStyleSheet(
            "QProgressDialog { background-color: #2D2D30; border: 1px solid #444; border-radius: 8px; }"
            "QLabel { color: #EEE; font-size: 13px; }"
            "QProgressBar { border: 1px solid #555; border-radius: 4px; text-align: center; color: white; background-color: #1E1E1E; }"
            "QProgressBar::chunk { background-color: #3A90FF; border-radius: 3px; }"
            "QPushButton { background-color: #3E3E42; color: #EEE; border: 1px solid #555; border-radius: 4px; padding: 5px 15px; }"
            "QPushButton:hover { background-color: #4E4E52; }"
        );
    }

    int totalCount = 0;
    for (const QString& path : paths) {
        if (progress && progress->wasCanceled()) break;

        QFileInfo info(path);
        if (info.isDir()) {
            // [CRITICAL] 无论拖拽到界面任何位置，文件夹始终作为顶级分类（parentId = -1）创建
            // 严格遵循用户要求：“只要拖拽文件夹到quickwindow或mainwindow界面的任何位置必须以新的分类来创建”
            totalCount += importFolderRecursive(path, -1, progress, &processedSize, fromClipboard);
        } else {
            if (storeFile(path, targetCategoryId, progress, &processedSize, fromClipboard)) {
                totalCount++;
            }
        }
    }

    if (progress) {
        progress->setValue(1000);
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

int FileStorageHelper::importFolderRecursive(const QString& folderPath, int parentCategoryId, QProgressDialog* progress, qint64* processedSize, bool fromClipboard) {
    QFileInfo info(folderPath);

    // 直接采用文件夹原始名称
    QString catName = info.fileName();

    // 1. 创建分类
    int catId = DatabaseManager::instance().addCategory(catName, parentCategoryId);
    if (catId <= 0) return 0;

    int count = 1; // 包含分类自身
    QDir dir(folderPath);

    // 2. 导入文件 (子项目不带剪贴板前缀)
    for (const QString& fileName : dir.entryList(QDir::Files)) {
        if (progress && progress->wasCanceled()) break;
        if (storeFile(dir.filePath(fileName), catId, progress, processedSize, false)) {
            count++;
        }
    }

    // 3. 递归导入子文件夹 (子分类不带剪贴板前缀)
    for (const QString& subDirName : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (progress && progress->wasCanceled()) break;
        count += importFolderRecursive(dir.filePath(subDirName), catId, progress, processedSize, false);
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
                 // 计算当前总进度的比例并映射到 0-1000 范围
                 qint64 total = progress->property("totalSize").toLongLong();
                 if (total > 0) {
                     int val = static_cast<int>((*processedSize * 1000) / total);
                     progress->setValue(val);
                 }
                 progress->setLabelText(QString("正在导入: %1").arg(info.fileName()));
            }
        }

        QFileInfo destInfo(destPath);
        QString relativePath = "attachments/" + destInfo.fileName();

        QString title = info.fileName();

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
