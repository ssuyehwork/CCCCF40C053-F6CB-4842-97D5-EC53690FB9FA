#include "DatabaseManager.h"
#include <QDebug>
#include <QSqlRecord>
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QDir>
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QFileInfo>
#include <QStandardPaths>
#include <QSettings>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <utility>
#include <algorithm>
#include "FileCryptoHelper.h"
#include "HardwareInfoHelper.h"
#include "ClipboardMonitor.h"
#include "../ui/StringUtils.h"
#include "../ui/FramelessDialog.h"

DatabaseManager& DatabaseManager::instance() {
    static DatabaseManager inst;
    return inst;
}

QStringList DatabaseManager::s_tagClipboard;
QMutex DatabaseManager::s_tagClipboardMutex;

void DatabaseManager::setTagClipboard(const QStringList& tags) {
    QMutexLocker locker(&s_tagClipboardMutex);
    s_tagClipboard = tags;
}

QStringList DatabaseManager::getTagClipboard() {
    QMutexLocker locker(&s_tagClipboardMutex);
    return s_tagClipboard;
}

DatabaseManager::DatabaseManager(QObject* parent) : QObject(parent) {
    QSettings settings("RapidNotes", "QuickWindow");
    m_autoCategorizeEnabled = settings.value("autoCategorizeClipboard", false).toBool();
}

void DatabaseManager::setAutoCategorizeEnabled(bool enabled) {
    if (m_autoCategorizeEnabled != enabled) {
        m_autoCategorizeEnabled = enabled;
        QSettings settings("RapidNotes", "QuickWindow");
        settings.setValue("autoCategorizeClipboard", enabled);
        emit autoCategorizeEnabledChanged(enabled);
    }
}

void DatabaseManager::setActiveCategoryId(int id) {
    if (m_activeCategoryId != id) {
        m_activeCategoryId = id;
        emit activeCategoryIdChanged(id);
    }
}

DatabaseManager::~DatabaseManager() {
    if (m_db.isOpen()) {
        m_db.close();
    }
}

bool DatabaseManager::init(const QString& dbPath) {
    QMutexLocker locker(&m_mutex);
    
    // 1. 确定路径
    // 外壳路径: 程序目录下的 inspiration.db
    m_realDbPath = dbPath; 
    
    // 内核路径: 用户 AppData 目录 (隐藏路径，用户通常不会去删这里)
    QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (appDataPath.isEmpty()) {
        appDataPath = QCoreApplication::applicationDirPath() + "/data";
    }
    QDir().mkpath(appDataPath);
    m_dbPath = appDataPath + "/rapidnotes_kernel.db";
    
    qDebug() << "[DB] 外壳路径 (Shell):" << m_realDbPath;
    qDebug() << "[DB] 内核路径 (Kernel):" << m_dbPath;

    // 1.5 启动时强行清理残留的内核数据库，防止上次异常退出导致脏数据被读取
    if (QFile::exists(m_dbPath)) {
        qDebug() << "[DB] 发现残留内核文件，正在清理...";
        
        // 【关键修复】确保 Qt 没有在其他地方隐式打开连接导致文件被占用
        QString connName = "RapidNotes_Main_Conn";
        if (QSqlDatabase::contains(connName)) {
            QSqlDatabase::database(connName).close();
            QSqlDatabase::removeDatabase(connName);
        }
        // 如果有默认连接也一并移除
        if (QSqlDatabase::contains(QSqlDatabase::defaultConnection)) {
            QSqlDatabase::database(QSqlDatabase::defaultConnection).close();
            QSqlDatabase::removeDatabase(QSqlDatabase::defaultConnection);
        }

        // 尝试解除只读或隐藏属性限制（Windows权限问题）
        QFile::setPermissions(m_dbPath, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadUser | QFile::WriteUser);
        
        // 清理可能存在的 SQLite WAL/SHM 残留文件
        QString walPath = m_dbPath + "-wal";
        QString shmPath = m_dbPath + "-shm";
        if (QFile::exists(walPath)) {
            QFile::setPermissions(walPath, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadUser | QFile::WriteUser);
            QFile::remove(walPath);
        }
        if (QFile::exists(shmPath)) {
            QFile::setPermissions(shmPath, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadUser | QFile::WriteUser);
            QFile::remove(shmPath);
        }

        if (!QFile::remove(m_dbPath)) {
            qCritical() << "[DB] 无法清理残留的内核文件，程序将退出！";
            QMessageBox::critical(nullptr, "严重错误", "检测到上次运行异常，但无法清理残留的数据库内核文件（可能由于权限不足或文件被占用）。\n为了保护您的数据安全，程序将退出。\n\n尝试方法：\n1. 请检查是否有其他进程（如按键精灵、杀毒软件等）占用了该文件。\n2. 尝试【以管理员身份运行】该软件。\n3. 重启计算机后再试。");
            exit(-1);
        }
    }

    // 2. 自动迁移逻辑 (Legacy support)
    QString legacyDbPath = QFileInfo(m_realDbPath).absolutePath() + "/notes.db";
    if (!QFile::exists(m_realDbPath) && QFile::exists(legacyDbPath) && !QFile::exists(m_dbPath)) {
        qDebug() << "[DB] 检测到旧版 notes.db，且无新版内核，正在自动迁移至新的三层保护体系...";
        if (QFile::copy(legacyDbPath, m_dbPath)) {
            qDebug() << "[DB] 旧版数据已拷贝至内核，且已安全擦除原始明文数据库。";
            FileCryptoHelper::secureDelete(legacyDbPath);
        }
    }

    // 3. 解壳加载逻辑
    bool kernelExists = QFile::exists(m_dbPath); // 由于上面已经强行删除了，此刻一定为 false (除非 legacy 迁移)
    bool shellExists = QFile::exists(m_realDbPath);

    if (kernelExists) {
        // [HEALING] 检测到残留内核，强制执行启动前修复
        qDebug() << "[DB] 检测到残留内核文件 (可能是上次异常退出)，正在执行强制启动修复逻辑...";
        
        // 1. 尝试将残留内核加密备份回主外壳 (尽可能挽救数据)
        QString key = FileCryptoHelper::getCombinedKey();
        if (FileCryptoHelper::encryptFileWithShell(m_dbPath, m_realDbPath, key)) {
            qDebug() << "[DB] 残留内核数据已成功恢复至主外壳。";
        } else {
            qWarning() << "[DB] 无法从中恢复数据 (文件可能已损坏)，跳过备份。";
        }

        // 2. 强制删除残留内核，确保本次启动环境干净且无占用
        if (FileCryptoHelper::secureDelete(m_dbPath)) {
            qDebug() << "[DB] 残留内核已彻底清除。";
        }
        
        // 重置标志，后续流程将进入从外壳加载的路径
        kernelExists = false;
        shellExists = QFile::exists(m_realDbPath);
    }

    if (shellExists) {
        qDebug() << "[DB] 发现外壳文件，尝试加载...";
        
        QString key = FileCryptoHelper::getCombinedKey();
        
        if (FileCryptoHelper::decryptFileWithShell(m_realDbPath, m_dbPath, key)) {
            qDebug() << "[DB] 现代解密成功。";
        } else {
            qDebug() << "[DB] 现代解密失败 (未发现魔数标记)，尝试旧版解密 (Legacy)...";
            if (FileCryptoHelper::decryptFileLegacy(m_realDbPath, m_dbPath, key)) {
                qDebug() << "[DB] 旧版解密成功。";
            } else {
                qDebug() << "[DB] 旧版解密也失败 (密码错误或数据损坏)，尝试明文检测...";
                QFile file(m_realDbPath);
                if (file.open(QIODevice::ReadOnly)) {
                    QByteArray header = file.read(16);
                    file.close();
                    if (header.startsWith("SQLite format 3")) {
                        qDebug() << "[DB] 检测到明文数据库，执行直接加载。";
                        QFile::copy(m_realDbPath, m_dbPath);
                    } else {
                        qCritical() << "[DB] 外壳文件已损坏或格式完全无法识别。";
                        return false;
                    }
                } else {
                    qCritical() << "[DB] 无法读取外壳文件。";
                    return false;
                }
            }
        }
    } else {
        qDebug() << "[DB] 未发现现有数据库及内核，将创建新数据库。";
    }

    // 4. 打开数据库
    if (m_db.isOpen()) m_db.close();
    
    QString connectionName = "RapidNotes_Main_Conn";
    if (QSqlDatabase::contains(connectionName)) {
        m_db = QSqlDatabase::database(connectionName);
    } else {
        m_db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
    }
    
    m_db.setDatabaseName(m_dbPath);

    if (!m_db.open()) {
        qCritical() << "无法打开数据库内核:" << m_db.lastError().text();
        return false;
    }

    if (!createTables()) return false;

    return true;
}

void DatabaseManager::closeAndPack() {
    QMutexLocker locker(&m_mutex);
    
    QString connName = m_db.connectionName();
    if (m_db.isOpen()) {
        m_db.close();
    }
    m_db = QSqlDatabase(); 
    if (!connName.isEmpty()) {
        QSqlDatabase::removeDatabase(connName);
    }
    
    if (QFile::exists(m_dbPath)) {
        qDebug() << "[DB] 正在执行退出合壳 (将内核加密保存至外壳文件)...";
        if (FileCryptoHelper::encryptFileWithShell(m_dbPath, m_realDbPath, FileCryptoHelper::getCombinedKey())) {
            if (QFile::exists(m_realDbPath) && QFileInfo(m_realDbPath).size() > 0) {
                if (FileCryptoHelper::secureDelete(m_dbPath)) {
                    qDebug() << "[DB] 合壳完成，安全擦除内核文件。";
                    backupDatabase();
                }
            }
        } else {
            qCritical() << "[DB] 合壳失败！数据保留在内核文件中。";
        }
    }
}

bool DatabaseManager::saveKernelToShell() {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;
    
    qDebug() << "[DB] 正在执行强制合壳 (中间状态保存)...";
    
    // 为了确保内核文件被完整刷入磁盘且可读，暂时关闭连接
    m_db.close();
    
    bool success = FileCryptoHelper::encryptFileWithShell(m_dbPath, m_realDbPath, FileCryptoHelper::getCombinedKey());
    
    // 重新打开连接以供后续使用
    if (!m_db.open()) {
        qCritical() << "[DB] saveKernelToShell 后无法重新打开数据库内核！";
        return false;
    }
    
    if (success) {
        qDebug() << "[DB] 中间状态已成功保存至外壳文件。";
    } else {
        qCritical() << "[DB] 中间状态保存失败！";
    }
    
    return success;
}

void DatabaseManager::backupDatabase() {
    if (m_realDbPath.isEmpty() || !QFile::exists(m_realDbPath)) return;

    QFileInfo dbInfo(m_realDbPath);
    QDir dbDir = dbInfo.dir();
    QString backupDirPath = dbDir.absoluteFilePath("backups");
    QDir backupDir(backupDirPath);

    if (!backupDir.exists()) {
        if (!dbDir.mkdir("backups")) {
            qWarning() << "[DB] 无法创建备份目录:" << backupDirPath;
            return;
        }
    }

    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString backupFileName = QString("inspiration_backup_%1.db").arg(timestamp);
    QString backupPath = backupDir.absoluteFilePath(backupFileName);

    if (QFile::copy(m_realDbPath, backupPath)) {
        qDebug() << "[DB] 数据库备份成功:" << backupPath;
    } else {
        qWarning() << "[DB] 数据库备份失败";
        return;
    }

    // 数量控制：保留最近 10 个备份
    QStringList filter;
    filter << "inspiration_backup_*.db";
    // 按名称排序（时间戳文件名按名称排序即为时间顺序）
    QFileInfoList backupFiles = backupDir.entryInfoList(filter, QDir::Files, QDir::Name);

    while (backupFiles.size() > 10) {
        QFileInfo oldest = backupFiles.takeFirst();
        if (QFile::remove(oldest.absoluteFilePath())) {
            qDebug() << "[DB] 已移除旧备份:" << oldest.fileName();
        }
    }
}

bool DatabaseManager::createTables() {
    QSqlQuery query(m_db);
    QString createNotesTable = R"(
        CREATE TABLE IF NOT EXISTS notes (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            title TEXT,
            content TEXT,
            tags TEXT,
            color TEXT DEFAULT '#2d2d2d',
            category_id INTEGER,
            item_type TEXT DEFAULT 'text',
            data_blob BLOB,
            content_hash TEXT,
            rating INTEGER DEFAULT 0,
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
            updated_at DATETIME DEFAULT CURRENT_TIMESTAMP,
            is_pinned INTEGER DEFAULT 0,
            is_locked INTEGER DEFAULT 0,
            is_favorite INTEGER DEFAULT 0,
            is_deleted INTEGER DEFAULT 0,
            source_app TEXT,
            source_title TEXT,
            last_accessed_at DATETIME
        )
    )";
    if (!query.exec(createNotesTable)) return false;

    QString createCategoriesTable = R"(
        CREATE TABLE IF NOT EXISTS categories (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,
            parent_id INTEGER,
            color TEXT DEFAULT '#808080',
            sort_order INTEGER DEFAULT 0,
            preset_tags TEXT,
            password TEXT,
            password_hint TEXT,
            is_deleted INTEGER DEFAULT 0
        )
    )";
    if (query.exec(createCategoriesTable)) {
        // 尝试迁移：为旧表增加 is_deleted 字段
        QSqlQuery check(m_db);
        if (check.exec("PRAGMA table_info(categories)")) {
            bool hasDeleted = false;
            while (check.next()) {
                if (check.value(1).toString() == "is_deleted") {
                    hasDeleted = true;
                    break;
                }
            }
            if (!hasDeleted) {
                query.exec("ALTER TABLE categories ADD COLUMN is_deleted INTEGER DEFAULT 0");
            }
        }
    }
    query.exec("CREATE TABLE IF NOT EXISTS tags (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT UNIQUE NOT NULL)");
    query.exec("CREATE TABLE IF NOT EXISTS note_tags (note_id INTEGER, tag_id INTEGER, PRIMARY KEY (note_id, tag_id))");
    query.exec("CREATE INDEX IF NOT EXISTS idx_notes_content_hash ON notes(content_hash)");
    // [CRITICAL] FTS5 索引表维护：必须确保 notes_fts 包含 title, content, tags 三个核心搜索字段。
    // 检查 FTS 表是否包含 tags 字段，如果不包含则重建 (用于从旧 FTS 版本迁移)
    bool hasTagsColumn = false;
    if (query.exec("PRAGMA table_info(notes_fts)")) {
        while (query.next()) {
            if (query.value(1).toString() == "tags") {
                hasTagsColumn = true;
                break;
            }
        }
    }
    if (!hasTagsColumn) {
        query.exec("DROP TABLE IF EXISTS notes_fts");
    }

    QString createFtsTable = R"(
        CREATE VIRTUAL TABLE IF NOT EXISTS notes_fts USING fts5(
            title, content, tags, content='notes', content_rowid='id'
        )
    )";
    query.exec(createFtsTable);
    
    // 如果是新建或重建，初始化索引数据
    if (!hasTagsColumn) {
        query.exec("INSERT INTO notes_fts(rowid, title, content, tags) SELECT id, title, content, tags FROM notes WHERE is_deleted = 0");
    }

    // 试用期与使用次数表
    query.exec("CREATE TABLE IF NOT EXISTS system_config (key TEXT PRIMARY KEY, value TEXT)");
    
    // 初始化试用信息
    QSqlQuery checkLaunch(m_db);
    checkLaunch.prepare("SELECT value FROM system_config WHERE key = 'first_launch_date'");
    if (checkLaunch.exec() && !checkLaunch.next()) {
        QSqlQuery initQuery(m_db);
        initQuery.prepare("INSERT INTO system_config (key, value) VALUES ('first_launch_date', :date)");
        initQuery.bindValue(":date", QDateTime::currentDateTime().toString(Qt::ISODate));
        initQuery.exec();
        
        initQuery.prepare("INSERT INTO system_config (key, value) VALUES ('usage_count', '0')");
        initQuery.exec();
    }

    // [CRITICAL] 待办事项表：扩展支持联动、循环和子任务。
    QString createTodosTable = R"(
        CREATE TABLE IF NOT EXISTS todos (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            title TEXT NOT NULL,
            content TEXT,
            start_time DATETIME,
            end_time DATETIME,
            status INTEGER DEFAULT 0, -- 0:待办, 1:已完成, 2:已逾期
            reminder_time DATETIME,
            priority INTEGER DEFAULT 0,
            color TEXT,
            note_id INTEGER DEFAULT -1,
            repeat_mode INTEGER DEFAULT 0, -- 0:None, 1:Daily, 2:Weekly, 3:Monthly
            parent_id INTEGER DEFAULT -1,
            progress INTEGER DEFAULT 0,
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
            updated_at DATETIME DEFAULT CURRENT_TIMESTAMP
        )
    )";
    if (query.exec(createTodosTable)) {
        // 增量升级逻辑
        QSqlQuery upgrade(m_db);
        QStringList newCols = {"note_id", "repeat_mode", "parent_id", "progress"};
        for (const auto& col : newCols) {
            upgrade.exec(QString("ALTER TABLE todos ADD COLUMN %1 INTEGER DEFAULT 0").arg(col));
        }
    }

    return true;
}

int DatabaseManager::addNote(const QString& title, const QString& content, const QStringList& tags,
                            const QString& color, int categoryId,
                            const QString& itemType, const QByteArray& dataBlob,
                            const QString& sourceApp, const QString& sourceTitle) {
    // 试用限制检查
    QVariantMap trial = getTrialStatus();
    if (trial["expired"].toBool() || trial["usage_limit_reached"].toBool()) {
        qWarning() << "[DB] 试用已结束或达到使用上限，停止新增灵感。";
        return 0;
    }

    QVariantMap newNoteMap;
    bool success = false;
    QString currentTime = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    QByteArray hashData = dataBlob.isEmpty() ? content.toUtf8() : dataBlob;
    QString contentHash = QCryptographicHash::hash(hashData, QCryptographicHash::Sha256).toHex();
    {   
        QMutexLocker locker(&m_mutex);
        if (!m_db.isOpen()) return 0;

        QString finalColor = color.isEmpty() ? "#2d2d2d" : color;
        QStringList finalTags = tags;

        // 查重：如果内容已存在，则更新标题、标签及分类
        QSqlQuery checkQuery(m_db);
        checkQuery.prepare("SELECT id, category_id, tags FROM notes WHERE content_hash = :hash AND is_deleted = 0 LIMIT 1");
        checkQuery.bindValue(":hash", contentHash);
        if (checkQuery.exec() && checkQuery.next()) {
            int existingId = checkQuery.value(0).toInt();
            QVariant oldCatVal = checkQuery.value(1);
            
            // 获取已有笔记的详细信息，用于智能判定是否需要更新标题等
            QVariantMap existingNote = getNoteById(existingId);
            QString existingTagsStr = existingNote.value("tags").toString();
            QStringList existingTags = existingTagsStr.split(",", Qt::SkipEmptyParts);
            for(QString& t : existingTags) t = t.trimmed();

            // 智能合并标签
            for (const QString& t : std::as_const(finalTags)) {
                if (!existingTags.contains(t.trimmed())) existingTags << t.trimmed();
            }

            // 漂移保护逻辑：如果笔记已有明确分类，则优先保留原分类，防止在自动归档时发生分类位移
            int finalCatToUse = categoryId;
            if (!oldCatVal.isNull() && oldCatVal.toInt() > 0) {
                finalCatToUse = oldCatVal.toInt(); 
            }

            // 获取新分类/旧分类的颜色
            QString finalColor = color;
            
            if (finalCatToUse != -1) {
                QSqlQuery catQuery(m_db);
                catQuery.prepare("SELECT color, preset_tags FROM categories WHERE id = :id");
                catQuery.bindValue(":id", finalCatToUse);
                if (catQuery.exec() && catQuery.next()) {
                    if (color.isEmpty()) finalColor = catQuery.value(0).toString();
                    QString preset = catQuery.value(1).toString();
                    if (!preset.isEmpty()) {
                        QStringList pTags = preset.split(",", Qt::SkipEmptyParts);
                        for (const QString& t : pTags) {
                            if (!existingTags.contains(t.trimmed())) existingTags << t.trimmed();
                        }
                    }
                }
            }

            QSqlQuery updateQuery(m_db);
            // 重复内容时，更新标签、时间及来源
            QString sql = "UPDATE notes SET tags = :tags, updated_at = :now, source_app = :app, source_title = :stitle, category_id = :cat_id";
            if (!finalColor.isEmpty()) sql += ", color = :color";
            
            // [CRITICAL] 智能标题保护逻辑：禁止恢复“旧版全量覆盖标题”的傻逼行为。
            // 必须确保：仅当原标题是自动生成的通用标题，且新标题更有意义时才覆盖；否则必须保持笔记原始标题不变。
            QString existingTitle = existingNote.value("title").toString();
            bool isExistingGeneric = existingTitle.isEmpty() || existingTitle == "无标题灵感" || 
                                     existingTitle.startsWith("[截图]") || 
                                     existingTitle.startsWith("[截图]") ||
                                     existingTitle.startsWith("Copied ");
            bool isNewMeaningful = !title.isEmpty() && !title.startsWith("[拖入") && !title.startsWith("[图片");
            
            if (isExistingGeneric && isNewMeaningful && existingTitle != title) {
                sql += ", title = :title";
            }
            
            sql += " WHERE id = :id";

            updateQuery.prepare(sql);
            updateQuery.bindValue(":tags", existingTags.join(", "));
            updateQuery.bindValue(":now", currentTime);
            updateQuery.bindValue(":app", sourceApp);
            updateQuery.bindValue(":stitle", sourceTitle);
            updateQuery.bindValue(":cat_id", finalCatToUse == -1 ? QVariant(QMetaType::fromType<int>()) : finalCatToUse);
            if (!finalColor.isEmpty()) updateQuery.bindValue(":color", finalColor);
            if (sql.contains(":title")) updateQuery.bindValue(":title", title);
            updateQuery.bindValue(":id", existingId);
            
            if (updateQuery.exec()) success = true;
            if (success) { 
                locker.unlock(); 
                emit noteUpdated(); 
                return existingId; 
            }
        }
        if (categoryId != -1) {
            QSqlQuery catQuery(m_db);
            catQuery.prepare("SELECT color, preset_tags FROM categories WHERE id = :id");
            catQuery.bindValue(":id", categoryId);
            if (catQuery.exec() && catQuery.next()) {
                if (color.isEmpty()) finalColor = catQuery.value(0).toString();
                QString preset = catQuery.value(1).toString();
                if (!preset.isEmpty()) {
                    QStringList pTags = preset.split(",", Qt::SkipEmptyParts);
                    for (const QString& t : pTags) {
                        QString trimmed = t.trimmed();
                        if (!finalTags.contains(trimmed)) finalTags << trimmed;
                    }
                }
            }
        }
        QSqlQuery query(m_db);
        query.prepare("INSERT INTO notes (title, content, tags, color, category_id, item_type, data_blob, content_hash, created_at, updated_at, source_app, source_title) VALUES (:title, :content, :tags, :color, :category_id, :item_type, :data_blob, :hash, :created_at, :updated_at, :source_app, :source_title)");
        query.bindValue(":title", title);
        query.bindValue(":content", content);
        
        QStringList cleanedFinalTags;
        for (const QString& t : finalTags) {
            QString tr = t.trimmed();
            if (!tr.isEmpty() && !cleanedFinalTags.contains(tr)) cleanedFinalTags << tr;
        }
        query.bindValue(":tags", cleanedFinalTags.join(", "));
        
        query.bindValue(":color", finalColor);
        query.bindValue(":category_id", categoryId == -1 ? QVariant(QMetaType::fromType<int>()) : categoryId);
        query.bindValue(":item_type", itemType);
        query.bindValue(":data_blob", dataBlob);
        query.bindValue(":hash", contentHash);
        query.bindValue(":created_at", currentTime);
        query.bindValue(":updated_at", currentTime);
        query.bindValue(":source_app", sourceApp);
        query.bindValue(":source_title", sourceTitle);
        if (query.exec()) {
            success = true;
            QVariant lastId = query.lastInsertId();
            QSqlQuery fetch(m_db);
            fetch.prepare("SELECT * FROM notes WHERE id = :id");
            fetch.bindValue(":id", lastId);
            if (fetch.exec() && fetch.next()) {
                QSqlRecord rec = fetch.record();
                for (int i = 0; i < rec.count(); ++i) newNoteMap[rec.fieldName(i)] = fetch.value(i);
            }
        }
    }
    if (success && !newNoteMap.isEmpty()) {
        int newId = newNoteMap["id"].toInt();
        syncFts(newId, title, content, newNoteMap["tags"].toString());
        incrementUsageCount(); // 每次增加笔记视为一次使用
        emit noteAdded(newNoteMap);
        return newId;
    }
    return 0;
}

bool DatabaseManager::updateNote(int id, const QString& title, const QString& content, const QStringList& tags, const QString& color, int categoryId) {
    bool success = false;
    QString currentTime = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    {
        QMutexLocker locker(&m_mutex);
        if (!m_db.isOpen()) return false;
        QSqlQuery query(m_db);
        
        // [PROFESSIONAL] 修正分类逻辑：确保能够正常移动至“未分类”(-1)，并同步更新颜色
        QString sql = "UPDATE notes SET title=:title, content=:content, tags=:tags, updated_at=:updated_at, category_id=:category_id, color=:color";
        sql += " WHERE id=:id";

        query.prepare(sql);
        query.bindValue(":title", title);
        query.bindValue(":content", content);
        
        QStringList trimmedTags;
        for (const QString& t : tags) {
            QString tr = t.trimmed();
            if (!tr.isEmpty() && !trimmedTags.contains(tr)) trimmedTags << tr;
        }
        query.bindValue(":tags", trimmedTags.join(", "));
        
        query.bindValue(":updated_at", currentTime);
        query.bindValue(":category_id", categoryId == -1 ? QVariant() : categoryId);
        
        QString finalColor = color;
        if (finalColor.isEmpty()) {
            if (categoryId != -1) {
                QSqlQuery catQuery(m_db);
                catQuery.prepare("SELECT color FROM categories WHERE id = :id");
                catQuery.bindValue(":id", categoryId);
                if (catQuery.exec() && catQuery.next()) finalColor = catQuery.value(0).toString();
                else finalColor = "#0A362F";
            } else {
                finalColor = "#0A362F";
            }
        }
        query.bindValue(":color", finalColor);
        query.bindValue(":id", id);
        success = query.exec();
    }
    if (success) { 
        QStringList trimmedTags;
        for (const QString& t : tags) {
            QString tr = t.trimmed();
            if (!tr.isEmpty() && !trimmedTags.contains(tr)) trimmedTags << tr;
        }
        syncFts(id, title, content, trimmedTags.join(", ")); 
        emit noteUpdated(); 
    }
    return success;
}

bool DatabaseManager::reorderCategories(int parentId, bool ascending) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;
    QSqlQuery query(m_db);
    if (parentId <= 0) query.prepare("SELECT id, name FROM categories WHERE parent_id IS NULL OR parent_id <= 0");
    else { query.prepare("SELECT id, name FROM categories WHERE parent_id = :pid"); query.bindValue(":pid", parentId); }
    if (!query.exec()) return false;
    struct CatInfo { int id; QString name; };
    QList<CatInfo> list;
    while (query.next()) list.append({query.value(0).toInt(), query.value(1).toString()});
    if (list.isEmpty()) return true;
    std::sort(list.begin(), list.end(), [ascending](const CatInfo& a, const CatInfo& b) {
        if (ascending) return a.name.localeAwareCompare(b.name) < 0;
        return a.name.localeAwareCompare(b.name) > 0;
    });
    m_db.transaction();
    QSqlQuery update(m_db);
    for (int i = 0; i < list.size(); ++i) {
        update.prepare("UPDATE categories SET sort_order = :val WHERE id = :id");
        update.bindValue(":val", i);
        update.bindValue(":id", list[i].id);
        update.exec();
    }
    bool ok = m_db.commit();
    if (ok) emit categoriesChanged();
    return ok;
}

bool DatabaseManager::updateCategoryOrder(int parentId, const QList<int>& categoryIds) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;
    if (!m_db.transaction()) return false;
    QSqlQuery query(m_db);
    query.prepare("UPDATE categories SET parent_id = :pid, sort_order = :order WHERE id = :id");
    for (int i = 0; i < categoryIds.size(); ++i) {
        query.bindValue(":pid", parentId <= 0 ? QVariant() : parentId);
        query.bindValue(":order", i);
        query.bindValue(":id", categoryIds[i]);
        if (!query.exec()) { m_db.rollback(); return false; }
    }
    bool ok = m_db.commit();
    if (ok) emit categoriesChanged();
    return ok;
}

bool DatabaseManager::reorderAllCategories(bool ascending) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;
    QSqlQuery query(m_db);
    query.exec("SELECT DISTINCT parent_id FROM categories");
    QList<int> parents;
    bool hasRoot = false;
    while (query.next()) {
        if (query.value(0).isNull() || query.value(0).toInt() <= 0) hasRoot = true;
        else parents.append(query.value(0).toInt());
    }
    bool success = true;
    if (hasRoot) success &= reorderCategories(-1, ascending);
    for (int pid : parents) success &= reorderCategories(pid, ascending);
    return success;
}

bool DatabaseManager::setCategoryPassword(int id, const QString& password, const QString& hint) {
    bool success = false;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_db.isOpen()) return false;
        QString hashedPassword = QString(QCryptographicHash::hash(password.toUtf8(), QCryptographicHash::Sha256).toHex());
        QSqlQuery query(m_db);
        query.prepare("UPDATE categories SET password=:password, password_hint=:hint WHERE id=:id");
        query.bindValue(":password", hashedPassword);
        query.bindValue(":hint", hint);
        query.bindValue(":id", id);
        success = query.exec();
    }
    if (success) emit categoriesChanged();
    return success;
}

bool DatabaseManager::removeCategoryPassword(int id) {
    bool success = false;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_db.isOpen()) return false;
        QSqlQuery query(m_db);
        query.prepare("UPDATE categories SET password=NULL, password_hint=NULL WHERE id=:id");
        query.bindValue(":id", id);
        success = query.exec();
        if (success) m_unlockedCategories.remove(id);
    }
    if (success) emit categoriesChanged();
    return success;
}

bool DatabaseManager::verifyCategoryPassword(int id, const QString& password) {
    bool correct = false;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_db.isOpen()) return false;
        QString hashedPassword = QString(QCryptographicHash::hash(password.toUtf8(), QCryptographicHash::Sha256).toHex());
        QSqlQuery query(m_db);
        query.prepare("SELECT password FROM categories WHERE id=:id");
        query.bindValue(":id", id);
        if (query.exec() && query.next()) {
            if (query.value(0).toString() == hashedPassword) correct = true;
        }
    }
    if (correct) unlockCategory(id);
    return correct;
}

bool DatabaseManager::isCategoryLocked(int id) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;
    if (m_unlockedCategories.contains(id)) return false;
    QSqlQuery query(m_db);
    query.prepare("SELECT password FROM categories WHERE id=:id");
    query.bindValue(":id", id);
    if (query.exec() && query.next()) return !query.value(0).toString().isEmpty();
    return false;
}

void DatabaseManager::lockCategory(int id) { { QMutexLocker locker(&m_mutex); m_unlockedCategories.remove(id); } emit categoriesChanged(); }
void DatabaseManager::unlockCategory(int id) { { QMutexLocker locker(&m_mutex); m_unlockedCategories.insert(id); } emit categoriesChanged(); }

bool DatabaseManager::restoreAllFromTrash() {
    bool success = false;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_db.isOpen()) return false;
        m_db.transaction();
        
        QSqlQuery query(m_db);
        // 恢复所有分类
        query.exec("UPDATE categories SET is_deleted = 0 WHERE is_deleted = 1");
        // 恢复所有笔记，并恢复默认颜色（如果原分类已不存在，这部分逻辑在获取颜色时会处理）
        success = query.exec("UPDATE notes SET is_deleted = 0, updated_at = datetime('now','localtime') WHERE is_deleted = 1");
        
        success = m_db.commit();
    }
    if (success) { emit noteUpdated(); emit categoriesChanged(); }
    return success;
}

bool DatabaseManager::updateNoteState(int id, const QString& column, const QVariant& value) {
    bool success = false;
    QString title, content, tags;
    bool needsFts = false;
    QString currentTime = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    {
        QMutexLocker locker(&m_mutex);
        if (!m_db.isOpen()) return false;
        // [CRITICAL] 必须包含 item_type 以支持从图片识别提取的文字类型标记
        QStringList allowedColumns = {"is_pinned", "is_locked", "is_favorite", "is_deleted", "tags", "rating", "category_id", "color", "content", "title", "item_type"};
        if (!allowedColumns.contains(column)) return false;
        QSqlQuery query(m_db);
        if (column == "is_favorite") {
            bool fav = value.toBool();
            QString color = fav ? "#ff6b81" : ""; 
            if (!fav) {
                QSqlQuery catQuery(m_db);
                catQuery.prepare("SELECT c.color FROM categories c JOIN notes n ON n.category_id = c.id WHERE n.id = :id");
                catQuery.bindValue(":id", id);
                if (catQuery.exec() && catQuery.next()) color = catQuery.value(0).toString();
                else color = "#0A362F"; 
            }
            query.prepare("UPDATE notes SET is_favorite = :val, color = :color, updated_at = :now WHERE id = :id");
            query.bindValue(":color", color);
        } else if (column == "is_deleted") {
            bool del = value.toBool();
            QString color = del ? "#2d2d2d" : "#0A362F";
            query.prepare("UPDATE notes SET is_deleted = :val, color = :color, category_id = NULL, updated_at = :now WHERE id = :id");
            query.bindValue(":color", color);
        } else if (column == "category_id") {
            int catId = value.isNull() ? -1 : value.toInt();
            QString color = "#0A362F"; 
            if (catId != -1) {
                QSqlQuery catQuery(m_db);
                catQuery.prepare("SELECT color FROM categories WHERE id = :id");
                catQuery.bindValue(":id", catId);
                if (catQuery.exec() && catQuery.next()) color = catQuery.value(0).toString();
            }
            query.prepare("UPDATE notes SET category_id = :val, color = :color, is_deleted = 0, updated_at = :now WHERE id = :id");
            query.bindValue(":color", color);
        } else {
            query.prepare(QString("UPDATE notes SET %1 = :val, updated_at = :now WHERE id = :id").arg(column));
        }
        query.bindValue(":val", value);
        query.bindValue(":now", currentTime);
        query.bindValue(":id", id);
        success = query.exec();
        if (success && (column == "content" || column == "title" || column == "tags")) {
            needsFts = true;
            QSqlQuery fetch(m_db);
            fetch.prepare("SELECT title, content, tags FROM notes WHERE id = ?");
            fetch.addBindValue(id);
            if (fetch.exec() && fetch.next()) { 
                title = fetch.value(0).toString(); 
                content = fetch.value(1).toString(); 
                tags = fetch.value(2).toString();
            }
        }
    } 
    if (success) { if (needsFts) syncFts(id, title, content, tags); emit noteUpdated(); }
    return success;
}

bool DatabaseManager::updateNoteStateBatch(const QList<int>& ids, const QString& column, const QVariant& value) {
    if (ids.isEmpty()) return true;
    bool success = false;
    QString currentTime = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    {
        QMutexLocker locker(&m_mutex);
        if (!m_db.isOpen()) return false;
        // [CRITICAL] 保持与 updateNoteState 相同的允许列白名单，确保功能不丢失
        QStringList allowedColumns = {"is_pinned", "is_locked", "is_favorite", "is_deleted", "tags", "rating", "category_id", "color", "content", "title", "item_type"};
        if (!allowedColumns.contains(column)) return false;
        m_db.transaction();
        QSqlQuery query(m_db);
        if (column == "category_id") {
            int catId = value.isNull() ? -1 : value.toInt();
            QString color = "#0A362F";
            if (catId != -1) {
                QSqlQuery catQuery(m_db);
                catQuery.prepare("SELECT color FROM categories WHERE id = :id");
                catQuery.bindValue(":id", catId);
                if (catQuery.exec() && catQuery.next()) color = catQuery.value(0).toString();
            }
            query.prepare("UPDATE notes SET category_id = :val, color = :color, is_deleted = 0, updated_at = :now WHERE id = :id");
            for (int id : ids) {
                query.bindValue(":val", value);
                query.bindValue(":color", color);
                query.bindValue(":now", currentTime);
                query.bindValue(":id", id);
                query.exec();
            }
        } else if (column == "is_favorite") {
            bool fav = value.toBool();
            if (fav) {
                query.prepare("UPDATE notes SET is_favorite = 1, color = '#ff6b81', updated_at = :now WHERE id = :id");
            } else {
                // 恢复各笔记所属分类的颜色
                query.prepare("UPDATE notes SET is_favorite = 0, color = COALESCE((SELECT color FROM categories WHERE id = notes.category_id), '#0A362F'), updated_at = :now WHERE id = :id");
            }
            for (int id : ids) {
                query.bindValue(":now", currentTime);
                query.bindValue(":id", id);
                query.exec();
            }
        } else if (column == "is_deleted") {
            bool del = value.toBool();
            if (del) {
                query.prepare("UPDATE notes SET is_deleted = 1, color = '#2d2d2d', category_id = NULL, is_pinned = 0, is_favorite = 0, updated_at = :now WHERE id = :id");
            } else {
                query.prepare("UPDATE notes SET is_deleted = 0, color = '#0A362F', updated_at = :now WHERE id = :id");
            }
            for (int id : ids) {
                query.bindValue(":now", currentTime);
                query.bindValue(":id", id);
                query.exec();
            }
        } else {
            QString sql = QString("UPDATE notes SET %1 = :val, updated_at = :now WHERE id = :id").arg(column);
            query.prepare(sql);
            for (int id : ids) {
                query.bindValue(":val", value);
                query.bindValue(":now", currentTime);
                query.bindValue(":id", id);
                query.exec();
            }
        }
        success = m_db.commit();
    }
    if (success) {
        for (int id : ids) syncFtsById(id);
        emit noteUpdated();
    }
    return success;
}

bool DatabaseManager::recordAccess(int id) {
    bool success = false;
    QString currentTime = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    {
        QMutexLocker locker(&m_mutex);
        if (!m_db.isOpen()) return false;
        QSqlQuery query(m_db);
        query.prepare("UPDATE notes SET last_accessed_at = :now WHERE id = :id");
        query.bindValue(":now", currentTime);
        query.bindValue(":id", id);
        success = query.exec();
    }
    return success;
}

bool DatabaseManager::toggleNoteState(int id, const QString& column) {
    QVariant currentVal;
    {
        QMutexLocker locker(&m_mutex);
        QSqlQuery query(m_db);
        query.prepare(QString("SELECT %1 FROM notes WHERE id = :id").arg(column));
        query.bindValue(":id", id);
        if (query.exec() && query.next()) currentVal = query.value(0);
    }
    if (currentVal.isValid()) return updateNoteState(id, column, !currentVal.toBool());
    return false;
}

bool DatabaseManager::moveNotesToCategory(const QList<int>& noteIds, int catId) {
    if (noteIds.isEmpty()) return true;
    bool success = false;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_db.isOpen()) return false;
        m_db.transaction();
        QString catColor = "#0A362F"; 
        QString presetTags;
        if (catId != -1) {
            QSqlQuery catQuery(m_db);
            catQuery.prepare("SELECT color, preset_tags FROM categories WHERE id = :id");
            catQuery.bindValue(":id", catId);
            if (catQuery.exec() && catQuery.next()) { catColor = catQuery.value(0).toString(); presetTags = catQuery.value(1).toString(); }
        }
        QSqlQuery query(m_db);
        query.prepare("UPDATE notes SET category_id = :cat_id, color = :color, is_deleted = 0, updated_at = :now WHERE id = :id");
        QString now = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
        for (int id : noteIds) {
            query.bindValue(":cat_id", catId == -1 ? QVariant() : catId);
            query.bindValue(":color", catColor);
            query.bindValue(":now", now);
            query.bindValue(":id", id);
            query.exec();
            if (!presetTags.isEmpty()) {
                QSqlQuery fetchTags(m_db);
                fetchTags.prepare("SELECT tags FROM notes WHERE id = :id");
                fetchTags.bindValue(":id", id);
                if (fetchTags.exec() && fetchTags.next()) {
                    QString existing = fetchTags.value(0).toString();
                    QStringList tagList = existing.split(",", Qt::SkipEmptyParts);
                    QStringList newTags = presetTags.split(",", Qt::SkipEmptyParts);
                    bool changed = false;
                    for (const QString& t : newTags) { if (!tagList.contains(t.trimmed())) { tagList.append(t.trimmed()); changed = true; } }
                    if (changed) { QSqlQuery updateTags(m_db); updateTags.prepare("UPDATE notes SET tags = :tags WHERE id = :id"); updateTags.bindValue(":tags", tagList.join(", ")); updateTags.bindValue(":id", id); updateTags.exec(); }
                }
            }
        }
        success = m_db.commit();
    }
    if (success) {
        for (int id : noteIds) syncFtsById(id);
        emit noteUpdated();
    }
    return success;
}

bool DatabaseManager::deleteNotesBatch(const QList<int>& ids) {
    if (ids.isEmpty()) return true;
    bool success = false;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_db.isOpen()) return false;
        m_db.transaction();
        QSqlQuery query(m_db);
        query.prepare("DELETE FROM notes WHERE id=:id");
        for (int id : ids) { query.bindValue(":id", id); if (query.exec()) removeFts(id); }
        success = m_db.commit();
    }
    if (success) {
        ClipboardMonitor::instance().clearLastHash();
        emit noteUpdated();
    }
    return success;
}

bool DatabaseManager::softDeleteNotes(const QList<int>& ids) {
    if (ids.isEmpty()) return true;
    bool success = false;
    QString currentTime = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    {
        QMutexLocker locker(&m_mutex);
        if (!m_db.isOpen()) return false;
        m_db.transaction();
        QSqlQuery query(m_db);
        // [MODIFIED] 不再清除 category_id，以便后续分类恢复或笔记原位恢复
        query.prepare("UPDATE notes SET is_deleted = 1, color = '#2d2d2d', is_pinned = 0, is_favorite = 0, updated_at = :now WHERE id = :id");
        for (int id : ids) { query.bindValue(":now", currentTime); query.bindValue(":id", id); query.exec(); }
        success = m_db.commit();
    }
    if (success) {
        ClipboardMonitor::instance().clearLastHash();
        emit noteUpdated();
    }
    return success;
}

void DatabaseManager::addNoteAsync(const QString& title, const QString& content, const QStringList& tags, const QString& color, int categoryId, const QString& itemType, const QByteArray& dataBlob, const QString& sourceApp, const QString& sourceTitle) {
    QMetaObject::invokeMethod(this, [this, title, content, tags, color, categoryId, itemType, dataBlob, sourceApp, sourceTitle]() { addNote(title, content, tags, color, categoryId, itemType, dataBlob, sourceApp, sourceTitle); }, Qt::QueuedConnection);
}

// [CRITICAL] 核心搜索逻辑：采用 FTS5 全文检索。禁止修改此处的 MATCH 语法及字段关联，以确保搜索结果的准确性与高性能。
QList<QVariantMap> DatabaseManager::searchNotes(const QString& keyword, const QString& filterType, const QVariant& filterValue, int page, int pageSize, const QVariantMap& criteria) {
    QMutexLocker locker(&m_mutex);
    QList<QVariantMap> results;
    if (!m_db.isOpen()) return results;

    // [NEW] 处理回收站特殊视图：包含已删除的分类
    if (filterType == "trash" && keyword.isEmpty()) {
        QString sql = R"(
            SELECT id, title, content, tags, color, category_id, item_type, data_blob, created_at, updated_at, is_pinned, is_locked, is_favorite, is_deleted, source_app, source_title, last_accessed_at 
            FROM notes WHERE is_deleted = 1
            UNION ALL
            SELECT id, name as title, '(已删除的分类包)' as content, '' as tags, color, parent_id as category_id, 'deleted_category' as item_type, NULL as data_blob, NULL as created_at, NULL as updated_at, 0 as is_pinned, 0 as is_locked, 0 as is_favorite, 1 as is_deleted, '' as source_app, '' as source_title, NULL as last_accessed_at
            FROM categories WHERE is_deleted = 1
            ORDER BY is_pinned DESC, updated_at DESC
        )";
        QSqlQuery query(m_db);
        if (query.exec(sql)) {
            while (query.next()) {
                QVariantMap map;
                QSqlRecord rec = query.record();
                for (int i = 0; i < rec.count(); ++i) map[rec.fieldName(i)] = query.value(i);
                results.append(map);
            }
        }
        return results;
    }

    QString baseSql = "SELECT notes.* FROM notes ";
    if (!keyword.isEmpty()) {
        // [OPTIMIZED] 使用 FTS5 进行全文搜索，显著提升大数据量下的检索速度与相关性排序
        baseSql = "SELECT notes.* FROM notes JOIN notes_fts ON notes.id = notes_fts.rowid ";
    }

    QString whereClause;
    QVariantList params;
    applyCommonFilters(whereClause, params, filterType, filterValue, criteria);
    
    if (!keyword.isEmpty()) {
        whereClause += "AND notes_fts MATCH ? ";
        params << sanitizeFtsKeyword(keyword);
    }
    
    QString finalSql = baseSql + whereClause + "ORDER BY ";
    if (!keyword.isEmpty()) { 
        // FTS 模式下优先使用 rank (相关性)
        finalSql += "notes_fts.rank, is_pinned DESC, updated_at DESC"; 
    } else {
        if (filterType == "recently_visited") finalSql += "is_pinned DESC, last_accessed_at DESC";
        else finalSql += "is_pinned DESC, updated_at DESC";
    }
    
    if (page > 0 && filterType != "trash") finalSql += QString(" LIMIT %1 OFFSET %2").arg(pageSize).arg((page - 1) * pageSize);
    
    QSqlQuery query(m_db);
    query.prepare(finalSql);
    for (int i = 0; i < params.size(); ++i) query.bindValue(i, params[i]);
    
    if (query.exec()) { 
        while (query.next()) { 
            QVariantMap map; 
            QSqlRecord rec = query.record(); 
            for (int i = 0; i < rec.count(); ++i) map[rec.fieldName(i)] = query.value(i); 
            results.append(map); 
        } 
    }
    else qCritical() << "searchNotes failed:" << query.lastError().text();
    return results;
}

// [CRITICAL] 核心计数逻辑：必须与 searchNotes 的过滤条件保持 1:1 同步，禁止擅自改动。
int DatabaseManager::getNotesCount(const QString& keyword, const QString& filterType, const QVariant& filterValue, const QVariantMap& criteria) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return 0;

    if (filterType == "trash" && keyword.isEmpty()) {
        QSqlQuery q(m_db);
        int count = 0;
        if (q.exec("SELECT COUNT(*) FROM notes WHERE is_deleted = 1")) { if (q.next()) count += q.value(0).toInt(); }
        if (q.exec("SELECT COUNT(*) FROM categories WHERE is_deleted = 1")) { if (q.next()) count += q.value(0).toInt(); }
        return count;
    }

    QString baseSql = "SELECT COUNT(*) FROM notes ";
    if (!keyword.isEmpty()) {
        baseSql = "SELECT COUNT(*) FROM notes JOIN notes_fts ON notes.id = notes_fts.rowid ";
    }

    QString whereClause;
    QVariantList params;
    applyCommonFilters(whereClause, params, filterType, filterValue, criteria);
    
    if (!keyword.isEmpty()) {
        whereClause += "AND notes_fts MATCH ? ";
        params << sanitizeFtsKeyword(keyword);
    }
    
    QSqlQuery query(m_db);
    query.prepare(baseSql + whereClause);
    for (int i = 0; i < params.size(); ++i) query.bindValue(i, params[i]);
    if (query.exec()) { if (query.next()) return query.value(0).toInt(); }
    else qCritical() << "getNotesCount failed:" << query.lastError().text();
    return 0;
}

QStringList DatabaseManager::getAllTags() {
    QMutexLocker locker(&m_mutex);
    QStringList allTags;
    if (!m_db.isOpen()) return allTags;
    QSqlQuery query(m_db);
    if (query.exec("SELECT tags FROM notes WHERE tags != '' AND is_deleted = 0")) {
        while (query.next()) {
            QString tagsStr = query.value(0).toString();
            QStringList parts = tagsStr.split(QRegularExpression("[,，]"), Qt::SkipEmptyParts);
            for (const QString& part : parts) {
                QString trimmed = part.trimmed();
                if (!trimmed.isEmpty() && !allTags.contains(trimmed)) allTags.append(trimmed);
            }
        }
    }
    allTags.sort();
    return allTags;
}

QList<QVariantMap> DatabaseManager::getRecentTagsWithCounts(int limit) {
    QMutexLocker locker(&m_mutex);
    QList<QVariantMap> results;
    if (!m_db.isOpen()) return results;
    struct TagData { QString name; int count = 0; QDateTime lastUsed; };
    QMap<QString, TagData> tagMap;
    QSqlQuery query(m_db);
    if (query.exec("SELECT tags, updated_at FROM notes WHERE tags != '' AND is_deleted = 0")) {
        while (query.next()) {
            QString tagsStr = query.value(0).toString();
            QDateTime updatedAt = query.value(1).toDateTime();
            QStringList parts = tagsStr.split(QRegularExpression("[,，]"), Qt::SkipEmptyParts);
            for (const QString& part : parts) {
                QString name = part.trimmed();
                if (name.isEmpty()) continue;
                if (!tagMap.contains(name)) tagMap[name] = {name, 1, updatedAt};
                else {
                    tagMap[name].count++;
                    if (updatedAt > tagMap[name].lastUsed) tagMap[name].lastUsed = updatedAt;
                }
            }
        }
    }
    QList<TagData> sortedList = tagMap.values();
    std::sort(sortedList.begin(), sortedList.end(), [](const TagData& a, const TagData& b) { if (a.lastUsed != b.lastUsed) return a.lastUsed > b.lastUsed; return a.count > b.count; });
    int actualLimit = qMin(limit, (int)sortedList.size());
    for (int i = 0; i < actualLimit; ++i) { QVariantMap m; m["name"] = sortedList[i].name; m["count"] = sortedList[i].count; results.append(m); }
    return results;
}

int DatabaseManager::addCategory(const QString& name, int parentId, const QString& color) {
    int lastId = -1;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_db.isOpen()) return -1;
        int maxOrder = 0;
        QSqlQuery orderQuery(m_db);
        if (parentId == -1) orderQuery.exec("SELECT MAX(sort_order) FROM categories WHERE parent_id IS NULL OR parent_id = -1");
        else { orderQuery.prepare("SELECT MAX(sort_order) FROM categories WHERE parent_id = :pid"); orderQuery.bindValue(":pid", parentId); orderQuery.exec(); }
        if (orderQuery.next()) maxOrder = orderQuery.value(0).toInt();
        QString chosenColor = color;
        if (chosenColor.isEmpty()) { static const QStringList palette = { "#FF6B6B", "#4ECDC4", "#45B7D1", "#96CEB4", "#FFEEAD", "#D4A5A5", "#9B59B6", "#3498DB", "#E67E22", "#2ECC71", "#E74C3C", "#F1C40F", "#1ABC9C", "#34495E", "#95A5A6" }; chosenColor = palette.at(QRandomGenerator::global()->bounded(palette.size())); }
        QSqlQuery query(m_db);
        query.prepare("INSERT INTO categories (name, parent_id, color, sort_order) VALUES (:name, :parent_id, :color, :sort_order)");
        query.bindValue(":name", name);
        query.bindValue(":parent_id", parentId == -1 ? QVariant(QMetaType::fromType<int>()) : parentId);
        query.bindValue(":color", chosenColor);
        query.bindValue(":sort_order", maxOrder + 1);
        if (query.exec()) lastId = query.lastInsertId().toInt();
    }
    if (lastId != -1) emit categoriesChanged();
    return lastId;
}

bool DatabaseManager::renameCategory(int id, const QString& name) {
    bool success = false;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_db.isOpen()) return false;
        QSqlQuery query(m_db);
        query.prepare("UPDATE categories SET name=:name WHERE id=:id");
        query.bindValue(":name", name);
        query.bindValue(":id", id);
        success = query.exec();
    }
    if (success) emit categoriesChanged();
    return success;
}

bool DatabaseManager::setCategoryColor(int id, const QString& color) {
    bool success = false;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_db.isOpen()) return false;
        m_db.transaction();
        QSqlQuery treeQuery(m_db);
        treeQuery.prepare(R"(WITH RECURSIVE category_tree(id) AS (SELECT :id UNION ALL SELECT c.id FROM categories c JOIN category_tree ct ON c.parent_id = ct.id) SELECT id FROM category_tree)");
        treeQuery.bindValue(":id", id);
        QList<int> allIds;
        if (treeQuery.exec()) { while (treeQuery.next()) allIds << treeQuery.value(0).toInt(); }
        if (!allIds.isEmpty()) {
            QString placeholders;
            for(int i=0; i<allIds.size(); ++i) placeholders += (i==0 ? "?" : ",?");
            QSqlQuery updateNotes(m_db);
            updateNotes.prepare(QString("UPDATE notes SET color = ? WHERE category_id IN (%1)").arg(placeholders));
            updateNotes.addBindValue(color);
            for(int cid : allIds) updateNotes.addBindValue(cid);
            updateNotes.exec();
            QSqlQuery updateCats(m_db);
            updateCats.prepare(QString("UPDATE categories SET color = ? WHERE id IN (%1)").arg(placeholders));
            updateCats.addBindValue(color);
            for(int cid : allIds) updateCats.addBindValue(cid);
            updateCats.exec();
        }
        success = m_db.commit();
    }
    if (success) { emit categoriesChanged(); emit noteUpdated(); }
    return success;
}

bool DatabaseManager::deleteCategory(int id) {
    // 默认执行软删除，以符合用户通过 Del 键将其移至回收站的要求
    return softDeleteCategories({id});
}

bool DatabaseManager::hardDeleteCategories(const QList<int>& ids) {
    if (ids.isEmpty()) return true;
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;

    m_db.transaction();
    QSqlQuery query(m_db);
    QStringList placeholders;
    for (int i = 0; i < ids.size(); ++i) placeholders << "?";

    query.prepare(QString("DELETE FROM categories WHERE id IN (%1)").arg(placeholders.join(",")));
    for (int id : ids) query.addBindValue(id);

    bool ok = query.exec();
    if (ok) {
        m_db.commit();
        emit categoriesChanged();
    } else {
        m_db.rollback();
        qWarning() << "[DB] hardDeleteCategories failed:" << query.lastError().text();
    }
    return ok;
}

bool DatabaseManager::softDeleteCategories(const QList<int>& ids) {
    if (ids.isEmpty()) return true;
    bool success = false;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_db.isOpen()) return false;
        m_db.transaction();
        
        QSqlQuery query(m_db);
        for (int id : ids) {
            // 使用递归 CTE 找到所有子分类 ID
            QSqlQuery treeQuery(m_db);
            treeQuery.prepare(R"(
                WITH RECURSIVE category_tree(id) AS (
                    SELECT :id
                    UNION ALL
                    SELECT c.id FROM categories c JOIN category_tree ct ON c.parent_id = ct.id
                ) SELECT id FROM category_tree
            )");
            treeQuery.bindValue(":id", id);
            QList<int> allIds;
            if (treeQuery.exec()) {
                while (treeQuery.next()) allIds << treeQuery.value(0).toInt();
            }

            if (!allIds.isEmpty()) {
                QStringList placeholders;
                for(int i=0; i<allIds.size(); ++i) placeholders << "?";
                QString joined = placeholders.join(",");

                // 1. 标记分类为已删除
                QSqlQuery delCat(m_db);
                delCat.prepare(QString("UPDATE categories SET is_deleted = 1 WHERE id IN (%1)").arg(joined));
                for(int cid : allIds) delCat.addBindValue(cid);
                delCat.exec();

                // 2. 标记所属笔记为已删除 (保留 category_id)
                QSqlQuery delNotes(m_db);
                delNotes.prepare(QString("UPDATE notes SET is_deleted = 1, updated_at = datetime('now','localtime') WHERE category_id IN (%1)").arg(joined));
                for(int cid : allIds) delNotes.addBindValue(cid);
                delNotes.exec();
            }
        }
        success = m_db.commit();
    }
    if (success) {
        emit categoriesChanged();
        emit noteUpdated();
    }
    return success;
}

bool DatabaseManager::restoreCategories(const QList<int>& ids) {
    if (ids.isEmpty()) return true;
    bool success = false;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_db.isOpen()) return false;
        m_db.transaction();
        
        QSqlQuery query(m_db);
        for (int id : ids) {
            // 同样递归找到所有子项，确保整树恢复
            QSqlQuery treeQuery(m_db);
            treeQuery.prepare(R"(
                WITH RECURSIVE category_tree(id) AS (
                    SELECT :id
                    UNION ALL
                    SELECT c.id FROM categories c JOIN category_tree ct ON c.parent_id = ct.id
                ) SELECT id FROM category_tree
            )");
            treeQuery.bindValue(":id", id);
            QList<int> allIds;
            if (treeQuery.exec()) {
                while (treeQuery.next()) allIds << treeQuery.value(0).toInt();
            }

            if (!allIds.isEmpty()) {
                QStringList placeholders;
                for(int i=0; i<allIds.size(); ++i) placeholders << "?";
                QString joined = placeholders.join(",");

                // 1. 恢复分类
                QSqlQuery resCat(m_db);
                resCat.prepare(QString("UPDATE categories SET is_deleted = 0 WHERE id IN (%1)").arg(joined));
                for(int cid : allIds) resCat.addBindValue(cid);
                resCat.exec();

                // 2. 恢复笔记
                QSqlQuery resNotes(m_db);
                resNotes.prepare(QString("UPDATE notes SET is_deleted = 0, updated_at = datetime('now','localtime') WHERE category_id IN (%1)").arg(joined));
                for(int cid : allIds) resNotes.addBindValue(cid);
                resNotes.exec();
            }
        }
        success = m_db.commit();
    }
    if (success) {
        emit categoriesChanged();
        emit noteUpdated();
    }
    return success;
}

bool DatabaseManager::moveCategory(int id, MoveDirection direction) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;
    int parentId = -1;
    QSqlQuery parentQuery(m_db);
    parentQuery.prepare("SELECT parent_id FROM categories WHERE id = :id");
    parentQuery.bindValue(":id", id);
    if (parentQuery.exec() && parentQuery.next()) parentId = parentQuery.value(0).isNull() ? -1 : parentQuery.value(0).toInt();
    else return false;
    QSqlQuery siblingsQuery(m_db);
    if (parentId == -1) siblingsQuery.prepare("SELECT id FROM categories WHERE parent_id IS NULL OR parent_id = -1 ORDER BY sort_order ASC");
    else { siblingsQuery.prepare("SELECT id FROM categories WHERE parent_id = :pid ORDER BY sort_order ASC"); siblingsQuery.bindValue(":pid", parentId); }
    if (!siblingsQuery.exec()) return false;
    QList<int> siblings;
    while (siblingsQuery.next()) siblings << siblingsQuery.value(0).toInt();
    int currentIndex = siblings.indexOf(id);
    if (currentIndex == -1) return false;
    switch (direction) {
        case Up: if (currentIndex > 0) std::swap(siblings[currentIndex], siblings[currentIndex - 1]); else return false; break;
        case Down: if (currentIndex < siblings.size() - 1) std::swap(siblings[currentIndex], siblings[currentIndex + 1]); else return false; break;
        case Top: if (currentIndex > 0) { siblings.removeAt(currentIndex); siblings.prepend(id); } else return false; break;
        case Bottom: if (currentIndex < siblings.size() - 1) { siblings.removeAt(currentIndex); siblings.append(id); } else return false; break;
    }
    return updateCategoryOrder(parentId, siblings);
}

QList<QVariantMap> DatabaseManager::getAllCategories() {
    QMutexLocker locker(&m_mutex);
    QList<QVariantMap> results;
    if (!m_db.isOpen()) return results;
    QSqlQuery query(m_db);
    // [MODIFIED] 仅加载未删除的分类
    if (query.exec("SELECT * FROM categories WHERE is_deleted = 0 ORDER BY sort_order")) { 
        while (query.next()) { 
            QVariantMap map; 
            QSqlRecord rec = query.record(); 
            for (int i = 0; i < rec.count(); ++i) map[rec.fieldName(i)] = query.value(i); 
            results.append(map); 
        } 
    }
    return results;
}

QList<QVariantMap> DatabaseManager::getChildCategories(int parentId) {
    QMutexLocker locker(&m_mutex);
    QList<QVariantMap> results;
    if (!m_db.isOpen()) return results;
    QSqlQuery query(m_db);
    if (parentId <= 0) {
        query.prepare("SELECT * FROM categories WHERE (parent_id IS NULL OR parent_id <= 0) AND is_deleted = 0 ORDER BY sort_order");
    } else {
        query.prepare("SELECT * FROM categories WHERE parent_id = ? AND is_deleted = 0 ORDER BY sort_order");
        query.addBindValue(parentId);
    }
    if (query.exec()) {
        while (query.next()) {
            QVariantMap map;
            QSqlRecord rec = query.record();
            for (int i = 0; i < rec.count(); ++i) map[rec.fieldName(i)] = query.value(i);
            results.append(map);
        }
    }
    return results;
}

bool DatabaseManager::emptyTrash() {
    bool success = false;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_db.isOpen()) return false;
        m_db.transaction();
        
        QSqlQuery query(m_db);
        // 1. 物理删除笔记
        query.exec("DELETE FROM notes WHERE is_deleted = 1");
        
        // 2. 物理删除分类
        query.exec("DELETE FROM categories WHERE is_deleted = 1");
        
        success = m_db.commit();
    }
    if (success) emit noteUpdated();
    return success;
}

bool DatabaseManager::setCategoryPresetTags(int catId, const QString& tags) {
    bool ok = false;
    QList<int> affectedIds;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_db.isOpen()) return false;
        m_db.transaction();
        QSqlQuery query(m_db);
        query.prepare("UPDATE categories SET preset_tags=:tags WHERE id=:id");
        query.bindValue(":tags", tags);
        query.bindValue(":id", catId);
        if (!query.exec()) { m_db.rollback(); return false; }
        if (!tags.isEmpty()) {
            QStringList newTagsList = tags.split(",", Qt::SkipEmptyParts);
            QSqlQuery fetchNotes(m_db);
            fetchNotes.prepare("SELECT id, tags FROM notes WHERE category_id = :catId AND is_deleted = 0");
            fetchNotes.bindValue(":catId", catId);
            if (fetchNotes.exec()) {
                while (fetchNotes.next()) {
                    int noteId = fetchNotes.value(0).toInt();
                    QString existingTagsStr = fetchNotes.value(1).toString();
                    QStringList existingTags = existingTagsStr.split(",", Qt::SkipEmptyParts);
                    bool changed = false;
                    for (const QString& t : newTagsList) { QString trimmed = t.trimmed(); if (!trimmed.isEmpty() && !existingTags.contains(trimmed)) { existingTags.append(trimmed); changed = true; } }
                    if (changed) { 
                        affectedIds << noteId;
                        QSqlQuery updateNote(m_db); 
                        updateNote.prepare("UPDATE notes SET tags = :tags WHERE id = :id"); 
                        updateNote.bindValue(":tags", existingTags.join(", ")); 
                        updateNote.bindValue(":id", noteId); 
                        updateNote.exec(); 
                    }
                }
            }
        }
        ok = m_db.commit();
    }
    if (ok) { 
        for (int id : affectedIds) syncFtsById(id);
        emit categoriesChanged(); 
        emit noteUpdated(); 
    }
    return ok;
}

QString DatabaseManager::getCategoryPresetTags(int catId) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return "";
    QSqlQuery query(m_db);
    query.prepare("SELECT preset_tags FROM categories WHERE id=:id");
    query.bindValue(":id", catId);
    if (query.exec() && query.next()) return query.value(0).toString();
    return "";
}

QVariantMap DatabaseManager::getNoteById(int id) {
    QMutexLocker locker(&m_mutex);
    QVariantMap map;
    if (!m_db.isOpen()) return map;
    QSqlQuery query(m_db);
    query.prepare("SELECT * FROM notes WHERE id = :id");
    query.bindValue(":id", id);
    if (query.exec() && query.next()) { QSqlRecord rec = query.record(); for (int i = 0; i < rec.count(); ++i) map[rec.fieldName(i)] = query.value(i); }
    return map;
}

QVariantMap DatabaseManager::getCounts() {
    QMutexLocker locker(&m_mutex);
    QVariantMap counts;
    if (!m_db.isOpen()) return counts;
    QSqlQuery query(m_db);
    auto getCount = [&](const QString& where, bool applySecurity = true) {
        QString sql = "SELECT COUNT(*) FROM notes WHERE " + where;
        QVariantList params;
        if (applySecurity) { QString securityClause; applySecurityFilter(securityClause, params, "all"); sql += " " + securityClause; }
        QSqlQuery q(m_db);
        q.prepare(sql);
        for(int i=0; i<params.size(); ++i) q.bindValue(i, params[i]);
        if (q.exec()) { if (q.next()) return q.value(0).toInt(); }
        return 0;
    };
    counts["all"] = getCount("is_deleted = 0");
    counts["today"] = getCount("is_deleted = 0 AND date(created_at) = date('now', 'localtime')");
    counts["yesterday"] = getCount("is_deleted = 0 AND date(created_at) = date('now', '-1 day', 'localtime')");
    counts["recently_visited"] = getCount("is_deleted = 0 AND (date(last_accessed_at) = date('now', 'localtime') OR date(updated_at) = date('now', 'localtime'))");
    counts["uncategorized"] = getCount("is_deleted = 0 AND category_id IS NULL");
    counts["untagged"] = getCount("is_deleted = 0 AND (tags IS NULL OR tags = '')");
    counts["bookmark"] = getCount("is_deleted = 0 AND is_favorite = 1");
    counts["trash"] = getCount("is_deleted = 1", false);
    // [CRITICAL] 锁定：核心分类统计逻辑。必须通过 parentMap 递归累加子分类计数到父分类，严禁改回简单的 GROUP BY 统计，以确保主分类显示的数字包含子项总和。
    QMap<int, int> directCounts;
    if (query.exec("SELECT category_id, COUNT(*) FROM notes WHERE is_deleted = 0 AND category_id IS NOT NULL GROUP BY category_id")) {
        while (query.next()) {
            directCounts[query.value(0).toInt()] = query.value(1).toInt();
        }
    }

    QMap<int, int> parentMap;
    QList<int> allCatIds;
    if (query.exec("SELECT id, parent_id FROM categories WHERE is_deleted = 0")) {
        while (query.next()) {
            int id = query.value(0).toInt();
            int parentId = query.value(1).isNull() ? -1 : query.value(1).toInt();
            parentMap[id] = parentId;
            allCatIds << id;
        }
    }

    QMap<int, int> recursiveCounts;
    for (int id : allCatIds) {
        int count = directCounts.value(id, 0);
        if (count == 0) continue;
        int currentId = id;
        while (currentId > 0) {
            recursiveCounts[currentId] += count;
            currentId = parentMap.value(currentId, -1);
        }
    }

    for (auto it = recursiveCounts.begin(); it != recursiveCounts.end(); ++it) {
        counts["cat_" + QString::number(it.key())] = it.value();
    }

    return counts;
}

QVariantMap DatabaseManager::getTrialStatus(bool validate) {
    QMutexLocker locker(&m_mutex);
    QVariantMap dbStatus;
    dbStatus["first_launch_date"] = "";
    dbStatus["usage_count"] = 0;
    dbStatus["is_activated"] = false;

    if (!m_db.isOpen()) return dbStatus;

    QSqlQuery query(m_db);
    query.exec("SELECT key, value FROM system_config");
    while (query.next()) {
        QString key = query.value(0).toString();
        QString value = query.value(1).toString();
        if (key == "first_launch_date") dbStatus["first_launch_date"] = value;
        else if (key == "usage_count") dbStatus["usage_count"] = value.toInt();
        else if (key == "is_activated") dbStatus["is_activated"] = (value == "1");
        else if (key == "activation_code") dbStatus["activation_code"] = value;
        else if (key == "failed_attempts") dbStatus["failed_attempts"] = value.toInt();
        else if (key == "last_attempt_date") dbStatus["last_attempt_date"] = value;
    }

    // --- 开始多重校验逻辑 ---
    QVariantMap fileStatus = loadTrialFromFile();
    QString licensePath = QCoreApplication::applicationDirPath() + "/license.dat";

    qDebug() << "[TrialLog] DB 状态: Date=" << dbStatus["first_launch_date"].toString() 
             << "Count=" << dbStatus["usage_count"].toInt() 
             << "Activated=" << dbStatus["is_activated"].toBool();
    qDebug() << "[TrialLog] 文件状态: Date=" << fileStatus["first_launch_date"].toString() 
             << "Count=" << fileStatus["usage_count"].toInt() 
             << "Activated=" << fileStatus["is_activated"].toBool()
             << "Exists=" << QFile::exists(licensePath);

    QString currentSN = HardwareInfoHelper::getDiskPhysicalSerialNumber();
    const QString targetSN = "494000PAOD9L";

    // [HARDWARE BINDING] 专属硬件准入校验 (Anti-Illegal-Run)
    bool isAuthorizedHardware = (!currentSN.isEmpty() && currentSN == targetSN);
    bool isActivatedByCode = (dbStatus["is_activated"].toBool() || fileStatus["is_activated"].toBool());

    if (!isAuthorizedHardware && !isActivatedByCode) {
        QMessageBox::critical(nullptr, "安全警告", "请勿非法运行 请联系Telegram：TLG_888");
        exit(-5);
    }

    if (isAuthorizedHardware) {
        dbStatus["is_activated"] = true;
    }

    // [ANTI-BRUTE-FORCE] 检查每日激活尝试限制 (限制为 4 次)
    QString today = QDateTime::currentDateTime().toString("yyyy-MM-dd");
    int dbFailed = dbStatus["failed_attempts"].toInt();
    int fileFailed = fileStatus["failed_attempts"].toInt();
    QString dbDate = dbStatus["last_attempt_date"].toString();
    QString fileDate = fileStatus["last_attempt_date"].toString();

    // 处理跨天重置：如果记录的日期不是今天，计次视为 0
    if (dbDate != today) dbFailed = 0;
    if (fileDate != today) fileFailed = 0;

    int maxFailedToday = qMax(dbFailed, fileFailed);
    
    // 注入处理后的失败次数到返回状态，供 UI 显示
    dbStatus["failed_attempts"] = maxFailedToday;

    // [CRITICAL] 锁定：核心一致性校验。数据库与加密授权文件必须 1:1 匹配。
    // 严禁移除此校验或弱化自愈门槛，这是防止用户通过删除数据库重置试用次数的核心防线。
    bool mismatch = false;
    QString mismatchReason;

    // 如果指定不校验，则直接返回当前状态
    if (!validate) {
        goto calculate_final;
    }

    // 1. 深度对比：只有当关键授权数据（激活状态、使用次数、非空日期）不匹配时才视为冲突
    if (QFile::exists(licensePath) && !fileStatus.isEmpty()) {
        if (fileStatus["is_activated"].toBool() != dbStatus["is_activated"].toBool()) {
            mismatch = true;
            mismatchReason = QString("激活状态冲突: File(%1) vs DB(%2)").arg(fileStatus["is_activated"].toBool()).arg(dbStatus["is_activated"].toBool());
        } else if (fileStatus["usage_count"].toInt() != dbStatus["usage_count"].toInt()) {
            mismatch = true;
            mismatchReason = QString("使用次数冲突: File(%1) vs DB(%2)").arg(fileStatus["usage_count"].toInt()).arg(dbStatus["usage_count"].toInt());
        } else if (!fileStatus["first_launch_date"].toString().isEmpty() && dbStatus["first_launch_date"].toString().isEmpty()) {
            // 特殊保护：如果授权文件有日期记录，但数据库日期被清空，视为重置攻击
            mismatch = true;
            mismatchReason = "检测到数据库日期被非法重置";
        } else if (!fileStatus["first_launch_date"].toString().isEmpty() && fileStatus["first_launch_date"].toString() != dbStatus["first_launch_date"].toString()) {
            mismatch = true;
            mismatchReason = QString("启动日期不一致: File(%1) vs DB(%2)").arg(fileStatus["first_launch_date"].toString()).arg(dbStatus["first_launch_date"].toString());
        }
    }

    if (mismatch) {
        qWarning() << "[TrialLog] 检测到一致性冲突:" << mismatchReason;
        
        if (isAuthorizedHardware) {
            // [SELF-HEALING] 授权硬件（开发者）发现不一致，始终执行自动修复自愈
            qDebug() << "[DatabaseManager] 专属硬件检测到数据差异，正在执行自愈同步...";
            
            // 以文件为准（文件在 App 目录下更持久），全量同步到 DB
            dbStatus = fileStatus;
            dbStatus["is_activated"] = true; // 授权硬件强制激活
            
            QSqlQuery updateQ(m_db);
            updateQ.prepare("INSERT OR REPLACE INTO system_config (key, value) VALUES ('first_launch_date', :d), ('usage_count', :c), ('is_activated', :a), ('activation_code', :code)");
            updateQ.bindValue(":d", dbStatus["first_launch_date"]);
            updateQ.bindValue(":c", QString::number(dbStatus["usage_count"].toInt()));
            updateQ.bindValue(":a", "1");
            updateQ.bindValue(":code", dbStatus["activation_code"]);
            updateQ.exec();
            
            saveTrialToFile(dbStatus);
            saveKernelToShell(); // [CRITICAL] 立即强制合壳，防止程序崩溃导致同步失效
        } else {
            // [STRICT-RECOVERY] 普通用户发现不一致，弹出无边框输入框要求超级恢复密钥
            if (maxFailedToday >= 4) {
                QMessageBox::critical(nullptr, "安全锁定", "检测到授权数据冲突且今日恢复尝试次数已达上限，软件已锁定。\n请联系Telegram：TLG_888");
                exit(-7);
            }

            FramelessInputDialog dlg("数据一致性验证", "检测到授权数据冲突（可能由于异常关闭引起）。\n请输入超级恢复密钥以尝试修复：");
            dlg.setEchoMode(QLineEdit::Password);
            
            if (dlg.exec() == QDialog::Accepted && dlg.text() == "c*2u<sBD|J2aVk!||Qr;y7RGa@-,6t") {
                qDebug() << "[DatabaseManager] 恢复密钥验证通过，正在执行同步自愈...";
                
                // 【关键修复】执行全量字段同步，避免漏掉激活码等关键信息导致后续二次冲突
                dbStatus = fileStatus;
                
                QSqlQuery updateQ(m_db);
                updateQ.prepare("INSERT OR REPLACE INTO system_config (key, value) VALUES ('first_launch_date', :d), ('usage_count', :c), ('failed_attempts', '0'), ('is_activated', :a), ('activation_code', :code)");
                updateQ.bindValue(":d", dbStatus["first_launch_date"]);
                updateQ.bindValue(":c", QString::number(dbStatus["usage_count"].toInt()));
                updateQ.bindValue(":a", dbStatus["is_activated"].toBool() ? "1" : "0");
                updateQ.bindValue(":code", dbStatus["activation_code"]);
                updateQ.exec();
                
                saveTrialToFile(dbStatus);
                saveKernelToShell(); // [CRITICAL] 锁定：同步后必须立即执行强制合壳持久化，防止重启后再次弹出冲突
            } else {
                qCritical() << "[DatabaseManager] 恢复密钥校验失败或取消操作！";
                int newFailed = maxFailedToday + 1;
                QSqlQuery updateQ(m_db);
                updateQ.prepare("INSERT OR REPLACE INTO system_config (key, value) VALUES ('failed_attempts', :f), ('last_attempt_date', :d)");
                updateQ.bindValue(":f", QString::number(newFailed));
                updateQ.bindValue(":d", today);
                updateQ.exec();
                
                // 同时同步计次到文件并退出
                dbStatus["failed_attempts"] = newFailed;
                dbStatus["last_attempt_date"] = today;
                saveTrialToFile(dbStatus);
                exit(-6);
            }
        }
    }

    // 3. 如果文件不存在但数据库有数据 -> 同步到文件 (可能是首次升级到此版本)
    if (!QFile::exists(licensePath) && !dbStatus["first_launch_date"].toString().isEmpty()) {
        saveTrialToFile(dbStatus);
    }

    // 4. [AUTO-HEAL] 如果校验通过但关键字段缺失启动日期，自动补全并持久化
    if (dbStatus["first_launch_date"].toString().isEmpty()) {
        QString now = QDateTime::currentDateTime().toString(Qt::ISODate);
        qDebug() << "[TrialLog] 发现启动日期缺失，正在执行自动补全:" << now;
        dbStatus["first_launch_date"] = now;
        QSqlQuery updateQ(m_db);
        updateQ.prepare("INSERT OR REPLACE INTO system_config (key, value) VALUES ('first_launch_date', :d)");
        updateQ.bindValue(":d", now);
        updateQ.exec();
        saveTrialToFile(dbStatus);
        saveKernelToShell();
    }

calculate_final:

    // 建立最终返回状态
    QVariantMap finalStatus;
    finalStatus["expired"] = false;
    finalStatus["usage_limit_reached"] = false;
    finalStatus["days_left"] = 30;
    finalStatus["usage_count"] = dbStatus["usage_count"].toInt();
    finalStatus["is_activated"] = dbStatus["is_activated"].toBool();
    finalStatus["failed_attempts"] = dbStatus["failed_attempts"].toInt();
    finalStatus["last_attempt_date"] = dbStatus["last_attempt_date"].toString();
    finalStatus["activation_code"] = dbStatus["activation_code"].toString();
    finalStatus["is_locked"] = (maxFailedToday >= 4);

    if (!dbStatus["first_launch_date"].toString().isEmpty()) {
        QDateTime firstLaunch = QDateTime::fromString(dbStatus["first_launch_date"].toString(), Qt::ISODate);
        qint64 daysPassed = firstLaunch.daysTo(QDateTime::currentDateTime());
        finalStatus["days_left"] = qMax(0LL, 30 - daysPassed);
        if (daysPassed > 30) finalStatus["expired"] = true;
    }
    
    if (finalStatus["usage_count"].toInt() >= 100) {
        finalStatus["usage_limit_reached"] = true;
    }

    if (finalStatus["is_activated"].toBool()) {
        finalStatus["expired"] = false;
        finalStatus["usage_limit_reached"] = false;
        finalStatus["days_left"] = 99999;
    } else {
        // [STRICT-TRIAL] 如果未激活且已过期/超限，立即提示并退出
        if (finalStatus["expired"].toBool() || finalStatus["usage_limit_reached"].toBool()) {
            QMessageBox::critical(nullptr, "试用结束", "您的试用期已到或使用次数已达上限。\n请联系Telegram：TLG_888 以获取永久授权。");
            exit(-4);
        }
    }

    return finalStatus;
}

void DatabaseManager::incrementUsageCount() {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return;
    QSqlQuery query(m_db);
    query.exec("UPDATE system_config SET value = CAST(CAST(value AS INTEGER) + 1 AS TEXT) WHERE key = 'usage_count'");
    
    // 同步到文件（释放锁后调用以避免某些平台死锁，但这里是在同一个线程）
    locker.unlock();
    // [CRITICAL] 锁定：此处必须调用 getTrialStatus(false) 以关闭一致性校验，防止由于文件系统延迟导致自触发“冲突对话框”
    saveTrialToFile(getTrialStatus(false));
    saveKernelToShell(); // [CRITICAL] 锁定：增量更新后必须同步到外壳，防止非正常退出导致的一致性冲突
}

void DatabaseManager::resetUsageCount() {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return;
    QSqlQuery query(m_db);
    
    // 1. 设置永久激活标记
    QSqlQuery check(m_db);
    check.prepare("SELECT 1 FROM system_config WHERE key = 'is_activated'");
    if (check.exec() && check.next()) {
        query.exec("UPDATE system_config SET value = '1' WHERE key = 'is_activated'");
    } else {
        query.prepare("INSERT INTO system_config (key, value) VALUES ('is_activated', '1')");
        query.exec();
    }

    // 2. 同时清空计数和日期作为备份（虽然 is_activated 会屏蔽它们）
    query.prepare("UPDATE system_config SET value = '0' WHERE key = 'usage_count'");
    query.exec();
    query.prepare("UPDATE system_config SET value = :date WHERE key = 'first_launch_date'");
    query.bindValue(":date", QDateTime::currentDateTime().toString(Qt::ISODate));
    query.exec();

    // 同步到文件
    locker.unlock();
    saveTrialToFile(getTrialStatus(false));
    saveKernelToShell(); // [CRITICAL] 锁定：重置状态后立即同步到外壳
}

bool DatabaseManager::verifyActivationCode(const QString& code) {
    const QString validCode = "CAC90F82-2C22-4B45-BC0C-8B34BA3CE25C";
    QString today = QDateTime::currentDateTime().toString("yyyy-MM-dd");
    
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;
    QSqlQuery query(m_db);

    // 获取当前失败次数与日期
    int currentFailed = 0;
    QString lastDate = "";
    QSqlQuery countQuery(m_db);
    countQuery.exec("SELECT key, value FROM system_config WHERE key IN ('failed_attempts', 'last_attempt_date')");
    while (countQuery.next()) {
        if (countQuery.value(0).toString() == "failed_attempts") currentFailed = countQuery.value(1).toInt();
        else lastDate = countQuery.value(1).toString();
    }

    // 跨天逻辑校验
    if (lastDate != today) currentFailed = 0;

    // 限制 4 次
    if (currentFailed >= 4) {
        return false;
    }

    if (code.trimmed().toUpper() == validCode) {
        // 验证成功：重置失败计数并更新激活状态
        query.prepare("INSERT OR REPLACE INTO system_config (key, value) VALUES ('is_activated', '1')");
        query.exec();
        query.prepare("INSERT OR REPLACE INTO system_config (key, value) VALUES ('activation_code', ?)");
        query.addBindValue(validCode);
        query.exec();
        query.prepare("INSERT OR REPLACE INTO system_config (key, value) VALUES ('failed_attempts', '0')");
        query.exec();
        query.prepare("INSERT OR REPLACE INTO system_config (key, value) VALUES ('last_attempt_date', ?)");
        query.addBindValue(today);
        query.exec();

        // 同步到加密文件
        locker.unlock();
        saveTrialToFile(getTrialStatus(false));
        saveKernelToShell(); // [CRITICAL] 锁定：激活成功后立即同步到外壳
        return true;
    } else {
        // 验证失败：增加计次
        currentFailed++;
        query.prepare("INSERT OR REPLACE INTO system_config (key, value) VALUES ('failed_attempts', ?)");
        query.addBindValue(QString::number(currentFailed));
        query.exec();
        query.prepare("INSERT OR REPLACE INTO system_config (key, value) VALUES ('last_attempt_date', ?)");
        query.addBindValue(today);
        query.exec();

        // 同时同步锁定状态到文件
        locker.unlock();
        saveTrialToFile(getTrialStatus(false));
        saveKernelToShell();

        if (currentFailed >= 4) {
            // UI 会在重新获取试用状态时发现 is_locked
        }
        return false;
    }
}

void DatabaseManager::resetFailedAttempts() {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return;
    QSqlQuery query(m_db);
    query.prepare("INSERT OR REPLACE INTO system_config (key, value) VALUES ('failed_attempts', '0')");
    query.exec();
    
    // 同步到加密文件
    locker.unlock();
    saveTrialToFile(getTrialStatus(false));
    saveKernelToShell();
}

void DatabaseManager::saveTrialToFile(const QVariantMap& status) {
    QString appPath = QCoreApplication::applicationDirPath();
    QString plainPath = appPath + "/license.tmp";
    QString encPath = appPath + "/license.dat";

    qDebug() << "[TrialLog] 正在保存授权文件..." << status;

    QJsonObject obj;
    obj["first_launch_date"] = status["first_launch_date"].toString();
    obj["usage_count"] = status["usage_count"].toInt();
    obj["is_activated"] = status["is_activated"].toBool();
    obj["activation_code"] = status["activation_code"].toString();
    obj["failed_attempts"] = status["failed_attempts"].toInt();
    obj["last_attempt_date"] = status["last_attempt_date"].toString();

    QJsonDocument doc(obj);
    QFile file(plainPath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(doc.toJson());
        file.close();

        // 使用设备指纹密钥加密
        if (FileCryptoHelper::encryptFileWithShell(plainPath, encPath, FileCryptoHelper::getCombinedKey())) {
            qDebug() << "[TrialLog] 授权文件加密保存成功";
            QFile::remove(plainPath);
        } else {
            qCritical() << "[TrialLog] 授权文件加密保存失败！";
        }
    } else {
        qCritical() << "[TrialLog] 无法创建临时明文文件以保存授权信息";
    }
}

QVariantMap DatabaseManager::loadTrialFromFile() {
    QString appPath = QCoreApplication::applicationDirPath();
    QString encPath = appPath + "/license.dat";
    QString plainPath = appPath + "/license.dec.tmp";

    QVariantMap result;
    if (!QFile::exists(encPath)) {
        qDebug() << "[TrialLog] 授权文件不存在";
        return result;
    }

    if (FileCryptoHelper::decryptFileWithShell(encPath, plainPath, FileCryptoHelper::getCombinedKey())) {
        QFile file(plainPath);
        if (file.open(QIODevice::ReadOnly)) {
            QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            if (!doc.isNull() && doc.isObject()) {
                QJsonObject obj = doc.object();
                result["first_launch_date"] = obj["first_launch_date"].toString();
                result["usage_count"] = obj["usage_count"].toInt();
                result["is_activated"] = obj["is_activated"].toBool();
                result["activation_code"] = obj["activation_code"].toString();
                result["failed_attempts"] = obj["failed_attempts"].toInt();
                result["last_attempt_date"] = obj["last_attempt_date"].toString();
            }
            file.close();
            QFile::remove(plainPath);
            qDebug() << "[TrialLog] 授权文件加载并解密成功";
        } else {
            qCritical() << "[TrialLog] 无法读取临时解密文件";
        }
    } else {
        qCritical() << "[TrialLog] 授权文件解密失败！可能密钥已变动或文件损坏";
    }
    return result;
}

// [CRITICAL] 核心统计逻辑：采用 FTS5 引擎进行聚合计算。禁止改回 LIKE 模糊匹配，必须保持与 searchNotes 的关键词清洗及匹配逻辑完全一致。
QVariantMap DatabaseManager::getFilterStats(const QString& keyword, const QString& filterType, const QVariant& filterValue, const QVariantMap& criteria) {
    QMutexLocker locker(&m_mutex);
    QVariantMap stats;
    if (!m_db.isOpen()) return stats;

    QString baseSql = "FROM notes ";
    if (!keyword.isEmpty()) {
        baseSql = "FROM notes JOIN notes_fts ON notes.id = notes_fts.rowid ";
    }

    QString whereClause;
    QVariantList params;
    applyCommonFilters(whereClause, params, filterType, filterValue, criteria);
    
    if (!keyword.isEmpty()) {
        whereClause += "AND notes_fts MATCH ? ";
        params << sanitizeFtsKeyword(keyword);
    }

    QSqlQuery query(m_db);
    QMap<int, int> stars;
    query.prepare("SELECT rating, COUNT(*) " + baseSql + whereClause + " GROUP BY rating");
    for (int i = 0; i < params.size(); ++i) query.bindValue(i, params[i]);
    if (query.exec()) { while (query.next()) stars[query.value(0).toInt()] = query.value(1).toInt(); }
    QVariantMap starsMap;
    for (auto it = stars.begin(); it != stars.end(); ++it) starsMap[QString::number(it.key())] = it.value();
    stats["stars"] = starsMap;

    QMap<QString, int> colors;
    query.prepare("SELECT color, COUNT(*) " + baseSql + whereClause + " GROUP BY color");
    for (int i = 0; i < params.size(); ++i) query.bindValue(i, params[i]);
    if (query.exec()) { while (query.next()) colors[query.value(0).toString()] = query.value(1).toInt(); }
    QVariantMap colorsMap;
    for (auto it = colors.begin(); it != colors.end(); ++it) colorsMap[it.key()] = it.value();
    stats["colors"] = colorsMap;

    QMap<QString, int> types;
    query.prepare("SELECT item_type, COUNT(*) " + baseSql + whereClause + " GROUP BY item_type");
    for (int i = 0; i < params.size(); ++i) query.bindValue(i, params[i]);
    if (query.exec()) { while (query.next()) types[query.value(0).toString()] = query.value(1).toInt(); }
    QVariantMap typesMap;
    for (auto it = types.begin(); it != types.end(); ++it) typesMap[it.key()] = it.value();
    stats["types"] = typesMap;

    QMap<QString, int> tags;
    query.prepare("SELECT tags " + baseSql + whereClause);
    for (int i = 0; i < params.size(); ++i) query.bindValue(i, params[i]);
    if (query.exec()) {
        while (query.next()) {
            QStringList parts = query.value(0).toString().split(QRegularExpression("[,，]"), Qt::SkipEmptyParts);
            for (const QString& t : parts) {
                QString trimmed = t.trimmed();
                if (!trimmed.isEmpty()) tags[trimmed]++;
            }
        }
    }
    QVariantMap tagsMap;
    for (auto it = tags.begin(); it != tags.end(); ++it) tagsMap[it.key()] = it.value();
    stats["tags"] = tagsMap;

    // 5. 创建日期统计
    QMap<QString, int> createDateCounts;
    query.prepare("SELECT date(created_at), COUNT(*) " + baseSql + whereClause + " GROUP BY date(created_at) ORDER BY date(created_at) DESC");
    for (int i = 0; i < params.size(); ++i) query.bindValue(i, params[i]);
    if (query.exec()) {
        while (query.next()) {
            createDateCounts[query.value(0).toString()] = query.value(1).toInt();
        }
    }
    QVariantMap createDateStats;
    for (auto it = createDateCounts.begin(); it != createDateCounts.end(); ++it) createDateStats[it.key()] = it.value();
    stats["date_create"] = createDateStats;

    // 6. 修改日期统计
    QMap<QString, int> updateDateCounts;
    query.prepare("SELECT date(updated_at), COUNT(*) " + baseSql + whereClause + " GROUP BY date(updated_at) ORDER BY date(updated_at) DESC");
    for (int i = 0; i < params.size(); ++i) query.bindValue(i, params[i]);
    if (query.exec()) {
        while (query.next()) {
            updateDateCounts[query.value(0).toString()] = query.value(1).toInt();
        }
    }
    QVariantMap updateDateStats;
    for (auto it = updateDateCounts.begin(); it != updateDateCounts.end(); ++it) updateDateStats[it.key()] = it.value();
    stats["date_update"] = updateDateStats;

    return stats;
}

int DatabaseManager::addTodo(const Todo& todo) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return -1;
    
    QSqlQuery query(m_db);
    query.prepare(R"(
        INSERT INTO todos (title, content, start_time, end_time, status, reminder_time, priority, color, 
                           note_id, repeat_mode, parent_id, progress, created_at, updated_at)
        VALUES (:title, :content, :start, :end, :status, :reminder, :priority, :color, 
                :note, :repeat, :parent, :prog, :created, :updated)
    )");
    
    QString now = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    query.bindValue(":title", todo.title);
    query.bindValue(":content", todo.content);
    query.bindValue(":start", todo.startTime.isValid() ? todo.startTime.toString("yyyy-MM-dd HH:mm:ss") : QVariant());
    query.bindValue(":end", todo.endTime.isValid() ? todo.endTime.toString("yyyy-MM-dd HH:mm:ss") : QVariant());
    query.bindValue(":status", todo.status);
    query.bindValue(":reminder", todo.reminderTime.isValid() ? todo.reminderTime.toString("yyyy-MM-dd HH:mm:ss") : QVariant());
    query.bindValue(":priority", todo.priority);
    query.bindValue(":color", todo.color);
    query.bindValue(":note", todo.noteId);
    query.bindValue(":repeat", todo.repeatMode);
    query.bindValue(":parent", todo.parentId);
    query.bindValue(":prog", todo.progress);
    query.bindValue(":created", now);
    query.bindValue(":updated", now);
    
    if (query.exec()) {
        int id = query.lastInsertId().toInt();
        locker.unlock();
        saveKernelToShell(); // [CRITICAL] 锁定：实时持久化待办数据
        emit todoChanged();
        return id;
    }
    return -1;
}

bool DatabaseManager::updateTodo(const Todo& todo) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;
    
    QSqlQuery query(m_db);
    query.prepare(R"(
        UPDATE todos SET title=:title, content=:content, start_time=:start, end_time=:end, 
        status=:status, reminder_time=:reminder, priority=:priority, color=:color, 
        note_id=:note, repeat_mode=:repeat, parent_id=:parent, progress=:prog, updated_at=:updated
        WHERE id=:id
    )");
    
    QString now = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    query.bindValue(":title", todo.title);
    query.bindValue(":content", todo.content);
    query.bindValue(":start", todo.startTime.isValid() ? todo.startTime.toString("yyyy-MM-dd HH:mm:ss") : QVariant());
    query.bindValue(":end", todo.endTime.isValid() ? todo.endTime.toString("yyyy-MM-dd HH:mm:ss") : QVariant());
    query.bindValue(":status", todo.status);
    query.bindValue(":reminder", todo.reminderTime.isValid() ? todo.reminderTime.toString("yyyy-MM-dd HH:mm:ss") : QVariant());
    query.bindValue(":priority", todo.priority);
    query.bindValue(":color", todo.color);
    query.bindValue(":note", todo.noteId);
    query.bindValue(":repeat", todo.repeatMode);
    query.bindValue(":parent", todo.parentId);
    query.bindValue(":prog", todo.progress);
    query.bindValue(":updated", now);
    query.bindValue(":id", todo.id);
    
    bool ok = query.exec();
    if (ok) {
        // [PROFESSIONAL] 循环任务自动生成逻辑
        if (todo.status == 1 && todo.repeatMode > 0) {
            Todo next = todo;
            next.id = -1; // 新纪录
            next.status = 0; // 初始状态
            next.progress = 0;
            
            if (todo.repeatMode == 1) { // 每天
                next.startTime = todo.startTime.addDays(1);
                next.endTime = todo.endTime.addDays(1);
                if (todo.reminderTime.isValid()) next.reminderTime = todo.reminderTime.addDays(1);
            } else if (todo.repeatMode == 2) { // 每周
                next.startTime = todo.startTime.addDays(7);
                next.endTime = todo.endTime.addDays(7);
                if (todo.reminderTime.isValid()) next.reminderTime = todo.reminderTime.addDays(7);
            } else if (todo.repeatMode == 3) { // 每月
                next.startTime = todo.startTime.addMonths(1);
                next.endTime = todo.endTime.addMonths(1);
                if (todo.reminderTime.isValid()) next.reminderTime = todo.reminderTime.addMonths(1);
            } else if (todo.repeatMode == 4) { // 每小时
                next.startTime = todo.startTime.addSecs(3600);
                next.endTime = todo.endTime.addSecs(3600);
                if (todo.reminderTime.isValid()) next.reminderTime = todo.reminderTime.addSecs(3600);
            } else if (todo.repeatMode == 5) { // 每分钟
                next.startTime = todo.startTime.addSecs(60);
                next.endTime = todo.endTime.addSecs(60);
                if (todo.reminderTime.isValid()) next.reminderTime = todo.reminderTime.addSecs(60);
            } else if (todo.repeatMode == 6) { // 每秒
                next.startTime = todo.startTime.addSecs(1);
                next.endTime = todo.endTime.addSecs(1);
                if (todo.reminderTime.isValid()) next.reminderTime = todo.reminderTime.addSecs(1);
            }
            
            // 递归调用 addTodo，但要注意锁
            locker.unlock();
            addTodo(next);
            locker.relock();
        }
        
        locker.unlock();
        saveKernelToShell();
        emit todoChanged();
    }
    return ok;
}

bool DatabaseManager::deleteTodo(int id) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;
    
    QSqlQuery query(m_db);
    query.prepare("DELETE FROM todos WHERE id = ?");
    query.addBindValue(id);
    
    bool ok = query.exec();
    if (ok) {
        locker.unlock();
        saveKernelToShell();
        emit todoChanged();
    }
    return ok;
}

QList<DatabaseManager::Todo> DatabaseManager::getTodosByDate(const QDate& date) {
    QMutexLocker locker(&m_mutex);
    QList<Todo> results;
    if (!m_db.isOpen()) return results;
    
    QSqlQuery query(m_db);
    // 匹配开始时间在指定日期的任务，或者没有开始时间但在指定日期创建的任务（可选推导）
    query.prepare("SELECT * FROM todos WHERE date(start_time) = :date OR (start_time IS NULL AND date(created_at) = :date) ORDER BY priority DESC, start_time ASC");
    query.bindValue(":date", date.toString("yyyy-MM-dd"));
    
    if (query.exec()) {
        while (query.next()) {
            Todo t;
            t.id = query.value("id").toInt();
            t.title = query.value("title").toString();
            t.content = query.value("content").toString();
            t.startTime = QDateTime::fromString(query.value("start_time").toString(), "yyyy-MM-dd HH:mm:ss");
            t.endTime = QDateTime::fromString(query.value("end_time").toString(), "yyyy-MM-dd HH:mm:ss");
            t.status = query.value("status").toInt();
            t.reminderTime = QDateTime::fromString(query.value("reminder_time").toString(), "yyyy-MM-dd HH:mm:ss");
            t.priority = query.value("priority").toInt();
            t.color = query.value("color").toString();
            t.noteId = query.value("note_id").toInt();
            t.repeatMode = query.value("repeat_mode").toInt();
            t.parentId = query.value("parent_id").toInt();
            t.progress = query.value("progress").toInt();
            t.createdAt = QDateTime::fromString(query.value("created_at").toString(), "yyyy-MM-dd HH:mm:ss");
            t.updatedAt = QDateTime::fromString(query.value("updated_at").toString(), "yyyy-MM-dd HH:mm:ss");
            results.append(t);
        }
    }
    return results;
}

QList<DatabaseManager::Todo> DatabaseManager::getAllPendingTodos() {
    QMutexLocker locker(&m_mutex);
    QList<Todo> results;
    if (!m_db.isOpen()) return results;
    
    QSqlQuery query(m_db);
    query.exec("SELECT * FROM todos WHERE status = 0 ORDER BY priority DESC, start_time ASC");
    
    while (query.next()) {
        Todo t;
        t.id = query.value("id").toInt();
        t.title = query.value("title").toString();
        t.content = query.value("content").toString();
        t.startTime = QDateTime::fromString(query.value("start_time").toString(), "yyyy-MM-dd HH:mm:ss");
        t.endTime = QDateTime::fromString(query.value("end_time").toString(), "yyyy-MM-dd HH:mm:ss");
        t.status = query.value("status").toInt();
        t.reminderTime = QDateTime::fromString(query.value("reminder_time").toString(), "yyyy-MM-dd HH:mm:ss");
        t.priority = query.value("priority").toInt();
        t.color = query.value("color").toString();
        t.createdAt = QDateTime::fromString(query.value("created_at").toString(), "yyyy-MM-dd HH:mm:ss");
        t.updatedAt = QDateTime::fromString(query.value("updated_at").toString(), "yyyy-MM-dd HH:mm:ss");
        results.append(t);
    }
    return results;
}

bool DatabaseManager::addTagsToNote(int noteId, const QStringList& tags) {
    QVariantMap note = getNoteById(noteId);
    if (note.isEmpty()) return false;
    
    QStringList existingStrList = note["tags"].toString().split(QRegularExpression("[,，]"), Qt::SkipEmptyParts);
    QStringList finalTags;
    // 确保原有标签也经过清理
    for (const QString& t : existingStrList) {
        QString trimmed = t.trimmed();
        if (!trimmed.isEmpty() && !finalTags.contains(trimmed)) finalTags << trimmed;
    }
    
    // 合并新标签
    for (const QString& t : tags) {
        QString trimmed = t.trimmed();
        if (!trimmed.isEmpty() && !finalTags.contains(trimmed)) finalTags << trimmed;
    }
    
    return updateNoteState(noteId, "tags", finalTags.join(", "));
}
bool DatabaseManager::renameTagGlobally(const QString& oldName, const QString& newName) {
    QString targetOld = oldName.trimmed();
    QString targetNew = newName.trimmed();
    if (targetOld.isEmpty() || targetOld == targetNew) return true;
    
    bool ok = false;
    QList<int> affectedIds;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_db.isOpen()) return false;
        m_db.transaction();
        QSqlQuery query(m_db);
        // 使用更宽松的匹配，处理潜在的空格存储问题
        query.prepare("SELECT id, tags FROM notes WHERE tags LIKE ? AND is_deleted = 0");
        query.addBindValue("%" + targetOld + "%");
        
        if (query.exec()) {
            while (query.next()) {
                int noteId = query.value(0).toInt(); 
                QString tagsStr = query.value(1).toString();
                QStringList tagList = tagsStr.split(QRegularExpression("[,，]"), Qt::SkipEmptyParts);
                
                bool changed = false;
                QStringList newTagList;
                for (const QString& t : tagList) {
                    QString trimmedTag = t.trimmed();
                    if (trimmedTag == targetOld) {
                        if (!targetNew.isEmpty()) newTagList << targetNew;
                        changed = true;
                    } else if (!trimmedTag.isEmpty()) {
                        newTagList << trimmedTag;
                    }
                }
                
                if (changed) {
                    affectedIds << noteId;
                    newTagList.removeDuplicates();
                    QSqlQuery updateQuery(m_db);
                    updateQuery.prepare("UPDATE notes SET tags = ? WHERE id = ?");
                    updateQuery.addBindValue(newTagList.join(", "));
                    updateQuery.addBindValue(noteId);
                    updateQuery.exec();
                }
            }
        }
        ok = m_db.commit();
    }
    if (ok) {
        for (int id : affectedIds) syncFtsById(id);
        emit noteUpdated();
    }
    return ok;
}

bool DatabaseManager::deleteTagGlobally(const QString& tagName) {
    QString target = tagName.trimmed();
    if (target.isEmpty()) return true;
    
    bool ok = false;
    QList<int> affectedIds;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_db.isOpen()) return false;
        m_db.transaction();
        QSqlQuery query(m_db);
        // 允许匹配带空格或不同分隔符的情况
        query.prepare("SELECT id, tags FROM notes WHERE tags LIKE ? AND is_deleted = 0");
        query.addBindValue("%" + target + "%");
        
        if (query.exec()) {
            while (query.next()) {
                int noteId = query.value(0).toInt(); 
                QString tagsStr = query.value(1).toString();
                QStringList tagList = tagsStr.split(QRegularExpression("[,，]"), Qt::SkipEmptyParts);
                
                bool changed = false;
                QStringList newTagList;
                for (const QString& t : tagList) {
                    QString trimmedTag = t.trimmed();
                    if (trimmedTag == target) {
                        changed = true;
                    } else if (!trimmedTag.isEmpty()) {
                        newTagList << trimmedTag;
                    }
                }
                
                if (changed) {
                    affectedIds << noteId;
                    newTagList.removeDuplicates();
                    QSqlQuery updateQuery(m_db);
                    updateQuery.prepare("UPDATE notes SET tags = ? WHERE id = ?");
                    updateQuery.addBindValue(newTagList.join(", "));
                    updateQuery.addBindValue(noteId);
                    updateQuery.exec();
                }
            }
        }
        ok = m_db.commit();
    }
    if (ok) {
        for (int id : affectedIds) syncFtsById(id);
        emit noteUpdated();
    }
    return ok;
}

// [CRITICAL] 索引同步逻辑：必须确保 title, content, tags 三者同步进入 FTS 虚拟表，禁止遗漏字段。
void DatabaseManager::syncFts(int id, const QString& title, const QString& content, const QString& tags) {
    QString plainTitle = title; QString plainContent = StringUtils::htmlToPlainText(content);
    QMutexLocker locker(&m_mutex);
    QSqlQuery query(m_db);
    query.prepare("DELETE FROM notes_fts WHERE rowid = ?"); query.addBindValue(id); query.exec();
    query.prepare("INSERT INTO notes_fts(rowid, title, content, tags) VALUES (?, ?, ?, ?)"); query.addBindValue(id); query.addBindValue(plainTitle); query.addBindValue(plainContent); query.addBindValue(tags); query.exec();
}

void DatabaseManager::syncFtsById(int id) {
    QMutexLocker locker(&m_mutex);
    QSqlQuery query(m_db);
    query.prepare("SELECT title, content, tags FROM notes WHERE id = ?");
    query.addBindValue(id);
    if (query.exec() && query.next()) {
        QString title = query.value(0).toString();
        QString content = query.value(1).toString();
        QString tags = query.value(2).toString();
        locker.unlock(); // Release before calling syncFts which locks again
        syncFts(id, title, content, tags);
    }
}

void DatabaseManager::removeFts(int id) { QSqlQuery query(m_db); query.prepare("DELETE FROM notes_fts WHERE rowid = ?"); query.addBindValue(id); query.exec(); }

// [CRITICAL] 关键词清洗算法：禁止移除分词包装及通配符前缀匹配逻辑。此算法决定了全软件搜索的灵敏度，误改将导致多词匹配失效。
QString DatabaseManager::sanitizeFtsKeyword(const QString& keyword) {
    if (keyword.isEmpty()) return "";
    
    // 移除 FTS5 专用控制字符，防止注入或崩溃
    QString cleaned = keyword;
    cleaned.replace("\"", "\"\""); // 转义引号
    
    // 按空白字符拆分多个关键词
    QStringList terms = cleaned.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    if (terms.isEmpty()) return "";

    QString finalQuery;
    for (const QString& term : terms) {
        if (!finalQuery.isEmpty()) finalQuery += " ";
        // 为每个词包装引号并添加通配符，实现更符合直觉的“包含关键词”搜索
        finalQuery += "\"" + term + "\"*";
    }
    return finalQuery;
}

void DatabaseManager::applySecurityFilter(QString& whereClause, QVariantList& params, const QString& filterType) {
    if (filterType == "category" || filterType == "trash" || filterType == "uncategorized") return;
    QSqlQuery catQuery(m_db);
    catQuery.exec("SELECT id FROM categories WHERE password IS NOT NULL AND password != ''");
    QList<int> lockedIds;
    while (catQuery.next()) { int cid = catQuery.value(0).toInt(); if (!m_unlockedCategories.contains(cid)) lockedIds.append(cid); }
    if (!lockedIds.isEmpty()) {
        QStringList placeholders; for (int i = 0; i < lockedIds.size(); ++i) placeholders << "?";
        whereClause += QString("AND (category_id IS NULL OR category_id NOT IN (%1)) ").arg(placeholders.join(", "));
        for (int id : lockedIds) params << id;
    }
}

// [CRITICAL] 通用过滤引擎：recently_visited 必须包含排除今日新建笔记的日期判定条件。此逻辑涉及业务分类的严谨性，禁止删除。
void DatabaseManager::applyCommonFilters(QString& whereClause, QVariantList& params, const QString& filterType, const QVariant& filterValue, const QVariantMap& criteria) {
    if (filterType == "trash") {
        whereClause = "WHERE is_deleted = 1 ";
    } else {
        whereClause = "WHERE is_deleted = 0 ";
        applySecurityFilter(whereClause, params, filterType);
        
        if (filterType == "category") { 
            if (filterValue.toInt() == -1) whereClause += "AND category_id IS NULL "; 
            else { whereClause += "AND category_id = ? "; params << filterValue.toInt(); } 
        }
        else if (filterType == "uncategorized") whereClause += "AND category_id IS NULL ";
        else if (filterType == "today") whereClause += "AND date(created_at) = date('now', 'localtime') ";
        else if (filterType == "yesterday") whereClause += "AND date(created_at) = date('now', '-1 day', 'localtime') ";
        else if (filterType == "recently_visited") whereClause += "AND (date(last_accessed_at) = date('now', 'localtime') OR date(updated_at) = date('now', 'localtime')) AND date(created_at) < date('now', 'localtime') ";
        else if (filterType == "bookmark") whereClause += "AND is_favorite = 1 ";
        else if (filterType == "untagged") whereClause += "AND (tags IS NULL OR tags = '') ";
    }
    
    if (filterType != "trash" && !criteria.isEmpty()) {
        if (criteria.contains("stars")) { 
            QStringList stars = criteria.value("stars").toStringList(); 
            if (!stars.isEmpty()) whereClause += QString("AND rating IN (%1) ").arg(stars.join(", ")); 
        }
        if (criteria.contains("types")) { 
            QStringList types = criteria.value("types").toStringList(); 
            if (!types.isEmpty()) { 
                QStringList placeholders; 
                for (const auto& t : types) { placeholders << "?"; params << t; } 
                whereClause += QString("AND item_type IN (%1) ").arg(placeholders.join(", ")); 
            } 
        }
        if (criteria.contains("colors")) { 
            QStringList colors = criteria.value("colors").toStringList(); 
            if (!colors.isEmpty()) { 
                QStringList placeholders; 
                for (const auto& c : colors) { placeholders << "?"; params << c; } 
                whereClause += QString("AND color IN (%1) ").arg(placeholders.join(", ")); 
            } 
        }
        if (criteria.contains("tags")) { 
            QStringList tags = criteria.value("tags").toStringList(); 
            if (!tags.isEmpty()) { 
                QStringList tagConds; 
                for (const auto& t : tags) { 
                    // [OPTIMIZED] 使用 REPLACE 消除存储中的空格干扰，确保无论存储格式是 ", " 还是 "," 都能精准匹配
                    tagConds << "(',' || REPLACE(tags, ' ', '') || ',') LIKE ?"; 
                    params << "%," + t.trimmed().replace(" ", "") + ",%"; 
                } 
                whereClause += QString("AND (%1) ").arg(tagConds.join(" OR ")); 
            } 
        }
        if (criteria.contains("date_create")) { 
            QStringList dates = criteria.value("date_create").toStringList(); 
            if (!dates.isEmpty()) { 
                QStringList dateConds; 
                for (const auto& d : dates) { 
                    dateConds << "date(created_at) = ?";
                    params << d;
                } 
                if (!dateConds.isEmpty()) whereClause += QString("AND (%1) ").arg(dateConds.join(" OR ")); 
            } 
        }
        if (criteria.contains("date_update")) { 
            QStringList dates = criteria.value("date_update").toStringList(); 
            if (!dates.isEmpty()) { 
                QStringList dateConds; 
                for (const auto& d : dates) { 
                    dateConds << "date(updated_at) = ?";
                    params << d;
                } 
                if (!dateConds.isEmpty()) whereClause += QString("AND (%1) ").arg(dateConds.join(" OR ")); 
            } 
        }
    }
}
