#ifndef IPLATFORMSYSTEM_H
#define IPLATFORMSYSTEM_H

#include <QString>
#include <QImage>

class IPlatformSystem {
public:
    virtual ~IPlatformSystem() = default;

    // 进程与窗口
    virtual bool isBrowserActive() = 0;
    virtual QString getForegroundAppPath() = 0;

    // 输入模拟
    virtual void simulateCopy() = 0;
    virtual void simulateSelectAll() = 0;
    virtual void simulateKeyStroke(int vk, bool alt = false, bool ctrl = false, bool shift = false) = 0;

    // 系统热键与钩子底层支持 (可选，如果想彻底隔离 HotkeyManager)
    virtual bool registerGlobalHotkey(int id, uint modifiers, uint vk) = 0;
    virtual bool unregisterGlobalHotkey(int id) = 0;
};

#endif // IPLATFORMSYSTEM_H
