#include "DatabaseManager.h"
#include <QDebug>
#include <QSqlRecord>
#include <QSqlError>
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QDir>
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QSet>
#include <QRegularExpression>
#include <QFileInfo>
#include <QStandardPaths>
#include <QSettings>
#include <QThread>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QtConcurrent>
#include <QThreadPool>
#include <QMessageBox>
#include <utility>
#include <algorithm>
#include "FileCryptoHelper.h"
#include "HardwareInfoHelper.h"
#include "ClipboardMonitor.h"
#include "../ui/StringUtils.h"
#include "../ui/FramelessDialog.h"

namespace {
    // 2026-04-08 按照用户要求：建立后缀名白名单，杜绝将非扩展名的标题内容误判为类型
    const QSet<QString> VALID_EXTENSIONS = {
        // 音频
        "mp1", "mp2", "mp3", "aac", "m4a", "m4r", "wav", "flac", "ape", "alac", "wma", "ogg", "oga", "ogx", "mpc", "ra", "rm", "ram", "mid", "midi", "aiff", "aif", "amr", "awb", "gsm", "vox", "wv", "cda", "au", "snd", "opus", "spx", "caf", "dsf", "dff",
        // 视频
        "avi", "mpg", "mpeg", "mp4", "m4v", "mov", "qt", "wmv", "asf", "flv", "f4v", "mkv", "webm", "3gp", "3g2", "rmvb", "vob", "ts", "m2ts", "mts", "ogv", "divx", "xvid", "dv", "mxf", "amv", "svi", "mpv",
        // 图形/图像
        "jpg", "jpeg", "png", "gif", "bmp", "tiff", "tif", "webp", "svg", "ico", "heif", "heic", "raw", "cr2", "nef", "orf", "sr2", "psd", "ai", "eps", "pdf",
        // 程序/脚本
        "exe", "com", "bat", "cmd", "sh", "bash", "ps1", "psm1", "vbs", "vb", "js", "ts", "jsx", "tsx", "py", "pyw", "pyc", "pyo", "rb", "pl", "pm", "php", "phar", "java", "class", "jar", "c", "cpp", "h", "hpp", "cs", "go", "rs", "swift", "kt", "kts", "scala", "sc", "lua", "r", "m", "mm", "sql", "asm", "s", "clj", "cljs", "groovy", "dart", "erl", "ex", "exs", "f", "for", "fs", "fsi", "fsx", "ml", "ocaml", "pas", "pp", "d",
        // 文档
        "txt", "text", "doc", "docx", "odt", "rtf", "wps", "wpd", "md", "markdown", "tex", "epub", "mobi", "azw", "djvu", "fb2", "ppt", "pptx", "odp", "key", "xls", "xlsx", "ods", "csv", "tsv", "log", "ini", "cfg", "json", "xml", "yaml", "yml"
    };

    // 2026-04-08 按照用户要求：物理解析多后缀关联，支持文件夹判定与 Link 强制分类
    QString extractFileExtensions(const QString& itemType, const QString& content) {
        if (itemType == "link") return "Link";
        
        // 仅处理文件/文件夹相关类型
        if (itemType != "file" && itemType != "local_file" && itemType != "local_batch" && itemType != "folder" && itemType != "local_folder") {
            return "";
        }
        
        QStringList paths = content.split(";", Qt::SkipEmptyParts);
        QSet<QString> extensions;
        for (const QString& path : paths) {
            QFileInfo info(path);
            if (info.isDir()) {
                extensions.insert("文件夹");
            } else {
                QString ext = info.suffix().toLower();
                if (!ext.isEmpty()) extensions.insert(ext);
            }
        }
        
        QStringList result = extensions.values();
        result.sort();
        return result.join(", ");
    }

}

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
    m_lockedCategoriesHidden = settings.value("lockedCategoriesHidden", false).toBool();

    m_autoSaveTimer = new QTimer(this);
    m_autoSaveTimer->setInterval(7000); // 7秒增量同步间隔
    connect(m_autoSaveTimer, &QTimer::timeout, this, &DatabaseManager::handleAutoSave);
}

DatabaseManager::~DatabaseManager() {
    closeAndPack();
}

bool DatabaseManager::init(const QString& dbPath) {
    m_realDbPath = dbPath;
    
    // 采用“内存内核 + 磁盘外壳”的高速缓存架构
    m_dbPath = ":memory:"; // 暂时使用内存模式提升速度
    m_db = QSqlDatabase::addDatabase("QSQLITE");
    m_db.setDatabaseName(m_dbPath);

    if (!m_db.open()) {
        m_lastError = m_db.lastError().text();
        return false;
    }

    // 启用 WAL 模式与同步优化
    QSqlQuery query(m_db);
    query.exec("PRAGMA journal_mode = WAL");
    query.exec("PRAGMA synchronous = NORMAL");
    query.exec("PRAGMA cache_size = -10000"); // 10MB 缓存

    if (!createTables()) return false;

    // 如果存在磁盘数据库，则加载到内存中 (此处简化，实际应有加载逻辑)
    if (QFile::exists(m_realDbPath)) {
        // [HEALING] 此处应实现从 m_realDbPath 到内存库的原子同步
    }

    m_isInitialized = true;
    return true;
}

void DatabaseManager::logStartup(const QString& msg) {
    qDebug() << "[DB STARTUP]" << msg;
}

void DatabaseManager::closeAndPack() {
    if (m_db.isOpen()) {
        m_db.close();
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
            last_accessed_at DATETIME,
            sort_order INTEGER DEFAULT 0,
            remark TEXT DEFAULT '',
            file_extensions TEXT DEFAULT '',
            word_count INTEGER DEFAULT 0
        )
    )";
    if (!query.exec(createNotesTable)) return false;

    QString createCategoriesTable = R"(
        CREATE TABLE IF NOT EXISTS categories (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT,
            parent_id INTEGER DEFAULT -1,
            color TEXT DEFAULT '',
            password TEXT DEFAULT '',
            password_hint TEXT DEFAULT '',
            is_pinned INTEGER DEFAULT 0,
            is_deleted INTEGER DEFAULT 0,
            sort_order INTEGER DEFAULT 0,
            preset_tags TEXT DEFAULT ''
        )
    )";
    if (!query.exec(createCategoriesTable)) return false;

    // FTS5 全文搜索表
    query.exec("CREATE VIRTUAL TABLE IF NOT EXISTS notes_fts USING fts5(title, content, tags, content='notes', content_rowid='id')");

    // 触发器同步
    QString wcExpr = "length(REPLACE(REPLACE(REPLACE(new.content, '<p>', ''), '</p>', ''), '<br/>', ''))";
    
    query.exec(QString(R"(
        CREATE TRIGGER IF NOT EXISTS trg_notes_insert AFTER INSERT ON notes BEGIN
            INSERT INTO notes_fts(rowid, title, content, tags)
            VALUES (new.id, new.title, new.content, new.tags);
            UPDATE notes SET word_count = %1 WHERE id = new.id;
        END;
    )").arg(wcExpr));

    query.exec(QString(R"(
        CREATE TRIGGER IF NOT EXISTS trg_notes_update AFTER UPDATE ON notes
        FOR EACH ROW WHEN (old.title != new.title OR old.content != new.content OR old.tags != new.tags)
        BEGIN
            INSERT INTO notes_fts(notes_fts, rowid, title, content, tags)
            VALUES ('delete', old.id, old.title, old.content, old.tags);
            INSERT INTO notes_fts(rowid, title, content, tags)
            VALUES (new.id, new.title, new.content, new.tags);
            UPDATE notes SET word_count = %1 WHERE id = new.id;
        END;
    )").arg(wcExpr));

    return true;
}

QList<QVariantMap> DatabaseManager::searchNotes(const QString& keyword, const QString& filterType, const QVariant& filterValue, int page, int pageSize, const QVariantMap& criteria) {
    QMutexLocker locker(&m_mutex);
    QList<QVariantMap> results;
    if (!m_db.isOpen()) return results;

    QString baseSql = "SELECT notes.* FROM notes ";
    if (!keyword.isEmpty()) {
        baseSql = "SELECT notes.* FROM notes JOIN notes_fts ON notes.id = notes_fts.rowid ";
    }

    QString whereClause;
    QVariantList params;
    applyCommonFilters(whereClause, params, filterType, filterValue, criteria);
    
    if (!keyword.isEmpty()) {
        whereClause += "AND notes_fts MATCH ? ";
        params << sanitizeFtsKeyword(keyword);
    }
    
    QString finalSql = baseSql + whereClause + " ORDER BY is_pinned DESC, updated_at DESC";
    if (page > 0) {
        finalSql += QString(" LIMIT %1 OFFSET %2").arg(pageSize).arg((page - 1) * pageSize);
    }

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
    return results;
}

int DatabaseManager::getNotesCount(const QString& keyword, const QString& filterType, const QVariant& filterValue, const QVariantMap& criteria) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return 0;

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
    query.prepare("SELECT COUNT(*) " + baseSql + whereClause);
    for (int i = 0; i < params.size(); ++i) query.bindValue(i, params[i]);

    if (query.exec() && query.next()) return query.value(0).toInt();
    return 0;
}

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

    // 1. 评级统计
    QMap<int, int> stars;
    query.prepare("SELECT rating, COUNT(*) " + baseSql + whereClause + " GROUP BY rating");
    for (int i = 0; i < params.size(); ++i) query.bindValue(i, params[i]);
    if (query.exec()) { while (query.next()) stars[query.value(0).toInt()] = query.value(1).toInt(); }
    QVariantMap starsMap;
    for (auto it = stars.begin(); it != stars.end(); ++it) starsMap[QString::number(it.key())] = it.value();
    stats["stars"] = starsMap;

    // 1.5 字数区间展示 (2026-04-xx 按照旧版本-3还原：100% 精确的 REPLACE 逻辑)
    QString wcSql = "length(REPLACE(REPLACE(REPLACE(content, '<p>', ''), '</p>', ''), '<br/>', ''))";
    QString wcFilter = " AND item_type = 'text' AND (tags NOT LIKE '%HEX%' AND tags NOT LIKE '%RGB%' AND tags NOT LIKE '%色码%') ";
    
    QString wcQuerySql = QString(
        "SELECT CASE "
        "WHEN %1 <= 10 THEN '10' "
        "WHEN %1 <= 20 THEN '20' "
        "WHEN %1 <= 30 THEN '30' "
        "WHEN %1 <= 40 THEN '40' "
        "WHEN %1 <= 50 THEN '50' "
        "WHEN %1 <= 60 THEN '60' "
        "WHEN %1 <= 70 THEN '70' "
        "WHEN %1 <= 80 THEN '80' "
        "WHEN %1 <= 90 THEN '90' "
        "WHEN %1 <= 100 THEN '100' "
        "ELSE '101' END as bucket, COUNT(*) "
        + baseSql + whereClause + wcFilter + " GROUP BY bucket"
    ).arg(wcSql);

    QSqlQuery wcQuery(m_db);
    wcQuery.prepare(wcQuerySql);
    for (int i = 0; i < params.size(); ++i) wcQuery.bindValue(i, params[i]);
    
    QVariantMap wcMap;
    if (wcQuery.exec()) {
        while (wcQuery.next()) wcMap[wcQuery.value(0).toString()] = wcQuery.value(1).toInt();
    }
    stats["word_count"] = wcMap;

    // 2. 颜色统计
    QMap<QString, int> colors;
    query.prepare("SELECT color, COUNT(*) " + baseSql + whereClause + " GROUP BY color");
    for (int i = 0; i < params.size(); ++i) query.bindValue(i, params[i]);
    if (query.exec()) { while (query.next()) colors[query.value(0).toString()] = query.value(1).toInt(); }
    QVariantMap colorsMap;
    for (auto it = colors.begin(); it != colors.end(); ++it) colorsMap[it.key()] = it.value();
    stats["colors"] = colorsMap;

    // 2.5 物理级多后缀统计
    QMap<QString, int> bizTypes;
    QSqlQuery typeQuery(m_db);
    typeQuery.prepare("SELECT item_type, file_extensions " + baseSql + whereClause);
    for (int i = 0; i < params.size(); ++i) typeQuery.bindValue(i, params[i]);
    if (typeQuery.exec()) {
        while (typeQuery.next()) {
            QString itemType = typeQuery.value(0).toString();
            QString exts = typeQuery.value(1).toString();
            if (!exts.isEmpty()) {
                QStringList parts = exts.split(",", Qt::SkipEmptyParts);
                for (const QString& e : parts) bizTypes[e.trimmed()]++;
            } else {
                if (itemType == "image") bizTypes["截图"]++;
                else if (itemType == "code") bizTypes["脚本代码"]++;
                else if (itemType == "text") bizTypes["纯文本"]++;
                else if (itemType == "link") bizTypes["Link"]++;
                else if (itemType == "file") bizTypes["数据库附件"]++;
                else if (itemType == "local_file") bizTypes["本地文件"]++;
                else if (itemType == "local_folder" || itemType == "folder") bizTypes["文件夹"]++;
                else if (itemType == "local_batch") bizTypes["批量托管"]++;
                else bizTypes["其他"]++;
            }
        }
    }
    QVariantMap typesMap;
    for (auto it = bizTypes.begin(); it != bizTypes.end(); ++it) typesMap[it.key()] = it.value();
    stats["types"] = typesMap;

    // 3. 标签统计
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

    // 4. 创建日期统计
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

    // 5. 修改日期统计
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

void DatabaseManager::applyCommonFilters(QString& whereClause, QVariantList& params, const QString& filterType, const QVariant& filterValue, const QVariantMap& criteria) {
    if (filterType == "trash") {
        whereClause = "WHERE is_deleted = 1 ";
    } else {
        whereClause = "WHERE is_deleted = 0 ";
        
        if (filterType == "category") { 
            if (filterValue.toInt() == -1) whereClause += "AND (category_id IS NULL OR category_id <= 0) "; 
            else { whereClause += "AND category_id = ? "; params << filterValue.toInt(); } 
        }
        else if (filterType == "uncategorized") {
            whereClause += "AND (category_id IS NULL OR category_id <= 0) ";
        }
        else if (filterType == "bookmark") whereClause += "AND is_favorite = 1 ";
        else if (filterType == "untagged") whereClause += "AND (tags IS NULL OR tags = '') ";
    }
    
    if (filterType != "trash" && !criteria.isEmpty()) {
        if (criteria.contains("stars")) { 
            QStringList stars = criteria.value("stars").toStringList(); 
            if (!stars.isEmpty()) whereClause += QString("AND rating IN (%1) ").arg(stars.join(", ")); 
        }
        if (criteria.contains("word_count")) {
            // 2026-04-xx 按照旧版本-3还原：100% 精确的 REPLACE 逻辑
            QStringList buckets = criteria.value("word_count").toStringList();
            if (!buckets.isEmpty()) {
                QString wcSql = "length(REPLACE(REPLACE(REPLACE(content, '<p>', ''), '</p>', ''), '<br/>', ''))";
                QStringList wcConds;
                for (const auto& b : buckets) {
                    int val = b.toInt();
                    if (val == 10) wcConds << QString("(%1 BETWEEN 0 AND 10)").arg(wcSql);
                    else if (val == 101) wcConds << QString("(%1 > 100)").arg(wcSql);
                    else wcConds << QString("(%1 BETWEEN %2 AND %3)").arg(wcSql).arg(val - 9).arg(val);
                }
                whereClause += QString("AND (%1) AND item_type = 'text' AND (tags NOT LIKE '%%HEX%%' AND tags NOT LIKE '%%RGB%%' AND tags NOT LIKE '%%色码%%') ").arg(wcConds.join(" OR "));
            }
        }
        if (criteria.contains("types")) { 
            QStringList typeCriteria = criteria.value("types").toStringList(); 
            if (!typeCriteria.isEmpty()) { 
                QStringList bizConds;
                for (const QString& label : typeCriteria) {
                    if (label == "截图") bizConds << "item_type = 'image'";
                    else if (label == "脚本代码") bizConds << "item_type = 'code'";
                    else if (label == "纯文本") bizConds << "item_type = 'text'";
                    else if (label == "Link") bizConds << "item_type = 'link'";
                    else if (label == "文件夹") bizConds << "item_type IN ('local_folder', 'folder')";
                    else {
                        bizConds << "(file_extensions LIKE ?)";
                        params << "%" + label.toLower() + "%";
                    }
                }
                if (!bizConds.isEmpty()) whereClause += QString("AND (%1) ").arg(bizConds.join(" OR "));
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
                    tagConds << "tags LIKE ?";
                    params << "%" + t.trimmed() + "%";
                } 
                whereClause += QString("AND (%1) ").arg(tagConds.join(" OR ")); 
            } 
        }
    }
}

QString DatabaseManager::sanitizeFtsKeyword(const QString& keyword) {
    QString sanitized = keyword;
    sanitized.replace("\"", "\"\"");
    return "\"" + sanitized + "*\"";
}

void DatabaseManager::handleAutoSave() {
    // 同步到磁盘的逻辑 (省略)
}

QList<QVariantMap> DatabaseManager::getAllCategories() {
    QMutexLocker locker(&m_mutex);
    QList<QVariantMap> results;
    QSqlQuery query("SELECT * FROM categories ORDER BY sort_order ASC", m_db);
    while (query.next()) {
        QVariantMap map;
        QSqlRecord rec = query.record();
        for (int i = 0; i < rec.count(); ++i) map[rec.fieldName(i)] = query.value(i);
        results.append(map);
    }
    return results;
}

bool DatabaseManager::isCategoryLocked(int id) {
    return false; // 简化
}
