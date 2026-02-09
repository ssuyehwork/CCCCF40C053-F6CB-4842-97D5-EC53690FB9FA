#include "SettingsWindow.h"
#include "StringUtils.h"

#include "IconHelper.h"
#include "CategoryPasswordDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QPushButton>
#include <QGroupBox>
#include <QLineEdit>
#include <QToolTip>
#include <QFileDialog>
#include <QDir>
#include <QCoreApplication>
#include <QLabel>

#include <QKeyEvent>
#include "../core/HotkeyManager.h"

// --- HotkeyEdit 辅助类 ---
HotkeyEdit::HotkeyEdit(QWidget* parent) : QLineEdit(parent) {
    setReadOnly(true);
    setAlignment(Qt::AlignCenter);
    setPlaceholderText("按下快捷键组合...");
    setStyleSheet("background-color: #1e1e1e; border: 1px solid #333; color: #3b8ed0; font-weight: bold; padding: 4px; border-radius: 4px;");
}

void HotkeyEdit::setHotkey(uint mods, uint vk, const QString& display) {
    m_mods = mods;
    m_vk = vk;
    setText(display);
}

bool HotkeyEdit::event(QEvent* e) {
    // 允许捕获系统级快捷键（如 Alt+Space）
    if (e->type() == QEvent::ShortcutOverride) {
        e->accept();
        return true;
    }
    return QLineEdit::event(e);
}

void HotkeyEdit::keyPressEvent(QKeyEvent* event) {
    int key = event->key();
    if (key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt || key == Qt::Key_Meta || key == Qt::Key_CapsLock) {
        return;
    }

    uint mods = 0;
    QStringList modStrings;
    if (event->modifiers() & Qt::ControlModifier) { mods |= 0x0002; modStrings << "Ctrl"; }
    if (event->modifiers() & Qt::AltModifier) { mods |= 0x0001; modStrings << "Alt"; }
    if (event->modifiers() & Qt::ShiftModifier) { mods |= 0x0004; modStrings << "Shift"; }
    if (event->modifiers() & Qt::MetaModifier) { mods |= 0x0008; modStrings << "Win"; }

    // 至少需要一个修饰符，或者 F1-F12
    if (mods == 0 && (key < Qt::Key_F1 || key > Qt::Key_F12)) return;

    m_mods = mods;
    m_vk = event->nativeVirtualKey();
    if (m_vk == 0 && key == Qt::Key_Space) m_vk = 0x20;
    
    QString keyName = (key == Qt::Key_Space) ? "Space" : QKeySequence(key).toString();
    modStrings << keyName;
    setText(modStrings.join(" + "));
}

SettingsWindow::SettingsWindow(QWidget* parent) : FramelessDialog("系统设置", parent) {
    setFixedSize(450, 600);
    initSettingsUI();
}

void SettingsWindow::initSettingsUI() {
    auto* layout = new QVBoxLayout(m_contentArea);
    layout->setContentsMargins(20, 15, 20, 15);
    layout->setSpacing(15);

    // 1. 密码管理部分
    auto* pwdGroup = new QGroupBox("安全设置", m_contentArea);
    pwdGroup->setStyleSheet("QGroupBox { color: #aaa; font-weight: bold; border: 1px solid #444; border-radius: 8px; margin-top: 10px; padding-top: 15px; } QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 3px; }");
    auto* pwdLayout = new QVBoxLayout(pwdGroup);
    
    QSettings settings("RapidNotes", "QuickWindow");
    bool hasPwd = !settings.value("appPassword").toString().isEmpty();

    auto* btnSet = new QPushButton(IconHelper::getIcon("lock", "#aaa"), " 设置启动密码");
    auto* btnModify = new QPushButton(IconHelper::getIcon("edit", "#aaa"), " 修改启动密码");
    auto* btnRemove = new QPushButton(IconHelper::getIcon("trash", "#e74c3c"), " 移除启动密码");

    QString btnStyle = "QPushButton { background-color: #2D2D2D; color: #EEE; border: 1px solid #444; border-radius: 5px; padding: 8px; text-align: left; } QPushButton:hover { background-color: #3E3E42; }";
    btnSet->setStyleSheet(btnStyle);
    btnModify->setStyleSheet(btnStyle);
    btnRemove->setStyleSheet(btnStyle);
    
    btnSet->setAutoDefault(false);
    btnModify->setAutoDefault(false);
    btnRemove->setAutoDefault(false);

    btnSet->setVisible(!hasPwd);
    btnModify->setVisible(hasPwd);
    btnRemove->setVisible(hasPwd);

    connect(btnSet, &QPushButton::clicked, this, &SettingsWindow::handleSetPassword);
    connect(btnModify, &QPushButton::clicked, this, &SettingsWindow::handleModifyPassword);
    connect(btnRemove, &QPushButton::clicked, this, &SettingsWindow::handleRemovePassword);

    pwdLayout->addWidget(btnSet);
    pwdLayout->addWidget(btnModify);
    pwdLayout->addWidget(btnRemove);
    layout->addWidget(pwdGroup);

    // 2. 快捷键设置部分
    auto* hkGroup = new QGroupBox("全局快捷键", m_contentArea);
    hkGroup->setStyleSheet("QGroupBox { color: #aaa; font-weight: bold; border: 1px solid #444; border-radius: 8px; margin-top: 5px; padding-top: 15px; } QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 3px; }");
    auto* hkLayout = new QFormLayout(hkGroup);
    hkLayout->setLabelAlignment(Qt::AlignRight);
    hkLayout->setVerticalSpacing(12);

    QSettings hotkeys("RapidNotes", "Hotkeys");
    
    m_hkQuickWin = new HotkeyEdit();
    m_hkQuickWin->setHotkey(hotkeys.value("quickWin_mods", 0x0001).toUInt(),
                            hotkeys.value("quickWin_vk", 0x20).toUInt(),
                            hotkeys.value("quickWin_display", "Alt + Space").toString());

    m_hkFavorite = new HotkeyEdit();
    m_hkFavorite->setHotkey(hotkeys.value("favorite_mods", 0x0002 | 0x0004).toUInt(),
                             hotkeys.value("favorite_vk", 0x45).toUInt(),
                             hotkeys.value("favorite_display", "Ctrl + Shift + E").toString());

    m_hkScreenshot = new HotkeyEdit();
    m_hkScreenshot->setHotkey(hotkeys.value("screenshot_mods", 0x0002 | 0x0001).toUInt(),
                               hotkeys.value("screenshot_vk", 0x41).toUInt(),
                               hotkeys.value("screenshot_display", "Ctrl + Alt + A").toString());

    m_hkOCR = new HotkeyEdit();
    m_hkOCR->setHotkey(hotkeys.value("ocr_mods", 0x0002 | 0x0001).toUInt(),
                        hotkeys.value("ocr_vk", 0x51).toUInt(),
                        hotkeys.value("ocr_display", "Ctrl + Alt + Q").toString());

    hkLayout->addRow("激活极速窗口:", m_hkQuickWin);
    hkLayout->addRow("快速收藏/加星:", m_hkFavorite);
    hkLayout->addRow("截图功能:", m_hkScreenshot);
    hkLayout->addRow("文字识别:", m_hkOCR);

    layout->addWidget(hkGroup);

    // 3. 截图设置部分
    auto* scGroup = new QGroupBox("截图设置", m_contentArea);
    scGroup->setStyleSheet("QGroupBox { color: #aaa; font-weight: bold; border: 1px solid #444; border-radius: 8px; margin-top: 5px; padding-top: 15px; } QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 3px; }");
    auto* scLayout = new QVBoxLayout(scGroup);
    
    auto* pathHint = new QLabel("截图自动保存路径 (复制/确认时同步保存):");
    pathHint->setStyleSheet("color: #888; font-size: 11px; margin-bottom: 2px;");
    scLayout->addWidget(pathHint);
    
    auto* pathBox = new QHBoxLayout();
    m_screenshotPathEdit = new QLineEdit();
    m_screenshotPathEdit->setStyleSheet("QLineEdit { background-color: #1e1e1e; border: 1px solid #333; color: #EEE; padding: 6px; border-radius: 4px; font-size: 12px; }");
    
    QSettings scSettings("RapidNotes", "Screenshot");
    QString defaultPath = QCoreApplication::applicationDirPath() + "/RPN_screenshot";
    m_screenshotPathEdit->setText(scSettings.value("savePath", defaultPath).toString());
    
    auto* btnBrowse = new QPushButton("浏览...");
    btnBrowse->setFixedSize(70, 28);
    btnBrowse->setCursor(Qt::PointingHandCursor);
    btnBrowse->setStyleSheet("QPushButton { background-color: #3E3E42; color: #EEE; border-radius: 4px; font-size: 12px; } QPushButton:hover { background-color: #4E4E52; }");
    connect(btnBrowse, &QPushButton::clicked, this, &SettingsWindow::browseScreenshotPath);
    
    pathBox->addWidget(m_screenshotPathEdit);
    pathBox->addWidget(btnBrowse);
    scLayout->addLayout(pathBox);
    
    layout->addWidget(scGroup);
    layout->addStretch();

    // 底部按钮
    auto* bottomLayout = new QHBoxLayout();
    bottomLayout->addStretch();

    auto* btnRestore = new QPushButton("恢复默认");
    btnRestore->setFixedSize(100, 32);
    btnRestore->setAutoDefault(false);
    btnRestore->setStyleSheet("QPushButton { background-color: #444; color: #eee; border: none; border-radius: 4px; } QPushButton:hover { background-color: #555; }");
    connect(btnRestore, &QPushButton::clicked, this, &SettingsWindow::handleRestoreDefaults);
    bottomLayout->addWidget(btnRestore);

    auto* btnSave = new QPushButton("保存设置");
    btnSave->setFixedSize(100, 32);
    btnSave->setAutoDefault(false);
    btnSave->setStyleSheet("QPushButton { background-color: #2cc985; color: white; border: none; border-radius: 4px; font-weight: bold; } QPushButton:hover { background-color: #229c67; }");
    connect(btnSave, &QPushButton::clicked, this, &SettingsWindow::saveSettings);
    bottomLayout->addWidget(btnSave);

    auto* btnClose = new QPushButton("关闭");
    btnClose->setFixedSize(80, 32);
    btnClose->setAutoDefault(false);
    btnClose->setStyleSheet("QPushButton { background-color: #444; color: white; border: none; border-radius: 4px; } QPushButton:hover { background-color: #555; }");
    connect(btnClose, &QPushButton::clicked, this, &QDialog::accept);
    bottomLayout->addWidget(btnClose);
    
    layout->addLayout(bottomLayout);
}

void SettingsWindow::saveSettings() {
    // 1. 保存热键
    QSettings hotkeys("RapidNotes", "Hotkeys");
    auto saveOne = [&](const QString& prefix, HotkeyEdit* edit) {
        hotkeys.setValue(prefix + "_mods", edit->getMods());
        hotkeys.setValue(prefix + "_vk", edit->getVk());
        hotkeys.setValue(prefix + "_display", edit->text());
    };
    saveOne("quickWin", m_hkQuickWin);
    saveOne("favorite", m_hkFavorite);
    saveOne("screenshot", m_hkScreenshot);
    saveOne("ocr", m_hkOCR);
    HotkeyManager::instance().reapplyHotkeys();

    // 2. 保存截图路径
    QSettings scSettings("RapidNotes", "Screenshot");
    scSettings.setValue("savePath", m_screenshotPathEdit->text().trimmed());
    
    QToolTip::showText(QCursor::pos(), StringUtils::wrapToolTip("<span style='color: #2ecc71; font-weight: bold;'>✔ 设置已保存并立即生效</span>"), this);
}

void SettingsWindow::handleSetPassword() {
    auto* dlg = new CategoryPasswordDialog("设置启动密码", this);
    if (dlg->exec() == QDialog::Accepted) {
        if (dlg->password() == dlg->confirmPassword()) {
            QSettings s("RapidNotes", "QuickWindow");
            s.setValue("appPassword", dlg->password());
            s.setValue("appPasswordHint", dlg->passwordHint());
            QToolTip::showText(QCursor::pos(), StringUtils::wrapToolTip("<span style='color: #2ecc71; font-weight: bold;'>✔ 启动密码已设置</span>"), this);
            accept(); // 关闭设置窗口以刷新状态（简单处理）
        } else {
            QToolTip::showText(QCursor::pos(), StringUtils::wrapToolTip("<span style='color: #e74c3c; font-weight: bold;'>✖ 两次输入的密码不一致</span>"), this);
        }
    }
    dlg->deleteLater();
}

void SettingsWindow::handleModifyPassword() {
    QSettings s("RapidNotes", "QuickWindow");
    auto* verifyDlg = new FramelessInputDialog("身份验证", "请输入当前启动密码:", "", this);
    verifyDlg->setEchoMode(QLineEdit::Password);
    if (verifyDlg->exec() == QDialog::Accepted) {
        if (verifyDlg->text() == s.value("appPassword").toString()) {
            auto* dlg = new CategoryPasswordDialog("修改启动密码", this);
            dlg->setInitialData(s.value("appPasswordHint").toString());
            if (dlg->exec() == QDialog::Accepted) {
                if (dlg->password() == dlg->confirmPassword()) {
                    s.setValue("appPassword", dlg->password());
                    s.setValue("appPasswordHint", dlg->passwordHint());
                    QToolTip::showText(QCursor::pos(), StringUtils::wrapToolTip("<span style='color: #2ecc71; font-weight: bold;'>✔ 启动密码已更新</span>"), this);
                    accept();
                } else {
                    QToolTip::showText(QCursor::pos(), StringUtils::wrapToolTip("<span style='color: #e74c3c; font-weight: bold;'>✖ 两次输入的密码不一致</span>"), this);
                }
            }
            dlg->deleteLater();
        } else {
            QToolTip::showText(QCursor::pos(), StringUtils::wrapToolTip("<span style='color: #e74c3c; font-weight: bold;'>✖ 密码错误，无法修改</span>"), this);
        }
    }
    verifyDlg->deleteLater();
}

void SettingsWindow::handleRemovePassword() {
    auto* verifyDlg = new FramelessInputDialog("身份验证", "请输入启动密码以移除保护:", "", this);
    verifyDlg->setEchoMode(QLineEdit::Password);
    if (verifyDlg->exec() == QDialog::Accepted) {
        QSettings s("RapidNotes", "QuickWindow");
        if (verifyDlg->text() == s.value("appPassword").toString()) {
            s.remove("appPassword");
            s.remove("appPasswordHint");
            QToolTip::showText(QCursor::pos(), StringUtils::wrapToolTip("<span style='color: #2ecc71; font-weight: bold;'>✔ 启动密码已移除</span>"), this);
            accept();
        } else {
            QToolTip::showText(QCursor::pos(), StringUtils::wrapToolTip("<span style='color: #e74c3c; font-weight: bold;'>✖ 密码错误，操作取消</span>"), this);
        }
    }
    verifyDlg->deleteLater();
}

void SettingsWindow::handleRestoreDefaults() {
    m_hkQuickWin->setHotkey(0x0001, 0x20, "Alt + Space");
    m_hkFavorite->setHotkey(0x0002 | 0x0004, 0x45, "Ctrl + Shift + E");
    m_hkScreenshot->setHotkey(0x0002 | 0x0001, 0x41, "Ctrl + Alt + A");
    m_hkOCR->setHotkey(0x0002 | 0x0001, 0x51, "Ctrl + Alt + Q");

    QString defaultPath = QCoreApplication::applicationDirPath() + "/RPN_screenshot";
    m_screenshotPathEdit->setText(defaultPath);
}

void SettingsWindow::browseScreenshotPath() {
    QString dir = QFileDialog::getExistingDirectory(this, "选择截图保存目录", m_screenshotPathEdit->text());
    if (!dir.isEmpty()) {
        m_screenshotPathEdit->setText(dir);
    }
}
