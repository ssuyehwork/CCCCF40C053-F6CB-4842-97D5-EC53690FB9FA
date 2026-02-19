#ifndef KEYBOARDHOOK_H
#define KEYBOARDHOOK_H

#include <QObject>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

class KeyboardHook : public QObject {
    Q_OBJECT
public:
    static KeyboardHook& instance();
    void start();
    void stop();
    bool isActive() const { return m_active; }

    void setDigitInterceptEnabled(bool enabled) { m_digitInterceptEnabled = enabled; }
    void setEnterCaptureEnabled(bool enabled) { m_enterCaptureEnabled = enabled; }

    // 设置采集快捷键 (mods 是 MOD_CONTROL 等，vk 是虚拟键码)
    void setAcquireHotkey(uint mods, uint vk) { m_acquireMods = mods; m_acquireVk = vk; }

signals:
    void digitPressed(int digit);
    void f4PressedInExplorer();
    void enterPressedInOtherApp(bool ctrl, bool shift, bool alt);
    void acquireTriggered(); // 只有在浏览器激活且按下采集热键时触发

private:
    bool m_digitInterceptEnabled = false;
    bool m_enterCaptureEnabled = false;
    uint m_acquireMods = 0;
    uint m_acquireVk = 0;
    KeyboardHook();
    ~KeyboardHook();
    bool m_active = false;

#ifdef Q_OS_WIN
    static LRESULT CALLBACK HookProc(int nCode, WPARAM wParam, LPARAM lParam);
#endif
};

#endif // KEYBOARDHOOK_H
