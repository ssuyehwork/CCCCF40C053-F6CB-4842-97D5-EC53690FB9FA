#include "FolderRepo.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include <QJsonArray>

namespace ArcMeta {

bool FolderRepo::save(const std::wstring& path, const FolderMeta& meta) {
    // 2026-03-xx 修复：通过 getThreadDatabase 获取当前线程专属连接，确保在后台同步任务中正确持久化。
    QSqlDatabase db = ArcMeta::Database::instance().getThreadDatabase();
    QSqlQuery q(db);
    q.prepare("INSERT OR REPLACE INTO folders (path, rating, color, tags, pinned, note, sort_by, sort_order) VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
    q.addBindValue(QString::fromStdWString(path));
    q.addBindValue(meta.rating);
    q.addBindValue(QString::fromStdWString(meta.color));
    
    QJsonArray tagsArr;
    for (const auto& t : meta.tags) tagsArr.append(QString::fromStdWString(t));
    q.addBindValue(QJsonDocument(tagsArr).toJson(QJsonDocument::Compact));
    
    q.addBindValue(meta.pinned ? 1 : 0);
    q.addBindValue(QString::fromStdWString(meta.note));
    q.addBindValue(QString::fromStdWString(meta.sortBy));
    q.addBindValue(QString::fromStdWString(meta.sortOrder));
    
    return q.exec();
}

bool FolderRepo::get(const std::wstring& path, FolderMeta& meta) {
    QSqlDatabase db = ArcMeta::Database::instance().getThreadDatabase();
    QSqlQuery q(db);
    q.prepare("SELECT rating, color, tags, pinned, note, sort_by, sort_order FROM folders WHERE path = ?");
    q.addBindValue(QString::fromStdWString(path));
    
    if (q.exec() && q.next()) {
        meta.rating = q.value(0).toInt();
        meta.color = q.value(1).toString().toStdWString();
        
        QJsonDocument doc = QJsonDocument::fromJson(q.value(2).toByteArray());
        meta.tags.clear();
        if (doc.isArray()) {
            for (const auto& v : doc.array()) meta.tags.push_back(v.toString().toStdWString());
        }

        meta.pinned = q.value(3).toInt() != 0;
        meta.note = q.value(4).toString().toStdWString();
        meta.sortBy = q.value(5).toString().toStdWString();
        meta.sortOrder = q.value(6).toString().toStdWString();
        return true;
    }
    return false;
}

bool FolderRepo::remove(const std::wstring& path) {
    QSqlDatabase db = ArcMeta::Database::instance().getThreadDatabase();
    QSqlQuery q(db);
    q.prepare("DELETE FROM folders WHERE path = ?");
    q.addBindValue(QString::fromStdWString(path));
    return q.exec();
}

} // namespace ArcMeta
