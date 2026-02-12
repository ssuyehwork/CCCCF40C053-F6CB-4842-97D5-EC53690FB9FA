#include "HotkeyManager.h"
#include <QCoreApplication>
#include <QDebug>
#include <QSettings>

HotkeyManager& HotkeyManager::instance() {
    static HotkeyManager inst;
    return inst;
}

HotkeyManager::HotkeyManager(QObject* parent) : QObject(parent) {
    qApp->installNativeEventFilter(this);
}

HotkeyManager::~HotkeyManager() {
    // 退出时取消所有注册
}

bool HotkeyManager::registerHotkey(int id, uint modifiers, uint vk) {
#ifdef Q_OS_WIN
    if (RegisterHotKey(nullptr, id, modifiers, vk)) {
        return true;
    }
    
    QString keyDesc = QString("ID=%1").arg(id);
    if (id == 1) keyDesc = "Alt+Space (快速窗口)";
    else if (id == 2) keyDesc = "Ctrl+Shift+E (全局收藏)";
    else if (id == 3) keyDesc = "Ctrl+Alt+A (全局截屏)";
    else if (id == 4) keyDesc = "Ctrl+Shift+S (全局采集)";
    else if (id == 5) keyDesc = "Ctrl+Shift+L (全局锁定)";
    else if (id == 6) keyDesc = "Ctrl+Alt+Q (文字识别)";

    qWarning().noquote() << QString("[HotkeyManager] 注册热键失败: %1 (错误代码: %2). 该快捷键可能已被系统或其他软件占用。")
                            .arg(keyDesc).arg(GetLastError());
#endif
    return false;
}

void HotkeyManager::unregisterHotkey(int id) {
#ifdef Q_OS_WIN
    UnregisterHotKey(nullptr, id);
#endif
}

void HotkeyManager::reapplyHotkeys() {
    QSettings hotkeys("RapidNotes", "Hotkeys");
    
    // 注销旧热键
    unregisterHotkey(1);
    unregisterHotkey(2);
    unregisterHotkey(3);
    unregisterHotkey(4);
    unregisterHotkey(5);
    unregisterHotkey(6);
    
    // 注册新热键（带默认值）
    uint q_mods = hotkeys.value("quickWin_mods", 0x0001).toUInt();  // Alt
    uint q_vk   = hotkeys.value("quickWin_vk", 0x20).toUInt();     // Space
    registerHotkey(1, q_mods, q_vk);
    
    uint f_mods = hotkeys.value("favorite_mods", 0x0002 | 0x0004).toUInt(); // Ctrl+Shift
    uint f_vk   = hotkeys.value("favorite_vk", 0x45).toUInt();              // E
    registerHotkey(2, f_mods, f_vk);
    
    uint s_mods = hotkeys.value("screenshot_mods", 0x0002 | 0x0001).toUInt(); // Ctrl+Alt
    uint s_vk   = hotkeys.value("screenshot_vk", 0x41).toUInt();               // A
    registerHotkey(3, s_mods, s_vk);

    uint a_mods = hotkeys.value("acquire_mods", 0x0002 | 0x0004).toUInt();  // Ctrl+Shift
    uint a_vk   = hotkeys.value("acquire_vk", 0x53).toUInt();               // S
    registerHotkey(4, a_mods, a_vk);

    uint l_mods = hotkeys.value("lock_mods", 0x0002 | 0x0004).toUInt();     // Ctrl+Shift
    uint l_vk   = hotkeys.value("lock_vk", 0x4C).toUInt();                  // L
    registerHotkey(5, l_mods, l_vk);

    uint ocr_mods = hotkeys.value("ocr_mods", 0x0002 | 0x0001).toUInt();    // Ctrl+Alt
    uint ocr_vk   = hotkeys.value("ocr_vk", 0x51).toUInt();                 // Q
    registerHotkey(6, ocr_mods, ocr_vk);
    
    qDebug() << "[HotkeyManager] 热键配置已更新。";
}

bool HotkeyManager::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) {
#ifdef Q_OS_WIN
    if (eventType == "windows_generic_MSG") {
        MSG* msg = static_cast<MSG*>(message);
        if (msg->message == WM_HOTKEY) {
            emit hotkeyPressed(static_cast<int>(msg->wParam));
            return true;
        }
    }
#endif
    return false;
}