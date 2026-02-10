#include "DatabaseManager.h"
#include <QDebug>
#include <QSqlRecord>
#include <QtConcurrent>
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QDir>
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QFileInfo>
#include <QStandardPaths>
#include "FileCryptoHelper.h"

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

DatabaseManager::DatabaseManager(QObject* parent) : QObject(parent) {}

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

    // 2. 自动迁移逻辑 (Legacy support)
    QString legacyDbPath = QFileInfo(m_realDbPath).absolutePath() + "/notes.db";
    if (!QFile::exists(m_realDbPath) && QFile::exists(legacyDbPath) && !QFile::exists(m_dbPath)) {
        qDebug() << "[DB] 检测到旧版 notes.db，且无新版内核，正在自动迁移至新的三层保护体系...";
        if (QFile::copy(legacyDbPath, m_dbPath)) {
            qDebug() << "[DB] 旧版数据已拷贝至内核，等待退出时加密合壳。";
        }
    }

    // 3. 解壳加载逻辑
    bool kernelExists = QFile::exists(m_dbPath);
    bool shellExists = QFile::exists(m_realDbPath);

    if (kernelExists) {
        // 如果 AppData 下的内核还在，即使外壳被删了，也会从这里加载并“复活”外壳
        qDebug() << "[DB] 检测到残留内核文件 (可能是上次异常退出或仅删除了外壳)，优先加载以恢复数据。";
    } else if (shellExists) {
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
                }
            }
        } else {
            qCritical() << "[DB] 合壳失败！数据保留在内核文件中。";
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

    QStringList columnsToAdd = {
        "ALTER TABLE notes ADD COLUMN color TEXT DEFAULT '#2d2d2d'",
        "ALTER TABLE notes ADD COLUMN category_id INTEGER",
        "ALTER TABLE notes ADD COLUMN item_type TEXT DEFAULT 'text'",
        "ALTER TABLE notes ADD COLUMN data_blob BLOB",
        "ALTER TABLE notes ADD COLUMN content_hash TEXT",
        "ALTER TABLE notes ADD COLUMN rating INTEGER DEFAULT 0",
        "ALTER TABLE notes ADD COLUMN source_app TEXT",
        "ALTER TABLE notes ADD COLUMN source_title TEXT",
        "ALTER TABLE notes ADD COLUMN last_accessed_at DATETIME"
    };
    for (const QString& sql : columnsToAdd) { query.exec(sql); }

    QString createCategoriesTable = R"(
        CREATE TABLE IF NOT EXISTS categories (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,
            parent_id INTEGER,
            color TEXT DEFAULT '#808080',
            sort_order INTEGER DEFAULT 0,
            preset_tags TEXT,
            password TEXT,
            password_hint TEXT
        )
    )";
    query.exec(createCategoriesTable);
    query.exec("ALTER TABLE categories ADD COLUMN password TEXT");
    query.exec("ALTER TABLE categories ADD COLUMN password_hint TEXT");
    query.exec("CREATE TABLE IF NOT EXISTS tags (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT UNIQUE NOT NULL)");
    query.exec("CREATE TABLE IF NOT EXISTS note_tags (note_id INTEGER, tag_id INTEGER, PRIMARY KEY (note_id, tag_id))");
    query.exec("CREATE INDEX IF NOT EXISTS idx_notes_content_hash ON notes(content_hash)");
    QString createFtsTable = R"(
        CREATE VIRTUAL TABLE IF NOT EXISTS notes_fts USING fts5(
            title, content, content='notes', content_rowid='id'
        )
    )";
    query.exec(createFtsTable);
    query.exec("DROP TRIGGER IF EXISTS notes_ai");
    query.exec("DROP TRIGGER IF EXISTS notes_ad");
    query.exec("DROP TRIGGER IF EXISTS notes_au");

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

    return true;
}

bool DatabaseManager::addNote(const QString& title, const QString& content, const QStringList& tags,
                             const QString& color, int categoryId,
                             const QString& itemType, const QByteArray& dataBlob,
                             const QString& sourceApp, const QString& sourceTitle) {
    // 试用限制检查
    QVariantMap trial = getTrialStatus();
    if (trial["expired"].toBool() || trial["usage_limit_reached"].toBool()) {
        qWarning() << "[DB] 试用已结束或达到使用上限，停止新增灵感。";
        return false;
    }

    QVariantMap newNoteMap;
    bool success = false;
    QString currentTime = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    QByteArray hashData = dataBlob.isEmpty() ? content.toUtf8() : dataBlob;
    QString contentHash = QCryptographicHash::hash(hashData, QCryptographicHash::Sha256).toHex();
    {   
        QMutexLocker locker(&m_mutex);
        if (!m_db.isOpen()) return false;
        QSqlQuery checkQuery(m_db);
        checkQuery.prepare("SELECT id FROM notes WHERE content_hash = :hash AND is_deleted = 0 LIMIT 1");
        checkQuery.bindValue(":hash", contentHash);
        if (checkQuery.exec() && checkQuery.next()) {
            int existingId = checkQuery.value(0).toInt();
            QSqlQuery updateQuery(m_db);
            updateQuery.prepare("UPDATE notes SET updated_at = :now, source_app = :app, source_title = :stitle WHERE id = :id");
            updateQuery.bindValue(":now", currentTime);
            updateQuery.bindValue(":app", sourceApp);
            updateQuery.bindValue(":stitle", sourceTitle);
            updateQuery.bindValue(":id", existingId);
            if (updateQuery.exec()) success = true;
            if (success) { locker.unlock(); emit noteUpdated(); return true; }
        }
        QString finalColor = color.isEmpty() ? "#2d2d2d" : color;
        QStringList finalTags = tags;
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
        query.bindValue(":tags", finalTags.join(","));
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
        syncFts(newNoteMap["id"].toInt(), title, content);
        incrementUsageCount(); // 每次增加笔记视为一次使用
        emit noteAdded(newNoteMap);
    }
    return success;
}

bool DatabaseManager::updateNote(int id, const QString& title, const QString& content, const QStringList& tags, const QString& color, int categoryId) {
    bool success = false;
    QString currentTime = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    {
        QMutexLocker locker(&m_mutex);
        if (!m_db.isOpen()) return false;
        QSqlQuery query(m_db);
        QString sql = "UPDATE notes SET title=:title, content=:content, tags=:tags, updated_at=:updated_at";
        if (!color.isEmpty()) sql += ", color=:color";
        else if (categoryId != -1) {
            QSqlQuery catQuery(m_db);
            catQuery.prepare("SELECT color FROM categories WHERE id = :id");
            catQuery.bindValue(":id", categoryId);
            if (catQuery.exec() && catQuery.next()) sql += ", color=:color";
        }
        if (categoryId != -1) sql += ", category_id=:category_id";
        sql += " WHERE id=:id";
        query.prepare(sql);
        query.bindValue(":title", title);
        query.bindValue(":content", content);
        query.bindValue(":tags", tags.join(","));
        query.bindValue(":updated_at", currentTime);
        if (!color.isEmpty()) query.bindValue(":color", color);
        else if (categoryId != -1) {
            QSqlQuery catQuery(m_db);
            catQuery.prepare("SELECT color FROM categories WHERE id = :id");
            catQuery.bindValue(":id", categoryId);
            if (catQuery.exec() && catQuery.next()) query.bindValue(":color", catQuery.value(0).toString());
        }
        if (categoryId != -1) query.bindValue(":category_id", categoryId);
        query.bindValue(":id", id);
        success = query.exec();
    }
    if (success) { syncFts(id, title, content); emit noteUpdated(); }
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
        QSqlQuery query(m_db);
        success = query.exec("UPDATE notes SET is_deleted = 0, category_id = NULL, color = '#0A362F' WHERE is_deleted = 1");
    }
    if (success) { emit noteUpdated(); emit categoriesChanged(); }
    return success;
}

bool DatabaseManager::updateNoteState(int id, const QString& column, const QVariant& value) {
    bool success = false;
    QString title, content;
    bool needsFts = false;
    QString currentTime = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    {
        QMutexLocker locker(&m_mutex);
        if (!m_db.isOpen()) return false;
        QStringList allowedColumns = {"is_pinned", "is_locked", "is_favorite", "is_deleted", "tags", "rating", "category_id", "color", "content", "title"};
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
        if (success && (column == "content" || column == "title")) {
            needsFts = true;
            QSqlQuery fetch(m_db);
            fetch.prepare("SELECT title, content FROM notes WHERE id = ?");
            fetch.addBindValue(id);
            if (fetch.exec() && fetch.next()) { title = fetch.value(0).toString(); content = fetch.value(1).toString(); }
        }
    } 
    if (success) { if (needsFts) syncFts(id, title, content); emit noteUpdated(); }
    return success;
}

bool DatabaseManager::updateNoteStateBatch(const QList<int>& ids, const QString& column, const QVariant& value) {
    if (ids.isEmpty()) return true;
    bool success = false;
    QString currentTime = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    {
        QMutexLocker locker(&m_mutex);
        if (!m_db.isOpen()) return false;
        QStringList allowedColumns = {"is_pinned", "is_locked", "is_favorite", "is_deleted", "tags", "rating", "category_id"};
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
            query.prepare("UPDATE notes SET category_id = :val, color = :color, is_deleted = 0, updated_at = :updated_at WHERE id = :id");
            for (int id : ids) {
                query.bindValue(":val", value);
                query.bindValue(":color", color);
                query.bindValue(":updated_at", currentTime);
                query.bindValue(":id", id);
                query.exec();
            }
        } else {
            QString sql = QString("UPDATE notes SET %1 = :val, updated_at = :updated_at WHERE id = :id").arg(column);
            query.prepare(sql);
            for (int id : ids) {
                query.bindValue(":val", value);
                query.bindValue(":updated_at", currentTime);
                query.bindValue(":id", id);
                query.exec();
            }
        }
        success = m_db.commit();
    }
    if (success) emit noteUpdated();
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

bool DatabaseManager::moveNoteToCategory(int noteId, int catId) { return moveNotesToCategory({noteId}, catId); }
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
                    if (changed) { QSqlQuery updateTags(m_db); updateTags.prepare("UPDATE notes SET tags = :tags WHERE id = :id"); updateTags.bindValue(":tags", tagList.join(",")); updateTags.bindValue(":id", id); updateTags.exec(); }
                }
            }
        }
        success = m_db.commit();
    }
    if (success) emit noteUpdated();
    return success;
}

bool DatabaseManager::deleteNote(int id) {
    bool success = false;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_db.isOpen()) return false;
        QSqlQuery query(m_db);
        query.prepare("DELETE FROM notes WHERE id=:id");
        query.bindValue(":id", id);
        success = query.exec();
        if (success) removeFts(id);
    } 
    if (success) emit noteUpdated();
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
    if (success) emit noteUpdated();
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
        query.prepare("UPDATE notes SET is_deleted = 1, category_id = NULL, color = '#2d2d2d', is_pinned = 0, is_favorite = 0, updated_at = :now WHERE id = :id");
        for (int id : ids) { query.bindValue(":now", currentTime); query.bindValue(":id", id); query.exec(); }
        success = m_db.commit();
    }
    if (success) emit noteUpdated();
    return success;
}

void DatabaseManager::addNoteAsync(const QString& title, const QString& content, const QStringList& tags, const QString& color, int categoryId, const QString& itemType, const QByteArray& dataBlob, const QString& sourceApp, const QString& sourceTitle) {
    QMetaObject::invokeMethod(this, [this, title, content, tags, color, categoryId, itemType, dataBlob, sourceApp, sourceTitle]() { addNote(title, content, tags, color, categoryId, itemType, dataBlob, sourceApp, sourceTitle); }, Qt::QueuedConnection);
}

QList<QVariantMap> DatabaseManager::searchNotes(const QString& keyword, const QString& filterType, const QVariant& filterValue, int page, int pageSize, const QVariantMap& criteria) {
    QMutexLocker locker(&m_mutex);
    QList<QVariantMap> results;
    if (!m_db.isOpen()) return results;
    QString baseSql = "SELECT notes.* FROM notes ";
    QString whereClause = "WHERE is_deleted = 0 ";
    QVariantList params;
    applySecurityFilter(whereClause, params, filterType);
    if (!criteria.isEmpty()) {
        if (criteria.contains("stars")) { QStringList stars = criteria.value("stars").toStringList(); if (!stars.isEmpty()) whereClause += QString("AND rating IN (%1) ").arg(stars.join(",")); }
        if (criteria.contains("types")) { QStringList types = criteria.value("types").toStringList(); if (!types.isEmpty()) { QStringList placeholders; for (const auto& t : types) { placeholders << "?"; params << t; } whereClause += QString("AND item_type IN (%1) ").arg(placeholders.join(",")); } }
        if (criteria.contains("colors")) { QStringList colors = criteria.value("colors").toStringList(); if (!colors.isEmpty()) { QStringList placeholders; for (const auto& c : colors) { placeholders << "?"; params << c; } whereClause += QString("AND color IN (%1) ").arg(placeholders.join(",")); } }
        if (criteria.contains("tags")) { QStringList tags = criteria.value("tags").toStringList(); if (!tags.isEmpty()) { QStringList tagConds; for (const auto& t : tags) { tagConds << "(',' || tags || ',') LIKE ?"; params << "%," + t.trimmed() + ",%"; } whereClause += QString("AND (%1) ").arg(tagConds.join(" OR ")); } }
        if (criteria.contains("date_create")) { QStringList dates = criteria.value("date_create").toStringList(); if (!dates.isEmpty()) { QStringList dateConds; for (const auto& d : dates) { if (d == "today") dateConds << "date(created_at) = date('now', 'localtime')"; else if (d == "yesterday") dateConds << "date(created_at) = date('now', '-1 day', 'localtime')"; else if (d == "week") dateConds << "date(created_at) >= date('now', '-6 days', 'localtime')"; else if (d == "month") dateConds << "strftime('%Y-%m', created_at) = strftime('%Y-%m', 'now', 'localtime')"; } if (!dateConds.isEmpty()) whereClause += QString("AND (%1) ").arg(dateConds.join(" OR ")); } }
    }
    if (filterType == "category") { if (filterValue.toInt() == -1) whereClause += "AND category_id IS NULL "; else { whereClause += "AND category_id = ? "; params << filterValue.toInt(); } }
    else if (filterType == "today") whereClause += "AND date(created_at) = date('now', 'localtime') ";
    else if (filterType == "yesterday") whereClause += "AND date(created_at) = date('now', '-1 day', 'localtime') ";
    else if (filterType == "recently_visited") whereClause += "AND (date(last_accessed_at) = date('now', 'localtime') OR date(updated_at) = date('now', 'localtime')) AND date(created_at) < date('now', 'localtime') ";
    else if (filterType == "bookmark") whereClause += "AND is_favorite = 1 ";
    else if (filterType == "trash") whereClause = "WHERE is_deleted = 1 ";
    else if (filterType == "untagged") whereClause += "AND (tags IS NULL OR tags = '') ";
    if (!keyword.isEmpty()) { whereClause += "AND (notes.tags LIKE ? OR notes.id IN (SELECT rowid FROM notes_fts WHERE notes_fts MATCH ?)) "; params << "%" + keyword + "%"; QString safeKeyword = keyword; safeKeyword.replace("\"", "\"\""); params << "\"" + safeKeyword + "\""; }
    QString finalSql = baseSql + whereClause + "ORDER BY ";
    if (!keyword.isEmpty()) { finalSql += "CASE WHEN notes.tags LIKE ? THEN 0 ELSE 1 END, "; params << "%" + keyword + "%"; }
    if (filterType == "recently_visited") finalSql += "is_pinned DESC, last_accessed_at DESC";
    else finalSql += "is_pinned DESC, updated_at DESC";
    if (page > 0 && filterType != "trash") finalSql += QString(" LIMIT %1 OFFSET %2").arg(pageSize).arg((page - 1) * pageSize);
    QSqlQuery query(m_db);
    query.prepare(finalSql);
    for (int i = 0; i < params.size(); ++i) query.bindValue(i, params[i]);
    if (query.exec()) { while (query.next()) { QVariantMap map; QSqlRecord rec = query.record(); for (int i = 0; i < rec.count(); ++i) map[rec.fieldName(i)] = query.value(i); results.append(map); } }
    else qCritical() << "searchNotes failed:" << query.lastError().text();
    return results;
}

int DatabaseManager::getNotesCount(const QString& keyword, const QString& filterType, const QVariant& filterValue, const QVariantMap& criteria) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return 0;
    QString baseSql = "SELECT COUNT(*) FROM notes ";
    QString whereClause = "WHERE is_deleted = 0 ";
    QVariantList params;
    applySecurityFilter(whereClause, params, filterType);
    if (!criteria.isEmpty()) {
        if (criteria.contains("stars")) { QStringList stars = criteria.value("stars").toStringList(); if (!stars.isEmpty()) whereClause += QString("AND rating IN (%1) ").arg(stars.join(",")); }
        if (criteria.contains("types")) { QStringList types = criteria.value("types").toStringList(); if (!types.isEmpty()) { QStringList placeholders; for (const auto& t : types) { placeholders << "?"; params << t; } whereClause += QString("AND item_type IN (%1) ").arg(placeholders.join(",")); } }
        if (criteria.contains("colors")) { QStringList colors = criteria.value("colors").toStringList(); if (!colors.isEmpty()) { QStringList placeholders; for (const auto& c : colors) { placeholders << "?"; params << c; } whereClause += QString("AND color IN (%1) ").arg(placeholders.join(",")); } }
        if (criteria.contains("tags")) { QStringList tags = criteria.value("tags").toStringList(); if (!tags.isEmpty()) { QStringList tagConds; for (const auto& t : tags) { tagConds << "(',' || tags || ',') LIKE ?"; params << "%," + t.trimmed() + ",%"; } whereClause += QString("AND (%1) ").arg(tagConds.join(" OR ")); } }
        if (criteria.contains("date_create")) { QStringList dates = criteria.value("date_create").toStringList(); if (!dates.isEmpty()) { QStringList dateConds; for (const auto& d : dates) { if (d == "today") dateConds << "date(created_at) = date('now', 'localtime')"; else if (d == "yesterday") dateConds << "date(created_at) = date('now', '-1 day', 'localtime')"; else if (d == "week") dateConds << "date(created_at) >= date('now', '-6 days', 'localtime')"; else if (d == "month") dateConds << "strftime('%Y-%m', created_at) = strftime('%Y-%m', 'now', 'localtime')"; } if (!dateConds.isEmpty()) whereClause += QString("AND (%1) ").arg(dateConds.join(" OR ")); } }
    }
    if (filterType == "category") { if (filterValue.toInt() == -1) whereClause += "AND category_id IS NULL "; else { whereClause += "AND category_id = ? "; params << filterValue.toInt(); } }
    else if (filterType == "today") whereClause += "AND date(created_at) = date('now', 'localtime') ";
    else if (filterType == "yesterday") whereClause += "AND date(created_at) = date('now', '-1 day', 'localtime') ";
    else if (filterType == "recently_visited") whereClause += "AND date(last_accessed_at) = date('now', 'localtime') AND date(created_at) < date('now', 'localtime') ";
    else if (filterType == "bookmark") whereClause += "AND is_favorite = 1 ";
    else if (filterType == "trash") whereClause = "WHERE is_deleted = 1 ";
    else if (filterType == "untagged") whereClause += "AND (tags IS NULL OR tags = '') ";
    if (!keyword.isEmpty()) { whereClause += "AND (notes.tags LIKE ? OR notes.id IN (SELECT rowid FROM notes_fts WHERE notes_fts MATCH ?)) "; params << "%" + keyword + "%"; QString safeKeyword = keyword; safeKeyword.replace("\"", "\"\""); params << "\"" + safeKeyword + "\""; }
    QSqlQuery query(m_db);
    query.prepare(baseSql + whereClause);
    for (int i = 0; i < params.size(); ++i) query.bindValue(i, params[i]);
    if (query.exec()) { if (query.next()) return query.value(0).toInt(); }
    else qCritical() << "getNotesCount failed:" << query.lastError().text();
    return 0;
}

QList<QVariantMap> DatabaseManager::getAllNotes() {
    QMutexLocker locker(&m_mutex);
    QList<QVariantMap> results;
    if (!m_db.isOpen()) return results;
    QSqlQuery catQuery(m_db);
    catQuery.exec("SELECT id FROM categories WHERE password IS NOT NULL AND password != ''");
    QList<int> lockedIds;
    while (catQuery.next()) { int cid = catQuery.value(0).toInt(); if (!m_unlockedCategories.contains(cid)) lockedIds.append(cid); }
    QString sql = "SELECT * FROM notes WHERE is_deleted = 0 ";
    if (!lockedIds.isEmpty()) { QStringList ids; for (int id : lockedIds) ids << QString::number(id); sql += QString("AND (category_id IS NULL OR category_id NOT IN (%1)) ").arg(ids.join(",")); }
    sql += "ORDER BY is_pinned DESC, updated_at DESC";
    QSqlQuery query(m_db);
    if (query.exec(sql)) { while (query.next()) { QVariantMap map; QSqlRecord rec = query.record(); for (int i = 0; i < rec.count(); ++i) map[rec.fieldName(i)] = query.value(i); results.append(map); } }
    return results;
}

QStringList DatabaseManager::getAllTags() {
    QMutexLocker locker(&m_mutex);
    QStringList allTags;
    if (!m_db.isOpen()) return allTags;
    QSqlQuery query(m_db);
    if (query.exec("SELECT tags FROM notes WHERE tags != '' AND is_deleted = 0")) { while (query.next()) { QString tagsStr = query.value(0).toString(); QStringList parts = tagsStr.split(",", Qt::SkipEmptyParts); for (const QString& part : parts) { QString trimmed = part.trimmed(); if (!allTags.contains(trimmed)) allTags.append(trimmed); } } }
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
    if (query.exec("SELECT tags, updated_at FROM notes WHERE tags != '' AND is_deleted = 0")) { while (query.next()) { QString tagsStr = query.value(0).toString(); QDateTime updatedAt = query.value(1).toDateTime(); QStringList parts = tagsStr.split(",", Qt::SkipEmptyParts); for (const QString& part : parts) { QString name = part.trimmed(); if (name.isEmpty()) continue; if (!tagMap.contains(name)) tagMap[name] = {name, 1, updatedAt}; else { tagMap[name].count++; if (updatedAt > tagMap[name].lastUsed) tagMap[name].lastUsed = updatedAt; } } } }
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
    bool success = false;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_db.isOpen()) return false;
        QSqlQuery query(m_db);
        query.prepare("DELETE FROM categories WHERE id=:id");
        query.bindValue(":id", id);
        if (query.exec()) { QSqlQuery updateNotes(m_db); updateNotes.prepare("UPDATE notes SET category_id = NULL WHERE category_id = :id"); updateNotes.bindValue(":id", id); updateNotes.exec(); success = true; }
    }
    if (success) emit categoriesChanged();
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
    if (query.exec("SELECT * FROM categories ORDER BY sort_order")) { while (query.next()) { QVariantMap map; QSqlRecord rec = query.record(); for (int i = 0; i < rec.count(); ++i) map[rec.fieldName(i)] = query.value(i); results.append(map); } }
    return results;
}

bool DatabaseManager::emptyTrash() {
    bool success = false;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_db.isOpen()) return false;
        QSqlQuery query(m_db);
        success = query.exec("DELETE FROM notes WHERE is_deleted = 1");
    }
    if (success) emit noteUpdated();
    return success;
}

bool DatabaseManager::setCategoryPresetTags(int catId, const QString& tags) {
    bool ok = false;
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
                    if (changed) { QSqlQuery updateNote(m_db); updateNote.prepare("UPDATE notes SET tags = :tags WHERE id = :id"); updateNote.bindValue(":tags", existingTags.join(",")); updateNote.bindValue(":id", noteId); updateNote.exec(); }
                }
            }
        }
        ok = m_db.commit();
    }
    if (ok) { emit categoriesChanged(); emit noteUpdated(); }
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
    counts["recently_visited"] = getCount("is_deleted = 0 AND (date(last_accessed_at) = date('now', 'localtime') OR date(updated_at) = date('now', 'localtime')) AND date(created_at) < date('now', 'localtime')");
    counts["uncategorized"] = getCount("is_deleted = 0 AND category_id IS NULL");
    counts["untagged"] = getCount("is_deleted = 0 AND (tags IS NULL OR tags = '')");
    counts["bookmark"] = getCount("is_deleted = 0 AND is_favorite = 1");
    counts["trash"] = getCount("is_deleted = 1", false);
    if (query.exec("SELECT category_id, COUNT(*) FROM notes WHERE is_deleted = 0 AND category_id IS NOT NULL GROUP BY category_id")) { while (query.next()) { counts["cat_" + query.value(0).toString()] = query.value(1).toInt(); } }
    return counts;
}

QVariantMap DatabaseManager::getTrialStatus() {
    QMutexLocker locker(&m_mutex);
    QVariantMap status;
    status["expired"] = false;
    status["usage_limit_reached"] = false;
    status["days_left"] = 30;
    status["usage_count"] = 0;

    if (!m_db.isOpen()) return status;

    QSqlQuery query(m_db);
    query.exec("SELECT key, value FROM system_config");
    while (query.next()) {
        QString key = query.value(0).toString();
        QString value = query.value(1).toString();
        if (key == "first_launch_date") {
            QDateTime firstLaunch = QDateTime::fromString(value, Qt::ISODate);
            qint64 daysPassed = firstLaunch.daysTo(QDateTime::currentDateTime());
            status["days_left"] = qMax(0LL, 30 - daysPassed);
            if (daysPassed > 30) status["expired"] = true;
        } else if (key == "usage_count") {
            int count = value.toInt();
            status["usage_count"] = count;
            if (count >= 1000) status["usage_limit_reached"] = true;
        }
    }
    return status;
}

void DatabaseManager::incrementUsageCount() {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return;
    QSqlQuery query(m_db);
    query.exec("UPDATE system_config SET value = CAST(CAST(value AS INTEGER) + 1 AS TEXT) WHERE key = 'usage_count'");
}

QVariantMap DatabaseManager::getFilterStats(const QString& keyword, const QString& filterType, const QVariant& filterValue, const QVariantMap& criteria) {
    QMutexLocker locker(&m_mutex);
    QVariantMap stats;
    if (!m_db.isOpen()) return stats;
    QString whereClause = "WHERE 1=1 ";
    QVariantList params;
    if (filterType == "trash") whereClause += "AND is_deleted = 1 ";
    else {
        whereClause += "AND is_deleted = 0 ";
        applySecurityFilter(whereClause, params, filterType);
        if (filterType == "category") { if (filterValue.toInt() == -1) whereClause += "AND category_id IS NULL "; else { whereClause += "AND category_id = ? "; params << filterValue.toInt(); } }
        else if (filterType == "today") whereClause += "AND date(created_at) = date('now', 'localtime') ";
        else if (filterType == "yesterday") whereClause += "AND date(created_at) = date('now', '-1 day', 'localtime') ";
        else if (filterType == "recently_visited") whereClause += "AND (date(last_accessed_at) = date('now', 'localtime') OR date(updated_at) = date('now', 'localtime')) AND date(created_at) < date('now', 'localtime') ";
        else if (filterType == "bookmark") whereClause += "AND is_favorite = 1 ";
        else if (filterType == "untagged") whereClause += "AND (tags IS NULL OR tags = '') ";
    }
    if (!keyword.isEmpty()) { whereClause += "AND id IN (SELECT rowid FROM notes_fts WHERE notes_fts MATCH ?) "; params << keyword; }
    QSqlQuery query(m_db);
    QMap<int, int> stars;
    query.prepare("SELECT rating, COUNT(*) FROM notes " + whereClause + " GROUP BY rating");
    for (int i = 0; i < params.size(); ++i) query.bindValue(i, params[i]);
    if (query.exec()) { while (query.next()) stars[query.value(0).toInt()] = query.value(1).toInt(); }
    QVariantMap starsMap;
    for (auto it = stars.begin(); it != stars.end(); ++it) starsMap[QString::number(it.key())] = it.value();
    stats["stars"] = starsMap;
    QMap<QString, int> colors;
    query.prepare("SELECT color, COUNT(*) FROM notes " + whereClause + " GROUP BY color");
    for (int i = 0; i < params.size(); ++i) query.bindValue(i, params[i]);
    if (query.exec()) { while (query.next()) colors[query.value(0).toString()] = query.value(1).toInt(); }
    QVariantMap colorsMap;
    for (auto it = colors.begin(); it != colors.end(); ++it) colorsMap[it.key()] = it.value();
    stats["colors"] = colorsMap;
    QMap<QString, int> types;
    query.prepare("SELECT item_type, COUNT(*) FROM notes " + whereClause + " GROUP BY item_type");
    for (int i = 0; i < params.size(); ++i) query.bindValue(i, params[i]);
    if (query.exec()) { while (query.next()) types[query.value(0).toString()] = query.value(1).toInt(); }
    QVariantMap typesMap;
    for (auto it = types.begin(); it != types.end(); ++it) typesMap[it.key()] = it.value();
    stats["types"] = typesMap;
    QMap<QString, int> tags;
    query.prepare("SELECT tags FROM notes " + whereClause);
    for (int i = 0; i < params.size(); ++i) query.bindValue(i, params[i]);
    if (query.exec()) { while (query.next()) { QStringList parts = query.value(0).toString().split(",", Qt::SkipEmptyParts); for (const QString& t : parts) tags[t.trimmed()]++; } }
    QVariantMap tagsMap;
    for (auto it = tags.begin(); it != tags.end(); ++it) tagsMap[it.key()] = it.value();
    stats["tags"] = tagsMap;
    QVariantMap dateStats;
    auto getCountDate = [&](const QString& dateCond) {
        QSqlQuery q(m_db);
        q.prepare("SELECT COUNT(*) FROM notes " + whereClause + " AND " + dateCond);
        for (int i = 0; i < params.size(); ++i) q.bindValue(i, params[i]);
        if (q.exec() && q.next()) return q.value(0).toInt();
        return 0;
    };
    dateStats["today"] = getCountDate("date(created_at) = date('now', 'localtime')");
    dateStats["yesterday"] = getCountDate("date(created_at) = date('now', '-1 day', 'localtime')");
    dateStats["week"] = getCountDate("date(created_at) >= date('now', '-6 days', 'localtime')");
    dateStats["month"] = getCountDate("strftime('%Y-%m', created_at) = strftime('%Y-%m', 'now', 'localtime')");
    stats["date_create"] = dateStats;
    return stats;
}

bool DatabaseManager::addTagsToNote(int noteId, const QStringList& tags) { QVariantMap note = getNoteById(noteId); if (note.isEmpty()) return false; QStringList existing = note["tags"].toString().split(",", Qt::SkipEmptyParts); for (const QString& t : tags) { if (!existing.contains(t.trimmed())) existing.append(t.trimmed()); } return updateNoteState(noteId, "tags", existing.join(",")); }
bool DatabaseManager::removeTagFromNote(int noteId, const QString& tag) { QVariantMap note = getNoteById(noteId); if (note.isEmpty()) return false; QStringList existing = note["tags"].toString().split(",", Qt::SkipEmptyParts); existing.removeAll(tag.trimmed()); return updateNoteState(noteId, "tags", existing.join(",")); }
bool DatabaseManager::renameTagGlobally(const QString& oldName, const QString& newName) {
    if (oldName.trimmed().isEmpty() || oldName == newName) return true;
    bool ok = false;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_db.isOpen()) return false;
        m_db.transaction();
        QSqlQuery query(m_db);
        query.prepare("SELECT id, tags FROM notes WHERE (',' || tags || ',') LIKE ? AND is_deleted = 0");
        query.addBindValue("%," + oldName.trimmed() + ",%");
        if (query.exec()) {
            while (query.next()) {
                int noteId = query.value(0).toInt(); QString tagsStr = query.value(1).toString(); QStringList tagList = tagsStr.split(",", Qt::SkipEmptyParts);
                for (int i = 0; i < tagList.size(); ++i) { if (tagList[i].trimmed() == oldName.trimmed()) tagList[i] = newName.trimmed(); }
                tagList.removeDuplicates();
                QSqlQuery updateQuery(m_db); updateQuery.prepare("UPDATE notes SET tags = ? WHERE id = ?"); updateQuery.addBindValue(tagList.join(",")); updateQuery.addBindValue(noteId); updateQuery.exec();
            }
        }
        ok = m_db.commit();
    }
    if (ok) emit noteUpdated();
    return ok;
}

bool DatabaseManager::deleteTagGlobally(const QString& tagName) {
    if (tagName.trimmed().isEmpty()) return true;
    bool ok = false;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_db.isOpen()) return false;
        m_db.transaction();
        QSqlQuery query(m_db);
        query.prepare("SELECT id, tags FROM notes WHERE (',' || tags || ',') LIKE ? AND is_deleted = 0");
        query.addBindValue("%," + tagName.trimmed() + ",%");
        if (query.exec()) {
            while (query.next()) {
                int noteId = query.value(0).toInt(); QString tagsStr = query.value(1).toString(); QStringList tagList = tagsStr.split(",", Qt::SkipEmptyParts);
                tagList.removeAll(tagName.trimmed());
                QSqlQuery updateQuery(m_db); updateQuery.prepare("UPDATE notes SET tags = ? WHERE id = ?"); updateQuery.addBindValue(tagList.join(",")); updateQuery.addBindValue(noteId); updateQuery.exec();
            }
        }
        ok = m_db.commit();
    }
    if (ok) emit noteUpdated();
    return ok;
}

void DatabaseManager::syncFts(int id, const QString& title, const QString& content) {
    QString plainTitle = title; QString plainContent = stripHtml(content);
    QMutexLocker locker(&m_mutex);
    QSqlQuery query(m_db);
    query.prepare("DELETE FROM notes_fts WHERE rowid = ?"); query.addBindValue(id); query.exec();
    query.prepare("INSERT INTO notes_fts(rowid, title, content) VALUES (?, ?, ?)"); query.addBindValue(id); query.addBindValue(plainTitle); query.addBindValue(plainContent); query.exec();
}

void DatabaseManager::removeFts(int id) { QSqlQuery query(m_db); query.prepare("DELETE FROM notes_fts WHERE rowid = ?"); query.addBindValue(id); query.exec(); }

void DatabaseManager::applySecurityFilter(QString& whereClause, QVariantList& params, const QString& filterType) {
    if (filterType == "category" || filterType == "trash") return;
    QSqlQuery catQuery(m_db);
    catQuery.exec("SELECT id FROM categories WHERE password IS NOT NULL AND password != ''");
    QList<int> lockedIds;
    while (catQuery.next()) { int cid = catQuery.value(0).toInt(); if (!m_unlockedCategories.contains(cid)) lockedIds.append(cid); }
    if (!lockedIds.isEmpty()) {
        QStringList placeholders; for (int i = 0; i < lockedIds.size(); ++i) placeholders << "?";
        whereClause += QString("AND (category_id IS NULL OR category_id NOT IN (%1)) ").arg(placeholders.join(","));
        for (int id : lockedIds) params << id;
    }
}

QString DatabaseManager::stripHtml(const QString& html) {
    if (!html.contains("<") && !html.contains("&")) return html;
    QString plain = html;
    plain.remove(QRegularExpression("<style.*?>.*?</style>", QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption));
    plain.remove(QRegularExpression("<script.*?>.*?</script>", QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption));
    plain.remove(QRegularExpression("<[^>]*>"));
    plain.replace("&nbsp;", " ", Qt::CaseInsensitive);
    plain.replace("&lt;", "<", Qt::CaseInsensitive);
    plain.replace("&gt;", ">", Qt::CaseInsensitive);
    plain.replace("&amp;", "&", Qt::CaseInsensitive);
    plain.replace("&quot;", "\"", Qt::CaseInsensitive);
    plain.replace("&#39;", "'");
    return plain.simplified();
}
