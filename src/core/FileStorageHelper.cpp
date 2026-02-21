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
#include <QTextStream>

int FileStorageHelper::processImport(const QStringList& paths, int targetCategoryId, bool fromClipboard) {
    if (paths.isEmpty()) return 0;

    QList<int> createdNoteIds;
    QList<int> createdCatIds;

    qint64 totalSize = calculateTotalSize(paths);
    qint64 processedSize = 0;
    
    FramelessProgressDialog* progress = nullptr;
    const qint64 threshold = 50 * 1024 * 1024; // 50MB

    if (totalSize >= threshold) {
        // 使用 1000 作为精度，防止 qint64 字节数超出进度条的 int 范围
        progress = new FramelessProgressDialog("导入进度", "正在导入文件和目录结构...", 0, 1000);
        progress->setProperty("totalSize", totalSize);
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
            // [CRITICAL] 无论拖拽到界面任何位置，文件夹始终作为顶级分类（parentId = -1）创建
            // 严格遵循用户要求：“只要拖拽文件夹到quickwindow或mainwindow界面的任何位置必须以新的分类来创建”
            totalCount += importFolderRecursive(path, -1, createdNoteIds, createdCatIds, progress, &processedSize, fromClipboard);
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

    // 2. 优先检查并导入 notes.csv (文本笔记)
    if (dir.exists("notes.csv")) {
        count += importFromCsv(dir.filePath("notes.csv"), catId, &createdNoteIds);
    }

    // 3. 导入物理文件 (子项目不带剪贴板前缀)
    for (const QString& fileName : dir.entryList(QDir::Files)) {
        if (progress && progress->wasCanceled()) break;
        if (fileName.toLower() == "notes.csv") continue; // 已处理

        if (storeFile(dir.filePath(fileName), catId, createdNoteIds, progress, processedSize, false)) {
            count++;
        }
    }

    // 3. 递归导入子文件夹 (子分类不带剪贴板前缀)
    for (const QString& subDirName : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (progress && progress->wasCanceled()) break;
        count += importFolderRecursive(dir.filePath(subDirName), catId, createdNoteIds, createdCatIds, progress, processedSize, false);
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

int FileStorageHelper::importFromCsv(const QString& csvPath, int targetCategoryId, QList<int>* createdNoteIds) {
    QFile file(csvPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return 0;

    QTextStream in(&file);
    in.setEncoding(QStringConverter::Utf8);
    int count = 0;
    QString fullContent = in.readAll();

    QStringList parts;
    QString current;
    bool inQuotes = false;
    bool isHeader = true;

    auto processRow = [&](const QStringList& rowParts) {
        if (rowParts.size() >= 2) {
            QString title = rowParts[0];
            QString content = rowParts[1];
            if (isHeader && title.toLower().contains("title") && content.toLower().contains("content")) {
                // 跳过表头
            } else {
                QStringList tags;
                if (rowParts.size() >= 3) {
                    // 兼容分号和逗号分隔的标签
                    tags = rowParts[2].split(QRegularExpression("[;,]"), Qt::SkipEmptyParts);
                }
                int nid = DatabaseManager::instance().addNote(title, content, tags, "", targetCategoryId);
                if (nid > 0) {
                    count++;
                    if (createdNoteIds) createdNoteIds->append(nid);
                }
            }
            isHeader = false;
        }
    };

    for (int i = 0; i < fullContent.length(); ++i) {
        QChar c = fullContent[i];
        if (c == '"') {
            if (inQuotes && i + 1 < fullContent.length() && fullContent[i+1] == '"') {
                current += '"';
                i++;
            } else {
                inQuotes = !inQuotes;
            }
        } else if (c == ',' && !inQuotes) {
            parts.append(current);
            current = "";
        } else if (c == '\n' && !inQuotes) {
            parts.append(current);
            current = "";
            processRow(parts);
            parts.clear();
        } else if (c == '\r' && !inQuotes) {
            // 跳过回车符
        } else {
            current += c;
        }
    }

    if (!parts.isEmpty() || !current.isEmpty()) {
        parts.append(current);
        processRow(parts);
    }
    return count;
}

bool FileStorageHelper::exportCategory(int categoryId, const QString& targetDir) {
    QList<QVariantMap> allCats = DatabaseManager::instance().getAllCategories();
    QString catName;
    if (categoryId >= 0) {
        for (const auto& c : allCats) {
            if (c["id"].toInt() == categoryId) {
                catName = c["name"].toString();
                break;
            }
        }
    } else {
        catName = "我的笔记导出";
    }

    QString currentDirPath = QDir(targetDir).filePath(catName);
    QDir().mkpath(currentDirPath);

    QList<QVariantMap> notes = DatabaseManager::instance().getNotesByCategory(categoryId);

    QList<QVariantMap> textNotes;
    for (const auto& note : notes) {
        QString type = note["item_type"].toString();
        // 如果是文件类，直接导出物理文件
        if (type == "local_file" || type == "local_folder" || type == "local_batch") {
            QString relativePath = note["content"].toString();
            QString fullPath = QCoreApplication::applicationDirPath() + "/" + relativePath;
            if (QFileInfo::exists(fullPath)) {
                QString destPath = getUniqueFilePath(currentDirPath, QFileInfo(fullPath).fileName());
                copyRecursively(fullPath, destPath);
            }
        } else if (type == "image") {
            QByteArray blob = note["data_blob"].toByteArray();
            if (!blob.isEmpty()) {
                QString fileName = note["title"].toString();
                fileName.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");
                if (!fileName.endsWith(".png", Qt::CaseInsensitive)) fileName += ".png";
                QString destPath = getUniqueFilePath(currentDirPath, fileName);
                QFile f(destPath);
                if (f.open(QIODevice::WriteOnly)) {
                    f.write(blob);
                    f.close();
                }
            }
        } else {
            // 纯文本类汇总到 CSV
            textNotes.append(note);
        }
    }

    if (!textNotes.isEmpty()) {
        QFile csvFile(currentDirPath + "/notes.csv");
        if (csvFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&csvFile);
            out.setEncoding(QStringConverter::Utf8);
            out << "title,content,tags\n";
            auto escape = [](QString s) {
                s.replace('"', "\"\"");
                if (s.contains(',') || s.contains('\n') || s.contains('"')) {
                    return "\"" + s + "\"";
                }
                return s;
            };
            for (const auto& note : textNotes) {
                out << escape(note["title"].toString()) << ","
                    << escape(note["content"].toString()) << ","
                    << escape(note["tags"].toString()) << "\n";
            }
            csvFile.close();
        }
    }

    // 递归导出子分类
    for (const auto& c : allCats) {
        int pid = c["parent_id"].isNull() ? -1 : c["parent_id"].toInt();
        if (pid == categoryId) {
            exportCategory(c["id"].toInt(), currentDirPath);
        }
    }

    return true;
}
