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

    // 1.5 精致字数聚合统计 (2026-04-xx 按照用户授权：HTML 脱壳计算)
    // 物理剔除标签对视觉字数的干扰，确保统计结果符合直觉
    QString wcSql = "length(REPLACE(REPLACE(REPLACE(content, '<p>', ''), '</p>', ''), '<br/>', ''))";
    // 业务隔离：统计阶段即剔除图片及包含色码标签的记录
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

    QMap<QString, int> colors;
    query.prepare("SELECT color, COUNT(*) " + baseSql + whereClause + " GROUP BY color");
    for (int i = 0; i < params.size(); ++i) query.bindValue(i, params[i]);
    if (query.exec()) { while (query.next()) colors[query.value(0).toString()] = query.value(1).toInt(); }
    QVariantMap colorsMap;
    for (auto it = colors.begin(); it != colors.end(); ++it) colorsMap[it.key()] = it.value();
    stats["colors"] = colorsMap;

    // 2.5 业务级类型统计 (2026-04-06 按照用户要求：多维嵌套映射聚合，支持子选项展示)
    QMap<QString, QMap<QString, int>> bizTypes;
    QSqlQuery typeQuery(m_db);
    // [PERFORMANCE] 仅查询必要的列，在 C++ 内存中执行复杂的分类逻辑，避免 SQLite 字符串操作开销
    typeQuery.prepare("SELECT item_type, title " + baseSql + whereClause);
    for (int i = 0; i < params.size(); ++i) typeQuery.bindValue(i, params[i]);
    if (typeQuery.exec()) {
        while (typeQuery.next()) {
            auto detailed = getDetailedBizType(typeQuery.value(0).toString(), typeQuery.value(1).toString());
            bizTypes[detailed.first][detailed.second]++;
        }
    }
    QVariantMap typesMap;
    for (auto itCat = bizTypes.begin(); itCat != bizTypes.end(); ++itCat) {
        QVariantMap extMap;
        for (auto itExt = itCat.value().begin(); itExt != itCat.value().end(); ++itExt) {
            extMap[itExt.key()] = itExt.value();
        }
        typesMap[itCat.key()] = extMap;
    }
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
