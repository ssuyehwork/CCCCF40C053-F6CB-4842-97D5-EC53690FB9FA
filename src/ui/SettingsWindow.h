#ifndef SETTINGSWINDOW_H
#define SETTINGSWINDOW_H

#include "FramelessDialog.h"
#include <QSettings>
#include <QKeySequence>

#include <QLineEdit>
#include <QKeyEvent>
#include <QEvent>

// --- HotkeyEdit 辅助类 ---
class HotkeyEdit : public QLineEdit {
    Q_OBJECT
public:
    HotkeyEdit(QWidget* parent = nullptr);

    void setHotkey(uint mods, uint vk, const QString& display);
    uint getMods() const { return m_mods; }
    uint getVk() const { return m_vk; }

protected:
    bool event(QEvent* e) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    uint m_mods = 0;
    uint m_vk = 0;
};

class SettingsWindow : public FramelessDialog {
    Q_OBJECT
public:
    explicit SettingsWindow(QWidget* parent = nullptr);

private slots:
    void handleSetPassword();
    void handleModifyPassword();
    void handleRemovePassword();
    void saveSettings();
    void handleRestoreDefaults();
    void browseScreenshotPath();

private:
    void initSettingsUI();
    
    // UI elements for Hotkeys
    HotkeyEdit* m_hkQuickWin;
    HotkeyEdit* m_hkFavorite;
    HotkeyEdit* m_hkScreenshot;
    HotkeyEdit* m_hkOCR;

    // UI elements for Screenshot
    QLineEdit* m_screenshotPathEdit;
};

#endif // SETTINGSWINDOW_H
