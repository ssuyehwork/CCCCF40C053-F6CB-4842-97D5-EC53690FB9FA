#ifndef WIN32SYSTEM_H
#define WIN32SYSTEM_H

#include "IPlatformSystem.h"
#include <windows.h>
#include <psapi.h>
#include <QFileInfo>

class Win32System : public IPlatformSystem {
public:
    bool isBrowserActive() override {
        HWND hwnd = GetForegroundWindow();
        if (!hwnd) return false;

        DWORD pid;
        GetWindowThreadProcessId(hwnd, &pid);
        
        HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
        if (!process) return false;

        wchar_t buffer[MAX_PATH];
        if (GetModuleFileNameExW(process, NULL, buffer, MAX_PATH)) {
            QString exeName = QFileInfo(QString::fromWCharArray(buffer)).fileName().toLower();
            static const QStringList browserExes = {
                "chrome.exe", "msedge.exe", "firefox.exe", "brave.exe", 
                "opera.exe", "iexplore.exe", "vivaldi.exe", "safari.exe"
            };
            CloseHandle(process);
            return browserExes.contains(exeName);
        }

        CloseHandle(process);
        return false;
    }

    QString getForegroundAppPath() override {
        HWND hwnd = GetForegroundWindow();
        if (!hwnd) return "";
        DWORD pid;
        GetWindowThreadProcessId(hwnd, &pid);
        HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
        if (!process) return "";
        wchar_t buffer[MAX_PATH];
        QString path;
        if (GetModuleFileNameExW(process, NULL, buffer, MAX_PATH)) {
            path = QString::fromWCharArray(buffer);
        }
        CloseHandle(process);
        return path;
    }

    void simulateCopy() override {
        // 显式释放修饰键，防止干扰 Ctrl+C
        keybd_event(VK_SHIFT, 0, KEYEVENTF_KEYUP, 0);
        keybd_event(VK_MENU, 0, KEYEVENTF_KEYUP, 0); // Alt

        keybd_event(VK_CONTROL, 0, 0, 0);
        keybd_event('C', 0, 0, 0);
        keybd_event('C', 0, KEYEVENTF_KEYUP, 0);
        keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
    }

    void simulateKeyStroke(int vk, bool alt, bool ctrl, bool shift) override {
        if (ctrl) keybd_event(VK_CONTROL, 0, 0, 0);
        if (alt) keybd_event(VK_MENU, 0, 0, 0);
        if (shift) keybd_event(VK_SHIFT, 0, 0, 0);

        keybd_event(vk, 0, 0, 0);
        keybd_event(vk, 0, KEYEVENTF_KEYUP, 0);

        if (shift) keybd_event(VK_SHIFT, 0, KEYEVENTF_KEYUP, 0);
        if (alt) keybd_event(VK_MENU, 0, KEYEVENTF_KEYUP, 0);
        if (ctrl) keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
    }

    bool registerGlobalHotkey(int id, uint modifiers, uint vk) override {
        return RegisterHotKey(nullptr, id, modifiers, vk);
    }

    bool unregisterGlobalHotkey(int id) override {
        return UnregisterHotKey(nullptr, id);
    }
};

#endif // WIN32SYSTEM_H
