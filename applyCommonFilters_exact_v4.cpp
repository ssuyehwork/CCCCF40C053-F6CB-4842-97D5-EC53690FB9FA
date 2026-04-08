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
