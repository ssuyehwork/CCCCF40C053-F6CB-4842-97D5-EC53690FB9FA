#include "MainNoteViewModel.h"
#include "../core/ServiceLocator.h"
#include "../core/DatabaseManager.h"
#include <algorithm>

MainNoteViewModel::MainNoteViewModel(QObject* parent) : QObject(parent) {}

void MainNoteViewModel::refreshData(const QString& keyword, const QString& filterType, const QVariant& filterValue, int page, int pageSize, const QVariantMap& criteria) {
    m_currentFilterType = filterType;
    auto db = ServiceLocator::get<DatabaseManager>();
    if (!db) return;

    auto notes = db->searchNotes(keyword, filterType, filterValue, page, pageSize, criteria);
    int totalCount = db->getNotesCount(keyword, filterType, filterValue, criteria);
    int totalPages = (totalCount + pageSize - 1) / pageSize;
    if (totalPages < 1) totalPages = 1;

    emit dataRefreshed(notes, totalCount, totalPages);
}

void MainNoteViewModel::deleteNotes(const QList<int>& ids, bool physical) {
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
}

void MainNoteViewModel::toggleFavorite(const QList<int>& ids) {
    auto db = ServiceLocator::get<DatabaseManager>();
    if (!db) return;
    for (int id : ids) {
        db->toggleNoteState(id, "is_favorite");
    }
}

void MainNoteViewModel::togglePin(const QList<int>& ids) {
    auto db = ServiceLocator::get<DatabaseManager>();
    if (!db) return;
    for (int id : ids) {
        db->toggleNoteState(id, "is_pinned");
    }
}

void MainNoteViewModel::setRating(const QList<int>& ids, int rating) {
    auto db = ServiceLocator::get<DatabaseManager>();
    if (!db) return;
    for (int id : ids) {
        db->updateNoteState(id, "rating", rating);
    }
}

void MainNoteViewModel::moveToCategory(const QList<int>& ids, int catId) {
    auto db = ServiceLocator::get<DatabaseManager>();
    if (!db) return;
    db->moveNotesToCategory(ids, catId);
}
