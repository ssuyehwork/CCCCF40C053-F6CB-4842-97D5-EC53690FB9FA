#include "HotkeyManager.h"
#include <QCoreApplication>
#include <QDebug>
#include <QSettings>
#include "../ui/StringUtils.h"

HotkeyManager& HotkeyManager::instance() {
    static HotkeyManager inst;
    return inst;
}

HotkeyManager::HotkeyManager(QObject* parent) : QObject(parent) {
    qApp->installNativeEventFilter(this);

    // [NEW] 注册焦点变化回调，实现热键动态开关。
    // 当检测到窗口切换时，立即重新评估是否需要注册 Ctrl+S 热键。
    StringUtils::setFocusCallback([this](bool isBrowser){
        qDebug() << "[HotkeyManager] 收到焦点切换通知，浏览器活跃状态:" << isBrowser;
        this->reapplyHotkeys();
    });
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
    else if (id == 4) keyDesc = "Ctrl+S (全局采集)";
    else if (id == 5) keyDesc = "Ctrl+Shift+L (全局锁定)";
    else if (id == 6) keyDesc = "Ctrl+Alt+Q (截图取文)";

    qWarning().noquote() << QString("[HotkeyManager] 注册热键失败: %1 (错误代码: %2). 该快捷键可能已被系统或其他软件占用。")
                            .arg(keyDesc).arg(GetLastError());
#endif
    return false;
}

void HotkeyManager::unregisterHotkey(int id) {
#ifdef Q_OS_WIN
    if (UnregisterHotKey(nullptr, id)) {
        qDebug() << "[HotkeyManager] 成功注销热键 ID:" << id;
    }
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

    // [REMOVED] 浏览器 Ctrl+S 全局采集热键已移除。
    // 现已整合为浏览器插件对接模式，Ctrl+S 由浏览器插件直接捕获并绕过剪贴板通过 PluginServer 通讯。
    unregisterHotkey(4);

    uint l_mods = hotkeys.value("lock_mods", 0x0002 | 0x0004).toUInt();     // Ctrl+Shift
    uint l_vk   = hotkeys.value("lock_vk", 0x4C).toUInt();                  // L
    registerHotkey(5, l_mods, l_vk);

    uint ocr_mods = hotkeys.value("ocr_mods", 0x0002 | 0x0001).toUInt();    // Ctrl+Alt
    uint ocr_vk   = hotkeys.value("ocr_vk", 0x51).toUInt();                 // Q
    registerHotkey(6, ocr_mods, ocr_vk);
    
    qDebug() << "[HotkeyManager] 所有系统热键已重新评估并应用。";
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
