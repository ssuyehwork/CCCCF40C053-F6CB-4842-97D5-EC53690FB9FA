#include "SyncQueue.h"
#include "AmMetaJson.h"
#include "../core/DatabaseManager.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <vector>
#include <QString>
#include <QDateTime>
#include <QFileInfo>
#include <QThread>
#include <QCoreApplication>
#include <QStringList>

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
    flush();
}

void SyncQueue::enqueue(const std::wstring& folderPath) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pendingPaths.insert(folderPath);
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

        processBatch();
    }
}

/**
 * @brief 核心业务逻辑：从 JSON 同步数据到 SQLite 事务
 * 适配当前项目的 DatabaseManager，将外部文件元数据建立索引以便搜索
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

    // 获取 DatabaseManager 连接名（这里假设主线程连接可用，或者根据宪法使用异步事务）
    // 为了满足“超级资源管理器”战略，我们需要在数据库中记录这些外部文件
    for (const auto& path : batch) {
        AmMetaJson meta(path);
        if (!meta.load()) continue;

        QString folderPath = QString::fromStdWString(path);

        // 遍历所有有用户操作的条目，同步到数据库索引
        for (const auto& [name, iMeta] : meta.items()) {
            if (!iMeta.hasUserOperations()) continue;

            QString fileName = QString::fromStdWString(name);
            QString fullPath = folderPath;
            if (!fullPath.endsWith('\\') && !fullPath.endsWith('/')) fullPath += '/';
            fullPath += fileName;

            QStringList tags;
            for(const auto& t : iMeta.tags) {
                tags << QString::fromStdWString(t);
            }

            // [THREAD-SAFETY] 数据库写操作必须通过主线程 DatabaseManager 执行
            // 解决跨线程 QSqlDatabase 访问崩溃风险。
            // 使用 BlockingQueuedConnection 确保在 flush() 或程序退出时同步真正完成。
            auto upsertFunc = [=]() {
                DatabaseManager::instance().upsertExternalNote(
                    fileName,
                    fullPath,
                    tags,
                    QString::fromStdWString(iMeta.color),
                    iMeta.type == L"folder" ? "local_folder" : "local_file",
                    QString::fromStdWString(iMeta.note)
                );
            };

            if (QThread::currentThread() == qApp->thread()) {
                upsertFunc();
            } else {
                QMetaObject::invokeMethod(&DatabaseManager::instance(), upsertFunc,
                    m_running ? Qt::QueuedConnection : Qt::BlockingQueuedConnection);
            }
        }
    }

    return true;
}

} // namespace ArcMeta
