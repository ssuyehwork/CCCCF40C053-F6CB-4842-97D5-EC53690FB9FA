#include "MetadataManager.h"
#include "AmMetaJson.h"
#include "SyncQueue.h"
#include <QFileInfo>
#include <QThreadPool>

namespace ArcMeta {

MetadataManager& MetadataManager::instance() {
    static MetadataManager inst;
    return inst;
}

MetadataManager::MetadataManager(QObject* parent) : QObject(parent) {}

RuntimeMeta MetadataManager::getMeta(const std::wstring& path) {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    auto it = m_cache.find(path);
    if (it != m_cache.end()) return it->second;
    return RuntimeMeta();
}

void MetadataManager::setRating(const std::wstring& path, int rating) {
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_cache[path].rating = rating;
    }
    emit metaChanged(path);
    persistAsync(path);
}

void MetadataManager::setColor(const std::wstring& path, const std::wstring& color) {
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_cache[path].color = color;
    }
    emit metaChanged(path);
    persistAsync(path);
}

void MetadataManager::setPinned(const std::wstring& path, bool pinned) {
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_cache[path].pinned = pinned;
    }
    emit metaChanged(path);
    persistAsync(path);
}

void MetadataManager::setTags(const std::wstring& path, const QStringList& tags) {
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_cache[path].tags = tags;
    }
    emit metaChanged(path);
    persistAsync(path);
}

void MetadataManager::setNote(const std::wstring& path, const std::wstring& note) {
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        // RuntimeMeta 中不存大段备注以省内存，直接持久化
    }
    emit metaChanged(path);
    persistAsync(path, note);
}

void MetadataManager::persistAsync(const std::wstring& path, const std::wstring& noteOverride) {
    QThreadPool::globalInstance()->start([this, path, noteOverride]() {
        RuntimeMeta meta = getMeta(path);
        QFileInfo info(QString::fromStdWString(path));
        std::wstring parentDir = info.absolutePath().toStdWString();
        std::wstring fileName = info.fileName().toStdWString();

        AmMetaJson json(parentDir);
        json.load();
        auto& itemMeta = json.items()[fileName];
        itemMeta.rating = meta.rating;
        itemMeta.color = meta.color;
        itemMeta.pinned = meta.pinned;
        if (!noteOverride.empty()) itemMeta.note = noteOverride;
        itemMeta.tags.clear();
        for (const auto& t : meta.tags) itemMeta.tags.push_back(t.toStdWString());
        json.save();

        SyncQueue::instance().enqueue(parentDir);
    });
}

} // namespace ArcMeta
