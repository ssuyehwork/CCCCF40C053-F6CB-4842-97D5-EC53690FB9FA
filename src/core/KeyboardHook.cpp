#include "KeyboardHook.h"
#include <QDebug>
#include "../ui/StringUtils.h"

#ifdef Q_OS_WIN
HHOOK g_hHook = nullptr;
#endif

KeyboardHook& KeyboardHook::instance() {
    static KeyboardHook inst;
    return inst;
}

KeyboardHook::KeyboardHook() {}

KeyboardHook::~KeyboardHook() {
    stop();
}

void KeyboardHook::start() {
#ifdef Q_OS_WIN
    if (g_hHook) return;
    g_hHook = SetWindowsHookEx(WH_KEYBOARD_LL, HookProc, GetModuleHandle(NULL), 0);
    if (g_hHook) {
        m_active = true;
        qDebug() << "Keyboard hook started";
    }
#endif
}

void KeyboardHook::stop() {
#ifdef Q_OS_WIN
    if (g_hHook) {
        UnhookWindowsHookEx(g_hHook);
        g_hHook = nullptr;
        m_active = false;
        qDebug() << "Keyboard hook stopped";
    }
#endif
}

#ifdef Q_OS_WIN
LRESULT CALLBACK KeyboardHook::HookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        KBDLLHOOKSTRUCT* pKey = (KBDLLHOOKSTRUCT*)lParam;
        
        // 忽略所有模拟按键，防止无限循环
        if (pKey->flags & LLKHF_INJECTED) {
            return CallNextHookEx(g_hHook, nCode, wParam, lParam);
        }

        bool isKeyDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);

        // 监听回车键/Ctrl+回车键 (仅限非本应用窗口，且必须显式使能)
        if (KeyboardHook::instance().m_enterCaptureEnabled && isKeyDown && pKey->vkCode == VK_RETURN) {
            HWND foreground = GetForegroundWindow();
            DWORD pid;
            GetWindowThreadProcessId(foreground, &pid);
            if (pid != GetCurrentProcessId()) {
                bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000);
                bool shift = (GetKeyState(VK_SHIFT) & 0x8000);
                bool alt = (GetKeyState(VK_MENU) & 0x8000);
                emit KeyboardHook::instance().enterPressedInOtherApp(ctrl, shift, alt);
                return 1; // 拦截回车，交给处理器稍后重新模拟
            }
        }

        // 采集快捷键检测 (仅限浏览器，且支持非浏览器穿透)
        if (KeyboardHook::instance().m_acquireVk != 0) {
            if (pKey->vkCode == KeyboardHook::instance().m_acquireVk) {
                bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000);
                bool shift = (GetKeyState(VK_SHIFT) & 0x8000);
                bool alt = (GetKeyState(VK_MENU) & 0x8000);

                uint currentMods = 0;
                if (ctrl) currentMods |= MOD_CONTROL;
                if (alt) currentMods |= MOD_ALT;
                if (shift) currentMods |= MOD_SHIFT;

                if (currentMods == KeyboardHook::instance().m_acquireMods) {
                    if (StringUtils::isBrowserActive()) {
                        if (isKeyDown) {
                            emit KeyboardHook::instance().acquireTriggered();
                        }
                        return 1; // 浏览器环境下，拦截该键，由 RapidNotes 接管
                    }
                    // 非浏览器环境下，不返回 1，继续执行 CallNextHookEx，实现“穿透”
                }
            }
        }

        // 工具箱数字拦截 (仅在使能时触发)
        if (KeyboardHook::instance().m_digitInterceptEnabled) {
            if (pKey->vkCode >= 0x30 && pKey->vkCode <= 0x39) {
                if (isKeyDown) {
                    int digit = pKey->vkCode - 0x30;
                    qDebug() << "Digit pressed:" << digit;
                    emit KeyboardHook::instance().digitPressed(digit);
                }
                // 按下和弹起都拦截
                return 1;
            }
        }
    }
    return CallNextHookEx(g_hHook, nCode, wParam, lParam);
}
#endif
