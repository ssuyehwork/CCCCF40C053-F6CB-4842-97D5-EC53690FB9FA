#ifndef USNWATCHER_H
#define USNWATCHER_H

#include <windows.h>
#include <string>
#include <thread>
#include <atomic>
#include "MftReader.h"

namespace mft {

class UsnWatcher {
public:
    static UsnWatcher& instance();

    bool start(const std::wstring& volumePath);
    void stop();

private:
    UsnWatcher() = default;
    ~UsnWatcher() { stop(); }
    UsnWatcher(const UsnWatcher&) = delete;
    UsnWatcher& operator=(const UsnWatcher&) = delete;

    void watchThread(std::wstring volumePath);

    std::thread m_thread;
    std::atomic<bool> m_running{false};
};

} // namespace mft

#endif // USNWATCHER_H
