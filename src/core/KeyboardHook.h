#ifndef KEYBOARDHOOK_H
#define KEYBOARDHOOK_H

#include <QObject>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

class KeyboardHook : public QObject {
    Q_OBJECT
public:
    explicit KeyboardHook(QObject* parent = nullptr);
    ~KeyboardHook();
    void start();
    void stop();
    bool isActive() const { return m_active; }

    void setDigitInterceptEnabled(bool enabled) { m_digitInterceptEnabled = enabled; }
    bool isDigitInterceptEnabled() const { return m_digitInterceptEnabled; }

signals:
    void digitPressed(int digit);
    void f4PressedInExplorer();

private:
    bool m_digitInterceptEnabled = false;
    bool m_active = false;

#ifdef Q_OS_WIN
    static LRESULT CALLBACK HookProc(int nCode, WPARAM wParam, LPARAM lParam);
#endif
};

#endif // KEYBOARDHOOK_H
