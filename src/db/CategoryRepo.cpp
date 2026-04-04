#include "CategoryRepo.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDateTime>

namespace ArcMeta {

/**
 * @brief 分类持久层实现
 * 2026-03-xx 物理修复：全面移除隐式 Default Connection，强制通过 getThreadDatabase 获取线程专用连接。
 */

bool CategoryRepo::add(Category& cat) {
    QSqlDatabase db = ArcMeta::Database::instance().getThreadDatabase();
    QSqlQuery q(db);
    q.prepare("INSERT INTO categories (parent_id, name, color, preset_tags, sort_order, pinned, created_at) VALUES (?, ?, ?, ?, ?, ?, ?)");
    q.addBindValue(cat.parentId);
    q.addBindValue(QString::fromStdWString(cat.name));
    q.addBindValue(QString::fromStdWString(cat.color));

    QJsonArray tagsArr;
    for (const auto& t : cat.presetTags) tagsArr.append(QString::fromStdWString(t));
    q.addBindValue(QJsonDocument(tagsArr).toJson(QJsonDocument::Compact));

    q.addBindValue(cat.sortOrder);
    q.addBindValue(cat.pinned ? 1 : 0);
    q.addBindValue((double)QDateTime::currentMSecsSinceEpoch());

    if (q.exec()) {
        cat.id = q.lastInsertId().toInt();
        return true;
    }
    return false;
}

bool CategoryRepo::reorderAll(bool ascending) {
    QSqlDatabase db = ArcMeta::Database::instance().getThreadDatabase();
    QSqlQuery q(db);
    q.prepare("SELECT id FROM categories ORDER BY name " + QString(ascending ? "ASC" : "DESC"));

    if (q.exec()) {
        int order = 0;
        db.transaction();
        while (q.next()) {
            int id = q.value(0).toInt();
            QSqlQuery upd(db);
            upd.prepare("UPDATE categories SET sort_order = ? WHERE id = ?");
            upd.addBindValue(order++);
            upd.addBindValue(id);
            upd.exec();
        }
        return db.commit();
    }
    return false;
}

bool CategoryRepo::update(const Category& cat) {
    QSqlDatabase db = ArcMeta::Database::instance().getThreadDatabase();
    QSqlQuery q(db);
    q.prepare("UPDATE categories SET parent_id = ?, name = ?, color = ?, sort_order = ?, pinned = ?, encrypted = ?, encrypt_hint = ? WHERE id = ?");
    q.addBindValue(cat.parentId);
    q.addBindValue(QString::fromStdWString(cat.name));
    q.addBindValue(QString::fromStdWString(cat.color));
    q.addBindValue(cat.sortOrder);
    q.addBindValue(cat.pinned ? 1 : 0);
    q.addBindValue(cat.encrypted ? 1 : 0);
    q.addBindValue(QString::fromStdWString(cat.encryptHint));
    q.addBindValue(cat.id);
    return q.exec();
}

bool CategoryRepo::addItemToCategory(int categoryId, const std::wstring& itemPath) {
    QSqlDatabase db = ArcMeta::Database::instance().getThreadDatabase();
    QSqlQuery q(db);
    q.prepare("INSERT OR IGNORE INTO category_items (category_id, item_path, added_at) VALUES (?, ?, ?)");
    q.addBindValue(categoryId);
    q.addBindValue(QString::fromStdWString(itemPath));
    q.addBindValue((double)QDateTime::currentMSecsSinceEpoch());
    return q.exec();
}

std::vector<Category> CategoryRepo::getAll() {
    std::vector<Category> results;
    QSqlDatabase db = ArcMeta::Database::instance().getThreadDatabase();
    // 物理还原：置顶优先，其次按 sort_order 排序
    QSqlQuery q("SELECT id, parent_id, name, color, preset_tags, sort_order, pinned, encrypted, encrypt_hint FROM categories ORDER BY pinned DESC, sort_order ASC", db);
    while (q.next()) {
        Category cat;
        cat.id = q.value(0).toInt();
        cat.parentId = q.value(1).toInt();
        cat.name = q.value(2).toString().toStdWString();
        cat.color = q.value(3).toString().toStdWString();

        QJsonDocument doc = QJsonDocument::fromJson(q.value(4).toByteArray());
        if (doc.isArray()) {
            for (const auto& v : doc.array()) cat.presetTags.push_back(v.toString().toStdWString());
        }

        cat.sortOrder = q.value(5).toInt();
        cat.pinned = q.value(6).toBool();
        cat.encrypted = q.value(7).toBool();
        cat.encryptHint = q.value(8).toString().toStdWString();
        results.push_back(cat);
    }
    return results;
}

bool CategoryRepo::removeItemFromCategory(int categoryId, const std::wstring& itemPath) {
    QSqlDatabase db = ArcMeta::Database::instance().getThreadDatabase();
    QSqlQuery q(db);
    q.prepare("DELETE FROM category_items WHERE category_id = ? AND item_path = ?");
    q.addBindValue(categoryId);
    q.addBindValue(QString::fromStdWString(itemPath));
    return q.exec();
}

std::vector<std::pair<int, int>> CategoryRepo::getCounts() {
    std::vector<std::pair<int, int>> counts;
    QSqlDatabase db = ArcMeta::Database::instance().getThreadDatabase();
    QSqlQuery q("SELECT category_id, COUNT(*) FROM category_items GROUP BY category_id", db);
    while (q.next()) {
        counts.push_back({q.value(0).toInt(), q.value(1).toInt()});
    }
    return counts;
}

int CategoryRepo::getUniqueItemCount() {
    QSqlDatabase db = ArcMeta::Database::instance().getThreadDatabase();
    QSqlQuery q("SELECT COUNT(DISTINCT item_path) FROM category_items", db);
    if (q.next()) return q.value(0).toInt();
    return 0;
}

int CategoryRepo::getUncategorizedItemCount() {
    QSqlDatabase db = ArcMeta::Database::instance().getThreadDatabase();
    // 2026-03-xx 物理修复：统计存在于 items 表且未删除、同时不在任何自定义分类中的项目数量
    // 逻辑：所有非删除项目 - 已关联分类的项目（去重）
    QSqlQuery q("SELECT (SELECT COUNT(*) FROM items WHERE deleted=0) - (SELECT COUNT(DISTINCT item_path) FROM category_items WHERE item_path IN (SELECT path FROM items WHERE deleted=0))", db);
    if (q.next()) return q.value(0).toInt();
    return 0;
}

QMap<QString, int> CategoryRepo::getSystemCounts() {
    QMap<QString, int> counts;
    QSqlDatabase db = ArcMeta::Database::instance().getThreadDatabase();
    double now = (double)QDateTime::currentMSecsSinceEpoch();
    double startOfToday = (double)QDateTime(QDate::currentDate(), QTime(0, 0)).toMSecsSinceEpoch();
    double startOfYesterday = (double)QDateTime(QDate::currentDate().addDays(-1), QTime(0, 0)).toMSecsSinceEpoch();

    // 全部数据
    QSqlQuery qAll("SELECT COUNT(*) FROM items WHERE deleted=0", db);
    if (qAll.next()) counts["all"] = qAll.value(0).toInt();

    // 今日
    QSqlQuery qToday(db);
    qToday.prepare("SELECT COUNT(*) FROM items WHERE deleted=0 AND ctime >= ?");
    qToday.addBindValue(startOfToday);
    if (qToday.exec() && qToday.next()) counts["today"] = qToday.value(0).toInt();

    // 昨日
    QSqlQuery qYesterday(db);
    qYesterday.prepare("SELECT COUNT(*) FROM items WHERE deleted=0 AND ctime >= ? AND ctime < ?");
    qYesterday.addBindValue(startOfYesterday);
    qYesterday.addBindValue(startOfToday);
    if (qYesterday.exec() && qYesterday.next()) counts["yesterday"] = qYesterday.value(0).toInt();

    // 最近访问 (24小时内)
    QSqlQuery qRecent(db);
    qRecent.prepare("SELECT COUNT(*) FROM items WHERE deleted=0 AND atime >= ?");
    qRecent.addBindValue(now - 86400000.0);
    if (qRecent.exec() && qRecent.next()) counts["recently_visited"] = qRecent.value(0).toInt();

    // 未分类
    counts["uncategorized"] = getUncategorizedItemCount();

    // 未标签
    QSqlQuery qUntagged("SELECT COUNT(*) FROM items WHERE deleted=0 AND (tags IS NULL OR tags = '' OR tags = '[]')", db);
    if (qUntagged.next()) counts["untagged"] = qUntagged.value(0).toInt();

    // 收藏 (假设 pinned=1 的 item 即为收藏)
    QSqlQuery qFav("SELECT COUNT(*) FROM items WHERE pinned=1 AND deleted=0", db);
    if (qFav.next()) counts["bookmark"] = qFav.value(0).toInt();

    // 回收站
    QSqlQuery qTrash("SELECT COUNT(*) FROM items WHERE deleted=1", db);
    if (qTrash.next()) counts["trash"] = qTrash.value(0).toInt();

    return counts;
}

bool CategoryRepo::remove(int id) {
    QSqlDatabase db = ArcMeta::Database::instance().getThreadDatabase();
    QSqlQuery q1(db);
    q1.prepare("DELETE FROM category_items WHERE category_id = ?");
    q1.addBindValue(id);
    q1.exec();

    QSqlQuery q2(db);
    q2.prepare("DELETE FROM categories WHERE id = ?");
    q2.addBindValue(id);
    return q2.exec();
}

bool CategoryRepo::updateOrders(int parentId, const QList<int>& ids) {
    QSqlDatabase db = ArcMeta::Database::instance().getThreadDatabase();
    db.transaction();
    for (int i = 0; i < ids.size(); ++i) {
        QSqlQuery q(db);
        q.prepare("UPDATE categories SET parent_id = ?, sort_order = ? WHERE id = ?");
        q.addBindValue(parentId);
        q.addBindValue(i);
        q.addBindValue(ids[i]);
        if (!q.exec()) {
            db.rollback();
            return false;
        }
    }
    return db.commit();
}

bool CategoryRepo::reorder(int parentId, bool ascending) {
    QSqlDatabase db = ArcMeta::Database::instance().getThreadDatabase();
    // 逻辑：获取该父级下的所有分类，按名称排序，然后重新赋予 sort_order
    QSqlQuery q(db);
    q.prepare("SELECT id FROM categories WHERE parent_id = ? ORDER BY name " + QString(ascending ? "ASC" : "DESC"));
    q.addBindValue(parentId);

    if (q.exec()) {
        int order = 0;
        db.transaction();
        while (q.next()) {
            int id = q.value(0).toInt();
            QSqlQuery upd(db);
            upd.prepare("UPDATE categories SET sort_order = ? WHERE id = ?");
            upd.addBindValue(order++);
            upd.addBindValue(id);
            upd.exec();
        }
        return db.commit();
    }
    return false;
}

std::vector<std::wstring> CategoryRepo::getItemPathsInCategory(int categoryId) {
    std::vector<std::wstring> results;
    QSqlDatabase db = ArcMeta::Database::instance().getThreadDatabase();
    QSqlQuery q(db);
    q.prepare("SELECT item_path FROM category_items WHERE category_id = ? ORDER BY added_at DESC");
    q.addBindValue(categoryId);
    if (q.exec()) {
        while (q.next()) {
            results.push_back(q.value(0).toString().toStdWString());
        }
    }
    return results;
}

} // namespace ArcMeta
