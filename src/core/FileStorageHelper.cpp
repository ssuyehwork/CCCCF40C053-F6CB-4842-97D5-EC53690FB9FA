#include "FileStorageHelper.h"
#include "../db/Database.h"
#include "../db/ItemRepo.h"
#include "../db/CategoryRepo.h"
#include "FileCryptoHelper.h"
#include "FileResourceManager.h"
#include "../ui/FramelessDialog.h"
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QApplication>
#include <QFileDialog>
#include <QTextStream>
#include <QRegularExpression>
#include <QStringConverter>
#include <functional>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QBuffer>
#include "../ui/ToolTipOverlay.h"
#include <QSqlDatabase>

static bool copyRecursively(const QString& srcPath, const QString& dstPath) {
    QFileInfo srcInfo(srcPath);
    if (srcInfo.isDir()) {
        if (!QDir().mkpath(dstPath)) return false;
        QDir srcDir(srcPath);
        QStringList entries = srcDir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString& entry : entries) {
            if (!copyRecursively(srcPath + "/" + entry, dstPath + "/" + entry)) return false;
        }
        return true;
    } else {
        return QFile::copy(srcPath, dstPath);
    }
}

/**
 * @brief 通用导出逻辑：将物理项列表导出到指定目录
 */
static void exportItemsToDirectory(const QStringList& itemPaths, const QString& exportPath, FramelessProgressDialog* progress = nullptr, int* processedCount = nullptr) {
    QDir().mkpath(exportPath);

    QSet<QString> usedFileNames;
    for (const auto& path : itemPaths) {
        if (progress && progress->wasCanceled()) break;

        QFileInfo info(path);
        if (!info.exists()) continue;

        if (progress && processedCount) {
            progress->setValue((*processedCount)++);
            progress->setLabelText(QString("正在导出: %1").arg(info.fileName().left(30)));
        }

        QString finalName = info.fileName();
        int i = 1;
        while (usedFileNames.contains(finalName.toLower())) {
            finalName = info.suffix().isEmpty() ? info.completeBaseName() + QString(" (%1)").arg(i++) : info.completeBaseName() + QString(" (%1)").arg(i++) + "." + info.suffix();
        }
        usedFileNames.insert(finalName.toLower());

        if (info.isFile()) {
            QFile::copy(path, exportPath + "/" + finalName);
        } else {
            copyRecursively(path, exportPath + "/" + finalName);
        }

        QCoreApplication::processEvents();
    }
}

int FileStorageHelper::processImport(const QStringList& paths, int targetCategoryId, bool fromClipboard) {
    if (paths.isEmpty()) return 0;

    // [OPTIMIZATION] 开启事务模式
    QSqlDatabase db = ArcMeta::Database::instance().getThreadDatabase();
    db.transaction();

    QList<QString> createdItemPaths;
    QList<int> createdCatIds;

    ItemStats stats = calculateItemsStats(paths);
    qint64 processedSize = 0;
    
    FramelessProgressDialog* progress = nullptr;
    const qint64 sizeThreshold = 50 * 1024 * 1024; // 50MB
    const int countThreshold = 50; // 50个项目

    if (stats.totalSize >= sizeThreshold || stats.totalCount >= countThreshold) {
        progress = new FramelessProgressDialog("导入进度", "正在导入文件和目录结构...", 0, 1000);
        progress->setProperty("totalSize", stats.totalSize);
        progress->setWindowModality(Qt::WindowModal);
        progress->show();
    }

    bool canceled = false;
    int totalCount = 0;
    for (const QString& path : paths) {
        if (progress && progress->wasCanceled()) {
            canceled = true;
            break;
        }

        QFileInfo info(path);
        if (info.isDir()) {
            totalCount += importFolderRecursive(path, targetCategoryId, createdItemPaths, createdCatIds, progress, &processedSize, fromClipboard);
        } else if (info.suffix().toLower() == "csv") {
            totalCount += parseCsvFile(path, targetCategoryId, &createdItemPaths);
        } else {
            QString outDestPath;
            if (storeFile(path, targetCategoryId, outDestPath, progress, &processedSize, fromClipboard)) {
                createdItemPaths.append(outDestPath);
                totalCount++;
            }
        }

        if (progress && progress->wasCanceled()) {
            canceled = true;
            break;
        }
    }

    if (canceled) {
        qDebug() << "[Import] 正在清理物理文件...";
        for (const QString& path : createdItemPaths) {
            QFileInfo fi(path);
            if (fi.isFile()) QFile::remove(path);
            else QDir(path).removeRecursively();
        }
        db.rollback();
        
        if (progress) delete progress;
        return 0;
    }

    if (progress) {
        progress->setValue(1000);
        delete progress;
    }

    db.commit();
    return totalCount;
}

FileStorageHelper::ItemStats FileStorageHelper::calculateItemsStats(const QStringList& paths) {
    ItemStats stats;
    for (const QString& path : paths) {
        QFileInfo info(path);
        stats.totalCount++;
        if (info.isDir()) {
            QDir dir(path);
            QStringList subPaths;
            for (const auto& entry : dir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
                subPaths << dir.absoluteFilePath(entry);
            }
            ItemStats subStats = calculateItemsStats(subPaths);
            stats.totalSize += subStats.totalSize;
            stats.totalCount += subStats.totalCount;
        } else {
            stats.totalSize += info.size();
        }
    }
    return stats;
}

int FileStorageHelper::importFolderRecursive(const QString& folderPath, int parentCategoryId, 
                                           QList<QString>& createdItemPaths, QList<int>& createdCatIds,
                                           FramelessProgressDialog* progress, qint64* processedSize, bool fromClipboard) {
    QFileInfo info(folderPath);
    QString catName = info.fileName();

    // 1. 创建分类 (ArcMeta Repo)
    ArcMeta::Category cat;
    cat.parentId = parentCategoryId;
    cat.name = catName.toStdWString();
    if (!ArcMeta::CategoryRepo::add(cat)) return 0;
    
    int catId = cat.id;
    createdCatIds.append(catId);

    int count = 1;
    QDir dir(folderPath);

    // 2. 导入文件
    for (const QString& fileName : dir.entryList(QDir::Files)) {
        if (progress && progress->wasCanceled()) break;
        QString outDestPath;
        if (storeFile(dir.filePath(fileName), catId, outDestPath, progress, processedSize, false)) {
            createdItemPaths.append(outDestPath);
            count++;
        }
    }

    // 3. 递归导入子文件夹
    for (const QString& subDirName : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (progress && progress->wasCanceled()) break;
        count += importFolderRecursive(dir.filePath(subDirName), catId, createdItemPaths, createdCatIds, progress, processedSize, false);
    }

    return count;
}

int FileStorageHelper::parseCsvFile(const QString& csvPath, int catId, QList<QString>* createdItemPaths) {
    // 2026-04-04 按照用户重构要求：CSV 导入逻辑仅作为参考保留，物理资源管理器不再将 CSV 行转换为数据库笔记。
    // 这里暂时返回 0，后续可根据需求适配为将 CSV 记录导出为物理 .txt 或 .md 文件。
    return 0;
}

bool FileStorageHelper::storeFile(const QString& path, int categoryId, 
                                QString& outDestPath,
                                FramelessProgressDialog* progress, qint64* processedSize, bool fromClipboard) {
    QFileInfo info(path);
    QString storageDir = getStorageRoot();
    QString destPath = getUniqueFilePath(storageDir, info.fileName());
    
    if (QFile::copy(path, destPath)) {
        outDestPath = destPath;
        if (processedSize) {
            *processedSize += info.size();
            if (progress) {
                 qint64 total = progress->property("totalSize").toLongLong();
                 if (total > 0) {
                     int val = static_cast<int>((*processedSize * 1000) / total);
                     progress->setValue(val);
                 }
                 progress->setLabelText(QString("正在导入: %1").arg(info.fileName()));
            }
        }

        // 建立分类关联
        ArcMeta::CategoryRepo::addItemToCategory(categoryId, destPath.toStdWString());

        // 持久化物理元数据
        ArcMeta::ItemMeta meta;
        meta.type = info.isDir() ? L"folder" : L"file";
        meta.originalName = info.fileName().toStdWString();
        // 设置一个默认的 tags 避免 untagged 统计误判
        meta.tags = { L"导入文件" };
        ArcMeta::FileResourceManager::instance().setItemMeta(destPath, meta);
        
        QApplication::processEvents();
        return true;
    }
    
    return false;
}

QString FileStorageHelper::getStorageRoot() {
    QString path = QCoreApplication::applicationDirPath() + "/attachments";
    QDir dir(path);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    return path;
}

void FileStorageHelper::exportCategory(int catId, const QString& catName, QWidget* parent) {
    QString dir = QFileDialog::getExistingDirectory(parent, "选择导出目录", "");
    if (dir.isEmpty()) return;

    QString safeCatName = catName;
    safeCatName.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");
    QString exportPath = dir + "/" + safeCatName;

    std::vector<std::wstring> wpaths = ArcMeta::CategoryRepo::getItemPathsInCategory(catId);
    if (wpaths.empty()) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #e67e22;'>[!] 导出失败：当前分类下没有物理项目</b>");
        return;
    }

    QStringList paths;
    for(const auto& wp : wpaths) paths << QString::fromStdWString(wp);

    int processedCount = 0;
    FramelessProgressDialog* progress = nullptr;
    if (paths.size() > 50) {
        progress = new FramelessProgressDialog("导出进度", "正在导出资源及元数据...", 0, paths.size(), parent);
        progress->show();
    }

    exportItemsToDirectory(paths, exportPath, progress, &processedCount);

    if (progress) delete progress;
    ToolTipOverlay::instance()->showText(QCursor::pos(), QString("<b style='color: #2ecc71;'>[OK] 分类 [%1] 导出完成</b>").arg(catName));
}

void FileStorageHelper::exportByFilter(const QString& filterType, const QVariant& filterValue, const QString& exportName, QWidget* parent) {
    // 2026-04-04 资源管理器重构：由 UI 传参通过 CategoryRepo 统一获取路径进行物理导出
    QString dir = QFileDialog::getExistingDirectory(parent, QString("选择导出目录 [%1]").arg(exportName), "");
    if (dir.isEmpty()) return;

    // 目前仅处理分类 ID 类型的过滤，其余系统分类逻辑需对接新的查询接口
    if (filterType == "category") {
        exportCategory(filterValue.toInt(), exportName, parent);
    }
}

void FileStorageHelper::exportFullStructure(QWidget* parent) {
    // 按照分类树结构物理导出，此处逻辑略去，待后续 Repo 提供 Tree 查询后补全
    ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #e67e22;'>[!] 导出功能升级中...</b>");
}

void FileStorageHelper::exportCategoryRecursive(int catId, const QString& catName, QWidget* parent) {
    QString dir = QFileDialog::getExistingDirectory(parent, "选择递归导出目录", "");
    if (dir.isEmpty()) return;

    QString safeCatName = catName;
    safeCatName.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");
    QString exportPath = dir + "/" + safeCatName;

    std::function<void(int, const QString&)> doExport = [&](int currentCatId, const QString& currentPath) {
        std::vector<std::wstring> wpaths = ArcMeta::CategoryRepo::getItemPathsInCategory(currentCatId);
        if (!wpaths.empty()) {
            QStringList paths;
            for(const auto& wp : wpaths) paths << QString::fromStdWString(wp);
            exportItemsToDirectory(paths, currentPath);
        }

        // 递归子分类 (由于 Repo 目前未提供 getChildrenIds，需先 getAll 过滤)
        auto allCats = ArcMeta::CategoryRepo::getAll();
        for (const auto& c : allCats) {
            if (c.parentId == currentCatId) {
                QString safeSubName = QString::fromStdWString(c.name).replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");
                doExport(c.id, currentPath + "/" + safeSubName);
            }
        }
    };

    doExport(catId, exportPath);
    ToolTipOverlay::instance()->showText(QCursor::pos(), QString("<b style='color: #2ecc71;'>[OK] 递归导出分类 [%1] 完成</b>").arg(catName));
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

void FileStorageHelper::exportToPackage(int catId, const QString& catName, QWidget* parent) {
    ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #e67e22;'>[!] 物理数据包导出功能重构中...</b>");
}

void FileStorageHelper::importFromPackage(QWidget* parent) {
    ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #e67e22;'>[!] 物理数据包导入功能重构中...</b>");
}
