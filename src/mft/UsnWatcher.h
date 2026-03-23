#ifndef USNWATCHER_H
#define USNWATCHER_H

#include <windows.h>
#include <string>
#include <functional>
#include <atomic>
#include <thread>
#include <QObject>

class UsnWatcher : public QObject {
    Q_OBJECT
public:
    static UsnWatcher& instance();
    
    bool start(const std::wstring& drive);
    void stop();

signals:
    void fileChanged(const std::wstring& path);
    void fileDeleted(const std::wstring& path);

private:
    UsnWatcher();
    ~UsnWatcher();
    
    void watchLoop();
    
    std::wstring m_drive;
    HANDLE m_hVolume = INVALID_HANDLE_VALUE;
    USN m_nextUsn = 0;
    std::atomic<bool> m_running{false};
    std::thread m_thread;
};

#endif // USNWATCHER_H
