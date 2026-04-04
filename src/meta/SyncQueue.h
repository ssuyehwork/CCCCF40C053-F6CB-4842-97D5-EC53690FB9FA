#pragma once

#include <string>
#include <set>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>

namespace ArcMeta {

/**
 * @brief 懒更新队列
 * 实现 .am_meta.json 变更后的异步防抖同步到数据库逻辑
 */
class SyncQueue {
public:
    static SyncQueue& instance();

    /**
     * @brief 启动后台同步线程
     */
    void start();

    /**
     * @brief 停止后台同步线程并确保队列刷空 (Flush)
     */
    void stop();

    /**
     * @brief 将发生变更的文件夹路径加入队列
     * @param folderPath 文件夹完整路径
     */
    void enqueue(const std::wstring& folderPath);

    /**
     * @brief 强制同步当前队列中的所有路径（阻塞直至完成）
     */
    void flush();

private:
    SyncQueue();
    ~SyncQueue();
    SyncQueue(const SyncQueue&) = delete;
    SyncQueue& operator=(const SyncQueue&) = delete;

    void workerThread();
    bool processBatch();

    std::set<std::wstring> m_pendingPaths; // 使用 set 自动去重合并 (防抖)
    std::mutex m_mutex;
    std::condition_variable m_cv;

    std::thread m_thread;
    std::atomic<bool> m_running{false};
};

} // namespace ArcMeta
