import sys

content = open('src/ui/StringUtils.h', 'r', encoding='utf-8').read()

search_text = """    static bool isBrowserActive() {
#ifdef Q_OS_WIN
        static const QRegularExpression browserPattern(R"((chrome|msedge|firefox|brave|opera|vivaldi|safari|arc|sidekick|maxthon|thorium|librewolf|waterfox)\.exe)");
        static bool hookInstalled = false;
        if (!hookInstalled) {
            // 监听前台窗口切换事件
            SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, NULL,
                           WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);
            hookInstalled = true;
            qDebug() << "[StringUtils] WinEventHook (Foreground) 已安装";
        }

        HWND hwnd = GetForegroundWindow();
        static HWND lastHwnd = nullptr;

        // 如果窗口句柄没变且缓存有效，直接返回结果
        if (m_browserCacheValid && hwnd == lastHwnd) {
            return m_isBrowserActiveCache;
        }

        lastHwnd = hwnd;
        m_isBrowserActiveCache = false;

        if (hwnd) {
            DWORD pid;
            GetWindowThreadProcessId(hwnd, &pid);

            // 尝试获取进程路径 (优先使用受限访问权限以提高成功率)
            HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid);
            if (!process) process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);

            if (process) {
                wchar_t buffer[MAX_PATH];
                if (GetModuleFileNameExW(process, NULL, buffer, MAX_PATH)) {
                    QString exePath = QString::fromWCharArray(buffer).toLower();
                    QString exeName = QFileInfo(exePath).fileName();

                    static QStringList browserExes;
                    static qint64 lastLoadTime = 0;
                    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();

                    // 浏览器进程列表配置缓存 (5秒刷新一次)
                    if (currentTime - lastLoadTime > 5000 || browserExes.isEmpty()) {
                        QSettings acquisitionSettings("RapidNotes", "Acquisition");
                        browserExes = acquisitionSettings.value("browserExes").toStringList();
                        if (browserExes.isEmpty()) {
                            browserExes = {
                                "chrome.exe", "msedge.exe", "firefox.exe", "brave.exe",
                                "opera.exe", "iexplore.exe", "vivaldi.exe", "safari.exe",
                                "arc.exe", "sidekick.exe", "maxthon.exe", "thorium.exe",
                                "librewolf.exe", "waterfox.exe"
                            };
                        }
                        lastLoadTime = currentTime;
                    }
                    m_isBrowserActiveCache = exeName.contains(browserPattern);
                    qDebug() << "[StringUtils] 活性检测 -> 进程:" << exeName << "是浏览器:" << m_isBrowserActiveCache;
                }
                CloseHandle(process);
            } else {
                qDebug() << "[StringUtils] 无法访问进程 (PID:" << pid << ")";
            }
        }

        m_browserCacheValid = true;
        return m_isBrowserActiveCache;
#else
        return false;
#endif
    }"""

replace_text = """    static bool isBrowserActive() {
#ifdef Q_OS_WIN
        static bool hookInstalled = false;
        if (!hookInstalled) {
            // 监听前台窗口切换事件
            SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, NULL,
                           WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);
            hookInstalled = true;
            qDebug() << "[StringUtils] WinEventHook (Foreground) 已安装";
        }

        HWND hwnd = GetForegroundWindow();
        static HWND lastHwnd = nullptr;

        // 如果窗口句柄没变且缓存有效，直接返回结果
        if (m_browserCacheValid && hwnd == lastHwnd) {
            return m_isBrowserActiveCache;
        }

        lastHwnd = hwnd;
        m_isBrowserActiveCache = false;

        if (hwnd) {
            DWORD pid;
            GetWindowThreadProcessId(hwnd, &pid);

            // 尝试获取进程路径 (优先使用受限访问权限以提高成功率)
            HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid);
            if (!process) process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);

            if (process) {
                wchar_t buffer[MAX_PATH];
                if (GetModuleFileNameExW(process, NULL, buffer, MAX_PATH)) {
                    QString exePath = QString::fromWCharArray(buffer).toLower();
                    QString exeName = QFileInfo(exePath).fileName();

                    static QStringList browserExes;
                    static qint64 lastLoadTime = 0;
                    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();

                    // 浏览器进程列表配置缓存 (5秒刷新一次)
                    if (currentTime - lastLoadTime > 5000 || browserExes.isEmpty()) {
                        QSettings acquisitionSettings("RapidNotes", "Acquisition");
                        browserExes = acquisitionSettings.value("browserExes").toStringList();
                        if (browserExes.isEmpty()) {
                            browserExes = {
                                "chrome.exe", "msedge.exe", "firefox.exe", "brave.exe",
                                "opera.exe", "iexplore.exe", "vivaldi.exe", "safari.exe",
                                "arc.exe", "sidekick.exe", "maxthon.exe", "thorium.exe",
                                "librewolf.exe", "waterfox.exe"
                            };
                        }
                        lastLoadTime = currentTime;
                    }
                    m_isBrowserActiveCache = browserExes.contains(exeName, Qt::CaseInsensitive);
                    qDebug() << "[StringUtils] 活性检测 -> 进程:" << exeName << "是浏览器:" << m_isBrowserActiveCache;
                }
                CloseHandle(process);
            } else {
                qDebug() << "[StringUtils] 无法访问进程 (PID:" << pid << ")";
            }
        }

        m_browserCacheValid = true;
        return m_isBrowserActiveCache;
#else
        return false;
#endif
    }"""

if search_text in content:
    new_content = content.replace(search_text, replace_text)
    open('src/ui/StringUtils.h', 'w', encoding='utf-8').write(new_content)
    print("Successfully replaced.")
else:
    print("Search text not found.")
