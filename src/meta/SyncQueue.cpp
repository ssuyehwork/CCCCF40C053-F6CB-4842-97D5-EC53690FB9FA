#include "SyncQueue.h"
#include "AmMetaJson.h"
#include "../db/Database.h"
#include "../db/FolderRepo.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include "../db/ItemRepo.h"
#include <vector>
#include <QString>
#include <QDateTime>

namespace ArcMeta {

SyncQueue& SyncQueue::instance() {
    static SyncQueue inst;
    return inst;
}

SyncQueue::SyncQueue() {}

SyncQueue::~SyncQueue() {
    stop();
}

void SyncQueue::start() {
    if (m_running) return;
    m_running = true;
    m_thread = std::thread(&SyncQueue::workerThread, this);
}

void SyncQueue::stop() {
    if (!m_running) return;
    m_running = false;
    m_cv.notify_all();
    if (m_thread.joinable()) {
        m_thread.join();
    }
    // 程序退出前执行最后的强制同步
    flush();
}

void SyncQueue::enqueue(const std::wstring& folderPath) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pendingPaths.insert(folderPath); // set 自动去重
    }
    m_cv.notify_one();
}

void SyncQueue::flush() {
    while (true) {
        if (!processBatch()) break;
    }
}

void SyncQueue::workerThread() {
    while (m_running) {
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait_for(lock, std::chrono::milliseconds(2000), [this] {
                return !m_running || !m_pendingPaths.empty();
            });
        }
        
        if (!m_running && m_pendingPaths.empty()) break;
        
        // 批量处理当前队列中的路径
        processBatch();
    }
}

/**
 * @brief 核心业务逻辑：从 JSON 同步数据到 SQLite 事务
 */
bool SyncQueue::processBatch() {
    std::vector<std::wstring> batch;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_pendingPaths.empty()) return false;
        batch.assign(m_pendingPaths.begin(), m_pendingPaths.end());
        m_pendingPaths.clear();
    }

    if (batch.empty()) return false;

    // 2026-03-xx 统一使用 getThreadDatabase 机制，修复后台同步线程的数据库连接警告。
    QSqlDatabase db = ArcMeta::Database::instance().getThreadDatabase();

    if (!db.isOpen()) return false;

    try {
        db.transaction();

        for (const auto& path : batch) {
            AmMetaJson meta(path);
            if (!meta.load()) continue;

            // 1. 使用 Repository 同步文件夹
            FolderRepo::save(path, meta.folder());

            // 2. 使用 Repository 同步所有条目
            for (const auto& [name, iMeta] : meta.items()) {
                ItemRepo::save(path, name, iMeta);
            }
        }

        if (db.commit()) {
            return true;
        } else {
            db.rollback();
            return false;
        }
    } catch (...) {
        db.rollback();
        return false;
    }
}

} // namespace ArcMeta
