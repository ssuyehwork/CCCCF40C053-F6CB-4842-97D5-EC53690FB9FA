#include "FileStorageHelper.h"
#include "DatabaseManager.h"
#include "../ui/FramelessDialog.h"
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QApplication>

int FileStorageHelper::processImport(const QStringList& paths, int targetCategoryId, bool fromClipboard) {
    if (paths.isEmpty()) return 0;

    QList<int> createdNoteIds;
    QList<int> createdCatIds;

    ItemStats stats = calculateItemsStats(paths);
    qint64 processedSize = 0;
    
    FramelessProgressDialog* progress = nullptr;
    const qint64 sizeThreshold = 50 * 1024 * 1024; // 50MB
    const int countThreshold = 50; // 50个项目

    if (stats.totalSize >= sizeThreshold || stats.totalCount >= countThreshold) {
        // 使用 1000 作为精度，防止 qint64 字节数超出进度条的 int 范围
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
            // [CRITICAL] 文件夹导入遵循用户要求：创建为分类
            // 如果 targetCategoryId 为 -1，则作为顶级分类；否则作为其子分类。
            totalCount += importFolderRecursive(path, targetCategoryId, createdNoteIds, createdCatIds, progress, &processedSize, fromClipboard);
        } else if (info.suffix().toLower() == "csv") {
            // 独立 CSV 文件导入：直接解析为笔记
            totalCount += parseCsvFile(path, targetCategoryId, &createdNoteIds);
        } else {
            if (storeFile(path, targetCategoryId, createdNoteIds, progress, &processedSize, fromClipboard)) {
                totalCount++;
            }
        }

        if (progress && progress->wasCanceled()) {
            canceled = true;
            break;
        }
    }

    if (canceled) {
        qDebug() << "[Import] 正在回滚已导入的数据...";
        // 1. 清理物理文件
        for (int id : createdNoteIds) {
            QVariantMap note = DatabaseManager::instance().getNoteById(id);
            QString relativePath = note["content"].toString();
            if (note["item_type"].toString() == "local_file" && relativePath.startsWith("attachments/")) {
                QString fullPath = QCoreApplication::applicationDirPath() + "/" + relativePath;
                QFile::remove(fullPath);
            }
        }
        // 2. 清理数据库记录 (笔记)
        DatabaseManager::instance().deleteNotesBatch(createdNoteIds);
        // 3. 清理分类 (使用物理删除，回滚不应进入回收站)
        DatabaseManager::instance().hardDeleteCategories(createdCatIds);
        
        if (progress) delete progress;
        return 0;
    }

    if (progress) {
        progress->setValue(1000);
        delete progress;
    }

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
                                           QList<int>& createdNoteIds, QList<int>& createdCatIds,
                                           FramelessProgressDialog* progress, qint64* processedSize, bool fromClipboard) {
    QFileInfo info(folderPath);
    
    // 直接采用文件夹原始名称
    QString catName = info.fileName();

    // 1. 创建分类
    int catId = DatabaseManager::instance().addCategory(catName, parentCategoryId);
    if (catId <= 0) return 0;
    
    createdCatIds.append(catId);

    int count = 1; // 包含分类自身
    QDir dir(folderPath);

    // 优先处理 notes.csv (还原文本笔记)
    if (dir.exists("notes.csv")) {
        count += parseCsvFile(dir.filePath("notes.csv"), catId, &createdNoteIds);
    }

    // 2. 导入文件 (排除 notes.csv)
    for (const QString& fileName : dir.entryList(QDir::Files)) {
        if (progress && progress->wasCanceled()) break;
        if (fileName.toLower() == "notes.csv") continue;
        if (storeFile(dir.filePath(fileName), catId, createdNoteIds, progress, processedSize, false)) {
            count++;
        }
    }

    // 3. 递归导入子文件夹
    for (const QString& subDirName : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (progress && progress->wasCanceled()) break;
        count += importFolderRecursive(dir.filePath(subDirName), catId, createdNoteIds, createdCatIds, progress, processedSize, false);
    }

    return count;
}

int FileStorageHelper::parseCsvFile(const QString& csvPath, int catId, QList<int>* createdNoteIds) {
    QFile file(csvPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return 0;
    
    QString data = QString::fromUtf8(file.readAll());
    file.close();

    int count = 0;
    QList<QStringList> rows;
    QStringList currentRow;
    QString currentField;
    bool inQuotes = false;
    for (int i = 0; i < data.length(); ++i) {
        QChar c = data[i];
        if (inQuotes) {
            if (c == '"') {
                if (i + 1 < data.length() && data[i + 1] == '"') {
                    currentField += '"'; i++;
                } else inQuotes = false;
            } else currentField += c;
        } else {
            if (c == '"') inQuotes = true;
            else if (c == ',') { currentRow << currentField; currentField.clear(); }
            else if (c == '\n') { currentRow << currentField; rows << currentRow; currentRow.clear(); currentField.clear(); }
            else if (c == '\r') continue;
            else currentField += c;
        }
    }
    if (!currentRow.isEmpty() || !currentField.isEmpty()) { currentRow << currentField; rows << currentRow; }

    if (rows.size() > 1) {
        QStringList headers = rows[0];
        int idxTitle = -1, idxContent = -1, idxTags = -1;
        for(int i=0; i<headers.size(); ++i) {
            QString h = headers[i].trimmed().toLower();
            if(h == "title") idxTitle = i;
            else if(h == "content") idxContent = i;
            else if(h == "tags") idxTags = i;
        }

        for (int i = 1; i < rows.size(); ++i) {
            QStringList row = rows[i];
            QString title = (idxTitle != -1 && idxTitle < row.size()) ? row[idxTitle] : "导入笔记";
            QString content = (idxContent != -1 && idxContent < row.size()) ? row[idxContent] : "";
            QString tagsStr = (idxTags != -1 && idxTags < row.size()) ? row[idxTags] : "";
            if (title.isEmpty() && content.isEmpty()) continue;
            
            int noteId = DatabaseManager::instance().addNote(title, content, tagsStr.split(",", Qt::SkipEmptyParts), "", catId);
            if (noteId > 0) {
                if (createdNoteIds) createdNoteIds->append(noteId);
                count++;
            }
        }
    }
    return count;
}

bool FileStorageHelper::storeFile(const QString& path, int categoryId, 
                                QList<int>& createdNoteIds,
                                FramelessProgressDialog* progress, qint64* processedSize, bool fromClipboard) {
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

        int noteId = DatabaseManager::instance().addNote(
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

        if (noteId > 0) {
            createdNoteIds.append(noteId);
        } else {
            // 如果数据库记录插入失败，为了严谨，删除刚才拷贝的物理文件
            QFile::remove(destPath);
            ok = false;
        }
        
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
