#ifndef WIN32SYSTEM_H
#define WIN32SYSTEM_H

#include "IPlatformSystem.h"
#include "KeyboardHook.h"
#include <windows.h>
#include <psapi.h>
#include <QFileInfo>
#include <vector>

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
        std::vector<INPUT> inputs;
        // 释放可能按下的修饰键
        addKeyInput(inputs, VK_SHIFT, true);
        addKeyInput(inputs, VK_MENU, true);

        // Ctrl + C
        addKeyInput(inputs, VK_CONTROL, false);
        addKeyInput(inputs, 'C', false);
        addKeyInput(inputs, 'C', true);
        addKeyInput(inputs, VK_CONTROL, true);

        SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
    }

    void simulateSelectAll() override {
        std::vector<INPUT> inputs;
        addKeyInput(inputs, VK_SHIFT, true);
        addKeyInput(inputs, VK_MENU, true);

        // Ctrl + A
        addKeyInput(inputs, VK_CONTROL, false);
        addKeyInput(inputs, 'A', false);
        addKeyInput(inputs, 'A', true);
        addKeyInput(inputs, VK_CONTROL, true);

        SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
    }

    void simulateKeyStroke(int vk, bool alt = false, bool ctrl = false, bool shift = false) override {
        std::vector<INPUT> inputs;
        if (ctrl) addKeyInput(inputs, VK_CONTROL, false);
        if (alt) addKeyInput(inputs, VK_MENU, false);
        if (shift) addKeyInput(inputs, VK_SHIFT, false);

        addKeyInput(inputs, vk, false);
        addKeyInput(inputs, vk, true);

        if (shift) addKeyInput(inputs, VK_SHIFT, true);
        if (alt) addKeyInput(inputs, VK_MENU, true);
        if (ctrl) addKeyInput(inputs, VK_CONTROL, true);

        SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
    }

private:
    void addKeyInput(std::vector<INPUT>& inputs, int vk, bool release) {
        INPUT input = {0};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = static_cast<WORD>(vk);
        input.ki.dwFlags = release ? KEYEVENTF_KEYUP : 0;
        input.ki.dwExtraInfo = RAPID_NOTES_KEY_SIGNATURE;
        inputs.push_back(input);
    }

public:

    bool registerGlobalHotkey(int id, uint modifiers, uint vk) override {
        return RegisterHotKey(nullptr, id, modifiers, vk);
    }

    bool unregisterGlobalHotkey(int id) override {
        return UnregisterHotKey(nullptr, id);
    }
};

#endif // WIN32SYSTEM_H
