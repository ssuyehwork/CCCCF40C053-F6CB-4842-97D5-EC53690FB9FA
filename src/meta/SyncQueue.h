#ifndef SYNCQUEUE_H
#define SYNCQUEUE_H

#include <string>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <unordered_set>
#include <QObject>

class SyncQueue : public QObject {
    Q_OBJECT
public:
    static SyncQueue& instance();
    
    void enqueue(const std::wstring& folderPath);
    void stop();

private:
    SyncQueue();
    ~SyncQueue();
    
    void processLoop();
    
    std::vector<std::wstring> m_queue;
    std::unordered_set<std::wstring> m_pendingPaths;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::thread m_thread;
    std::atomic<bool> m_running{false};
};

#endif // SYNCQUEUE_H
