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
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    auto it = m_cache.find(path);
    if (it != m_cache.end() && it->second.isLoaded) return it->second;

    // 按需从磁盘加载，防止直接覆盖现有数据造成丢失
    QFileInfo info(QString::fromStdWString(path));
    AmMetaJson json(info.absolutePath().toStdWString());
    if (json.load()) {
        auto itemIt = json.items().find(info.fileName().toStdWString());
        if (itemIt != json.items().end()) {
            RuntimeMeta& meta = m_cache[path];
            meta.rating = itemIt->second.rating;
            meta.color = itemIt->second.color;
            meta.pinned = itemIt->second.pinned;
            meta.note = itemIt->second.note;
            meta.encrypted = itemIt->second.encrypted;
            meta.tags.clear();
            for (const auto& t : itemIt->second.tags) meta.tags << QString::fromStdWString(t);
            meta.isLoaded = true;
            return meta;
        }
    }

    RuntimeMeta& meta = m_cache[path];
    meta.isLoaded = true; // 即使磁盘无数据也标记已加载，避免重复读取
    return meta;
}

void MetadataManager::setRating(const std::wstring& path, int rating) {
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        if (!m_cache[path].isLoaded) {
            lock.unlock(); getMeta(path); lock.lock();
        }
        m_cache[path].rating = rating;
    }
    emit metaChanged(path);
    persistAsync(path);
}

void MetadataManager::setColor(const std::wstring& path, const std::wstring& color) {
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        if (!m_cache[path].isLoaded) {
            lock.unlock(); getMeta(path); lock.lock();
        }
        m_cache[path].color = color;
    }
    emit metaChanged(path);
    persistAsync(path);
}

void MetadataManager::setPinned(const std::wstring& path, bool pinned) {
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        if (!m_cache[path].isLoaded) {
            lock.unlock(); getMeta(path); lock.lock();
        }
        m_cache[path].pinned = pinned;
    }
    emit metaChanged(path);
    persistAsync(path);
}

void MetadataManager::setTags(const std::wstring& path, const QStringList& tags) {
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        if (!m_cache[path].isLoaded) {
            lock.unlock(); getMeta(path); lock.lock();
        }
        m_cache[path].tags = tags;
    }
    emit metaChanged(path);
    persistAsync(path);
}

void MetadataManager::setNote(const std::wstring& path, const std::wstring& note) {
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        if (!m_cache[path].isLoaded) {
            lock.unlock(); getMeta(path); lock.lock();
        }
        m_cache[path].note = note;
    }
    emit metaChanged(path);
    persistAsync(path);
}

void MetadataManager::persistAsync(const std::wstring& path) {
    QThreadPool::globalInstance()->start([this, path]() {
        ::ArcMeta::RuntimeMeta meta = getMeta(path);
        QFileInfo info(QString::fromStdWString(path));
        std::wstring parentDir = info.absolutePath().toStdWString();
        std::wstring fileName = info.fileName().toStdWString();

        AmMetaJson json(parentDir);
        json.load();
        auto& itemMeta = json.items()[fileName];

        // 关键逻辑：将内存中的最新属性同步到磁盘，确保不覆盖其他字段
        itemMeta.rating = meta.rating;
        itemMeta.color = meta.color;
        itemMeta.pinned = meta.pinned;
        itemMeta.note = meta.note;
        itemMeta.tags.clear();
        for (const auto& t : meta.tags) itemMeta.tags.push_back(t.toStdWString());

        json.save();

        ::ArcMeta::SyncQueue::instance().enqueue(parentDir);
    });
}

} // namespace ArcMeta
