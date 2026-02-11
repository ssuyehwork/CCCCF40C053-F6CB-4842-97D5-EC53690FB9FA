#include "SettingsWindow.h"
#include "../core/ServiceLocator.h"

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
#include <QScrollArea>
#include "../core/HotkeyManager.h"
#include "../core/ShortcutManager.h"

// --- ShortcutEdit 辅助类 ---
ShortcutEdit::ShortcutEdit(QWidget* parent) : QLineEdit(parent) {
    setReadOnly(true);
    setAlignment(Qt::AlignCenter);
    setPlaceholderText("录制快捷键...");
    setStyleSheet("background-color: #1e1e1e; border: 1px solid #333; color: #2ecc71; font-weight: bold; padding: 4px; border-radius: 4px;");
}

void ShortcutEdit::setKeySequence(const QKeySequence& seq) {
    m_seq = seq;
    setText(seq.toString(QKeySequence::NativeText));
}

void ShortcutEdit::keyPressEvent(QKeyEvent* event) {
    int key = event->key();
    if (key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt || key == Qt::Key_Meta) {
        return;
    }

    QKeySequence seq(event->modifiers() | key);
    setKeySequence(seq);
}

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
    setObjectName("SettingsWindow");
    setFixedSize(700, 600);
    initSettingsUI();
}

void SettingsWindow::initSettingsUI() {
    auto* mainLayout = new QVBoxLayout(m_contentArea);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    auto* bodyLayout = new QHBoxLayout();
    bodyLayout->setSpacing(0);
    bodyLayout->setContentsMargins(0, 0, 0, 0);

    // 1. 左侧侧边栏
    m_sidebar = new QListWidget();
    m_sidebar->setFixedWidth(150);
    m_sidebar->setObjectName("SettingsSidebar");
    m_sidebar->setFocusPolicy(Qt::NoFocus);
    m_sidebar->setSpacing(0); // 强制项间距为 0，防止高度溢出
    m_sidebar->setStyleSheet(
        "QListWidget#SettingsSidebar {"
        "  background-color: #252526;"
        "  border: none;"
        "  border-right: 1px solid #333;"
        "  outline: none;"
        "  padding-top: 5px;"
        "}"
        "QListWidget#SettingsSidebar::item {"
        "  height: 40px;"
        "  min-height: 40px;"
        "  max-height: 40px;"
        "  padding: 0px 0px 0px 15px;"
        "  margin: 0px;"
        "  color: #AAA;"
        "  border-left: 3px solid transparent;"
        "}"
        "QListWidget#SettingsSidebar::item:hover {"
        "  background-color: #2D2D2D;"
        "}"
        "QListWidget#SettingsSidebar::item:selected {"
        "  background-color: #37373D;"
        "  color: #FFF;"
        "  border-left: 3px solid #3b8ed0;"
        "}"
    );

    auto addCategory = [&](const QString& name, const QString& iconName) {
        auto* item = new QListWidgetItem(IconHelper::getIcon(iconName, "#AAA", 18), name, m_sidebar);
        item->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        item->setSizeHint(QSize(0, 40)); // 强制项的高度为 40 像素，解决 QSS 失效问题
    };

    addCategory("安全设置", "lock_secure");
    addCategory("全局快捷键", "keyboard");
    addCategory("局内快捷键", "code");
    addCategory("截图设置", "screenshot");

    bodyLayout->addWidget(m_sidebar);

    // 2. 右侧内容区域
    m_pages = new QStackedWidget();
    m_pages->setStyleSheet("background-color: #1E1E1E;");
    
    m_pages->addWidget(createSecurityPage());
    m_pages->addWidget(createHotkeyPage());
    m_pages->addWidget(createAppShortcutPage());
    m_pages->addWidget(createScreenshotPage());

    bodyLayout->addWidget(m_pages, 1);
    mainLayout->addLayout(bodyLayout, 1);

    // 3. 底部按钮区域
    auto* bottomBar = new QWidget();
    bottomBar->setFixedHeight(50);
    bottomBar->setStyleSheet("background-color: #252526; border-top: 1px solid #333;");
    auto* bottomLayout = new QHBoxLayout(bottomBar);
    bottomLayout->setContentsMargins(15, 0, 15, 0);
    bottomLayout->setSpacing(10);

    auto* btnRestore = new QPushButton("恢复默认");
    btnRestore->setFixedSize(90, 30);
    btnRestore->setAutoDefault(false);
    btnRestore->setStyleSheet("QPushButton { background-color: #3E3E42; color: #EEE; border: none; border-radius: 4px; } QPushButton:hover { background-color: #4E4E52; }");
    connect(btnRestore, &QPushButton::clicked, this, &SettingsWindow::handleRestoreDefaults);
    bottomLayout->addWidget(btnRestore);

    bottomLayout->addStretch();

    auto* btnSave = new QPushButton("保存设置");
    btnSave->setFixedSize(90, 30);
    btnSave->setAutoDefault(false);
    btnSave->setStyleSheet("QPushButton { background-color: #2cc985; color: white; border: none; border-radius: 4px; font-weight: bold; } QPushButton:hover { background-color: #229c67; }");
    connect(btnSave, &QPushButton::clicked, this, &SettingsWindow::saveSettings);
    bottomLayout->addWidget(btnSave);

    auto* btnClose = new QPushButton("关闭");
    btnClose->setFixedSize(70, 30);
    btnClose->setAutoDefault(false);
    btnClose->setStyleSheet("QPushButton { background-color: #3E3E42; color: white; border: none; border-radius: 4px; } QPushButton:hover { background-color: #4E4E52; }");
    connect(btnClose, &QPushButton::clicked, this, &QDialog::accept);
    bottomLayout->addWidget(btnClose);

    mainLayout->addWidget(bottomBar);

    connect(m_sidebar, &QListWidget::currentRowChanged, this, &SettingsWindow::onCategoryChanged);
    m_sidebar->setCurrentRow(0);
}

QWidget* SettingsWindow::createSecurityPage() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(30, 30, 30, 30);
    layout->setSpacing(20);

    auto* title = new QLabel("安全设置");
    title->setStyleSheet("color: #EEE; font-size: 18px; font-weight: bold;");
    layout->addWidget(title);

    auto* desc = new QLabel("保护您的灵感库，设置应用启动密码。");
    desc->setStyleSheet("color: #888; font-size: 12px;");
    layout->addWidget(desc);

    auto* btnContainer = new QWidget();
    auto* btnLayout = new QVBoxLayout(btnContainer);
    btnLayout->setContentsMargins(0, 10, 0, 0);
    btnLayout->setSpacing(12);

    QSettings settings("RapidNotes", "QuickWindow");
    bool hasPwd = !settings.value("appPassword").toString().isEmpty();

    m_btnSetPwd = new QPushButton(IconHelper::getIcon("lock", "#aaa"), " 设置启动密码");
    m_btnModifyPwd = new QPushButton(IconHelper::getIcon("edit", "#aaa"), " 修改启动密码");
    m_btnRemovePwd = new QPushButton(IconHelper::getIcon("trash", "#e74c3c"), " 移除启动密码");

    QString btnStyle = "QPushButton { background-color: #2D2D2D; color: #EEE; border: 1px solid #444; border-radius: 6px; padding: 12px; text-align: left; font-size: 13px; } QPushButton:hover { background-color: #3E3E42; border-color: #555; }";
    m_btnSetPwd->setStyleSheet(btnStyle);
    m_btnModifyPwd->setStyleSheet(btnStyle);
    m_btnRemovePwd->setStyleSheet(btnStyle);
    
    m_btnSetPwd->setAutoDefault(false);
    m_btnModifyPwd->setAutoDefault(false);
    m_btnRemovePwd->setAutoDefault(false);

    m_btnSetPwd->setVisible(!hasPwd);
    m_btnModifyPwd->setVisible(hasPwd);
    m_btnRemovePwd->setVisible(hasPwd);

    connect(m_btnSetPwd, &QPushButton::clicked, this, &SettingsWindow::handleSetPassword);
    connect(m_btnModifyPwd, &QPushButton::clicked, this, &SettingsWindow::handleModifyPassword);
    connect(m_btnRemovePwd, &QPushButton::clicked, this, &SettingsWindow::handleRemovePassword);

    btnLayout->addWidget(m_btnSetPwd);
    btnLayout->addWidget(m_btnModifyPwd);
    btnLayout->addWidget(m_btnRemovePwd);
    layout->addWidget(btnContainer);

    layout->addStretch();
    return page;
}

QWidget* SettingsWindow::createHotkeyPage() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(30, 30, 30, 30);
    layout->setSpacing(20);

    auto* title = new QLabel("全局快捷键");
    title->setStyleSheet("color: #EEE; font-size: 18px; font-weight: bold;");
    layout->addWidget(title);

    auto* formContainer = new QWidget();
    auto* formLayout = new QFormLayout(formContainer);
    formLayout->setContentsMargins(0, 10, 0, 0);
    formLayout->setSpacing(15);
    formLayout->setLabelAlignment(Qt::AlignLeft);

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

    auto addRow = [&](const QString& label, QWidget* field) {
        auto* labelWidget = new QLabel(label);
        labelWidget->setStyleSheet("color: #AAA; font-size: 13px;");
        field->setFixedWidth(200);
        formLayout->addRow(labelWidget, field);
    };

    addRow("激活极速窗口:", m_hkQuickWin);
    addRow("快速收藏/加星:", m_hkFavorite);
    addRow("截图功能:", m_hkScreenshot);
    addRow("文字识别:", m_hkOCR);

    layout->addWidget(formContainer);
    layout->addStretch();
    return page;
}

QWidget* SettingsWindow::createScreenshotPage() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(30, 30, 30, 30);
    layout->setSpacing(20);

    auto* title = new QLabel("截图设置");
    title->setStyleSheet("color: #EEE; font-size: 18px; font-weight: bold;");
    layout->addWidget(title);

    auto* content = new QWidget();
    auto* vLayout = new QVBoxLayout(content);
    vLayout->setContentsMargins(0, 10, 0, 0);
    vLayout->setSpacing(10);

    auto* pathHint = new QLabel("截图自动保存路径 (复制/确认时同步保存):");
    pathHint->setStyleSheet("color: #AAA; font-size: 13px;");
    vLayout->addWidget(pathHint);
    
    auto* pathBox = new QHBoxLayout();
    pathBox->setSpacing(8);
    m_screenshotPathEdit = new QLineEdit();
    m_screenshotPathEdit->setStyleSheet("QLineEdit { background-color: #2D2D2D; border: 1px solid #444; color: #EEE; padding: 8px; border-radius: 4px; font-size: 12px; }");
    
    QSettings scSettings("RapidNotes", "Screenshot");
    QString defaultPath = QCoreApplication::applicationDirPath() + "/RPN_screenshot";
    m_screenshotPathEdit->setText(scSettings.value("savePath", defaultPath).toString());
    
    auto* btnBrowse = new QPushButton("浏览...");
    btnBrowse->setFixedSize(70, 32);
    btnBrowse->setCursor(Qt::PointingHandCursor);
    btnBrowse->setStyleSheet("QPushButton { background-color: #3E3E42; color: #EEE; border-radius: 4px; font-size: 12px; } QPushButton:hover { background-color: #4E4E52; }");
    connect(btnBrowse, &QPushButton::clicked, this, &SettingsWindow::browseScreenshotPath);
    
    pathBox->addWidget(m_screenshotPathEdit);
    pathBox->addWidget(btnBrowse);
    vLayout->addLayout(pathBox);
    
    layout->addWidget(content);
    layout->addStretch();
    return page;
}

void SettingsWindow::onCategoryChanged(int index) {
    m_pages->setCurrentIndex(index);
}

void SettingsWindow::saveSettings() {
    // 1. 保存全局热键
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
    ServiceLocator::get<HotkeyManager>()->reapplyHotkeys();

    // 2. 保存局内快捷键
    auto& sm = ServiceLocator::get<ShortcutManager>();
    for (auto it = m_appShortcutEdits.begin(); it != m_appShortcutEdits.end(); ++it) {
        sm.setShortcut(it.key(), it.value()->keySequence());
    }
    sm.save();

    // 3. 保存截图路径
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
            
            // 实时刷新按钮可见性
            m_btnSetPwd->setVisible(false);
            m_btnModifyPwd->setVisible(true);
            m_btnRemovePwd->setVisible(true);
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
            
            // 实时刷新按钮可见性
            m_btnSetPwd->setVisible(true);
            m_btnModifyPwd->setVisible(false);
            m_btnRemovePwd->setVisible(false);
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

    ServiceLocator::get<ShortcutManager>()->resetToDefaults();
    for (auto it = m_appShortcutEdits.begin(); it != m_appShortcutEdits.end(); ++it) {
        it.value()->setKeySequence(ServiceLocator::get<ShortcutManager>()->getShortcut(it.key()));
    }

    QString defaultPath = QCoreApplication::applicationDirPath() + "/RPN_screenshot";
    m_screenshotPathEdit->setText(defaultPath);
}

void SettingsWindow::browseScreenshotPath() {
    QString dir = QFileDialog::getExistingDirectory(this, "选择截图保存目录", m_screenshotPathEdit->text());
    if (!dir.isEmpty()) {
        m_screenshotPathEdit->setText(dir);
    }
}

QWidget* SettingsWindow::createAppShortcutPage() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(30, 20, 30, 20);
    layout->setSpacing(15);

    auto* title = new QLabel("局内快捷键");
    title->setStyleSheet("color: #EEE; font-size: 18px; font-weight: bold;");
    layout->addWidget(title);

    auto* scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    scrollArea->setStyleSheet("QScrollArea { background: transparent; border: none; } "
                              "QScrollBar:vertical { width: 8px; background: transparent; } "
                              "QScrollBar::handle:vertical { background: #333; border-radius: 4px; } "
                              "QScrollBar::handle:vertical:hover { background: #444; }");
    
    auto* container = new QWidget();
    container->setStyleSheet("background: transparent;");
    auto* formLayout = new QFormLayout(container);
    formLayout->setContentsMargins(0, 0, 15, 0);
    formLayout->setSpacing(12);
    formLayout->setLabelAlignment(Qt::AlignLeft);

    auto& sm = ServiceLocator::get<ShortcutManager>();
    auto categories = QStringList() << "极速窗口" << "主窗口" << "编辑器" << "预览窗" << "搜索窗口" << "关键字搜索";

    for (const QString& cat : categories) {
        auto* catLabel = new QLabel(cat);
        catLabel->setStyleSheet("color: #3b8ed0; font-weight: bold; font-size: 14px; margin-top: 10px; margin-bottom: 5px;");
        formLayout->addRow(catLabel);

        auto shortcuts = sm.getShortcutsByCategory(cat);
        for (const auto& info : shortcuts) {
            auto* edit = new ShortcutEdit();
            edit->setKeySequence(sm.getShortcut(info.id));
            m_appShortcutEdits[info.id] = edit;

            auto* label = new QLabel(info.description + ":");
            label->setStyleSheet("color: #AAA; font-size: 13px;");
            edit->setFixedWidth(180);
            formLayout->addRow(label, edit);
        }
    }

    scrollArea->setWidget(container);
    layout->addWidget(scrollArea);

    return page;
}
