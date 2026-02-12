#include "QuickNoteViewModel.h"
#include "../core/ServiceLocator.h"

#include "../core/DatabaseManager.h"
#include <algorithm>

QuickNoteViewModel::QuickNoteViewModel(QObject* parent) : QObject(parent) {}

void QuickNoteViewModel::refreshData(const QString& keyword, const QString& filterType, const QVariant& filterValue, int page) {
    m_currentFilterType = filterType;
    m_currentFilterValue = filterValue;
    m_currentPage = page;

    auto db = ServiceLocator::get<DatabaseManager>();
    if (!db) return;

    int totalCount = db->getNotesCount(keyword, filterType, filterValue);
    const int pageSize = 100;
    m_totalPages = std::max(1, (totalCount + pageSize - 1) / pageSize);
    
    if (m_currentPage > m_totalPages) m_currentPage = m_totalPages;
    if (m_currentPage < 1) m_currentPage = 1;

    auto notes = db->searchNotes(keyword, filterType, filterValue, m_currentPage, pageSize);
    emit dataRefreshed(notes, totalCount, m_totalPages);
}

void QuickNoteViewModel::deleteNotes(const QList<int>& ids, bool physical) {
    if (ids.isEmpty()) return;
    auto db = ServiceLocator::get<DatabaseManager>();
    if (!db) return;

    if (physical || m_currentFilterType == "trash") {
        db->deleteNotesBatch(ids);
        emit statusMessageRequested(QString("✔ 已永久删除 %1 条数据").arg(ids.count()));
    } else {
        db->softDeleteNotes(ids);
        emit statusMessageRequested(QString("✔ 已移至回收站 %1 条数据").arg(ids.count()));
    }
    emit sidebarRefreshRequested();
}

void QuickNoteViewModel::toggleFavorite(const QList<int>& ids) {
    auto db = ServiceLocator::get<DatabaseManager>();
    if (!db) return;
    for (int id : ids) {
        db->toggleNoteState(id, "is_favorite");
    }
}

void QuickNoteViewModel::togglePin(const QList<int>& ids) {
    auto db = ServiceLocator::get<DatabaseManager>();
    if (!db) return;
    for (int id : ids) {
        db->toggleNoteState(id, "is_pinned");
    }
}

void QuickNoteViewModel::setRating(const QList<int>& ids, int rating) {
    auto db = ServiceLocator::get<DatabaseManager>();
    if (!db) return;
    for (int id : ids) {
        db->updateNoteState(id, "rating", rating);
    }
}

void QuickNoteViewModel::moveToCategory(const QList<int>& ids, int catId) {
    auto db = ServiceLocator::get<DatabaseManager>();
    if (!db) return;
    db->moveNotesToCategory(ids, catId);
}

void QuickNoteViewModel::addTags(const QList<int>& ids, const QStringList& tags) {
    auto db = ServiceLocator::get<DatabaseManager>();
    if (!db) return;
    for (int id : ids) {
        db->addTagsToNote(id, tags);
    }
}

void QuickNoteViewModel::updateTags(const QList<int>& ids, const QStringList& tags) {
    auto db = ServiceLocator::get<DatabaseManager>();
    if (!db) return;
    QString tagsStr = tags.join(", ");
    for (int id : ids) {
        db->updateNoteState(id, "tags", tagsStr);
    }
}
