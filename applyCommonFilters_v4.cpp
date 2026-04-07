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
        markDirty();
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
        // 2026-03-xx 按照用户要求修复逻辑：在排除锁定分类时，必须确保“未分类”项目（NULL 或 <=0）始终可见，不被误杀
        whereClause += QString("AND (category_id IS NULL OR category_id <= 0 OR category_id NOT IN (%1)) ").arg(placeholders.join(", "));
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
            // 2026-03-xx 按照用户要求修复傻逼逻辑：分类 ID 为 -1 时应视为“未分类”，统一查询口径
            if (filterValue.toInt() == -1) whereClause += "AND (category_id IS NULL OR category_id <= 0) ";
            else { whereClause += "AND category_id = ? "; params << filterValue.toInt(); }
        }
        else if (filterType == "uncategorized") {
            // 2026-03-xx 按照用户要求修复傻逼逻辑：统一“未分类”判定，防止物理删除分类后的笔记在恢复时变成“幽灵数据”
            whereClause += "AND (category_id IS NULL OR category_id <= 0) ";
        }
        else if (filterType == "today") {
            whereClause += "AND date(created_at) = ? ";
            params << QDate::currentDate().toString("yyyy-MM-dd");
        }
        else if (filterType == "yesterday") {
            whereClause += "AND date(created_at) = ? ";
            params << QDate::currentDate().addDays(-1).toString("yyyy-MM-dd");
        }
        else if (filterType == "recently_visited") {
            whereClause += "AND date(last_accessed_at) = ? ";
            params << QDate::currentDate().toString("yyyy-MM-dd");
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
            // 2026-04-xx 按照用户要求：字数区间多选逻辑
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
                // [FIX] 修正 LIKE 语法：SQLite 在 prepare 模式下不需要双百分号。
                // 启用字数筛选时，物理叠加“仅文本”与“非色码”约束。
                whereClause += QString("AND (%1) AND item_type = 'text' AND (tags NOT LIKE '%HEX%' AND tags NOT LIKE '%RGB%' AND tags NOT LIKE '%色码%') ").arg(wcConds.join(" OR "));
            }
        }
        if (criteria.contains("types")) {
            // 2026-04-06 按照用户要求：多维嵌套业务类型过滤逻辑
            // 支持格式: "Category" (全选) 或 "Category|Extension" (精选)
            QStringList typeCriteria = criteria.value("types").toStringList();
            if (!typeCriteria.isEmpty()) {
                QStringList bizConds;
                for (const QString& tc : typeCriteria) {
                    if (tc.contains('|')) {
                        QStringList parts = tc.split('|');
                        QString ext = parts[1];
                        if (ext == "图片数据") bizConds << "item_type = 'image'";
                        else if (ext == "脚本代码") bizConds << "item_type = 'code'";
                        else if (ext == "纯文本") bizConds << "item_type = 'text'";
                        else if (parts[0] == "其他") {
                            if (ext == "未知分类") bizConds << "(item_type IS NULL OR item_type = '' OR item_type = 'text')";
                            else if (ext == "link" || ext == "local_file" || ext == "local_folder" || ext == "local_batch" || ext == "file" || ext == "folder") {
                                bizConds << "item_type = ?";
                                params << ext;
                            } else {
                                bizConds << "title LIKE ?";
                                params << "%." + ext;
                            }
                        }
                        else {
