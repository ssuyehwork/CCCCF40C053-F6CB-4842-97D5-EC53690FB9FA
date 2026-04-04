#include "SettingsWindow.h"
#include "CategoryPasswordDialog.h"
#include "../core/HotkeyManager.h"
#include "../core/ShortcutManager.h"
#include <QHBoxLayout>
#include <QSettings>
#include <functional>
#include <QFileDialog>
#include <QScrollArea>
#include <QApplication>
#include <QInputDialog>
#include <QCheckBox>
#include <QPlainTextEdit>
#include <QFileInfo>
#include <QSysInfo>
#include "ToolTipOverlay.h"
#include "../core/KeyboardHook.h"
#include "../core/HardwareInfoHelper.h"
#include <QClipboard>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

// --- HotkeyEdit 实现 ---
HotkeyEdit::HotkeyEdit(QWidget* parent) : QLineEdit(parent) {
    setReadOnly(true);
    setPlaceholderText("按键设置...");
    setAlignment(Qt::AlignCenter);
    setStyleSheet("QLineEdit { background: #1a1a1a; color: #4a90e2; font-weight: bold; border-radius: 4px; padding: 4px; border: 1px solid #333; }");
}

void HotkeyEdit::setKeyData(uint mods, uint vk) {
    m_mods = mods;
    m_vk = vk;
    setText(keyToString(mods, vk));
}

void HotkeyEdit::keyPressEvent(QKeyEvent* event) {
    int key = event->key();
    if (key == Qt::Key_Escape || key == Qt::Key_Backspace) {
        m_mods = 0;
        m_vk = 0;
        setText("");
        return;
    }

    if (key >= Qt::Key_Control && key <= Qt::Key_Meta) return;

    uint winMods = 0;
    if (event->modifiers() & Qt::ControlModifier) winMods |= 0x0002; // MOD_CONTROL
    if (event->modifiers() & Qt::AltModifier)     winMods |= 0x0001; // MOD_ALT
    if (event->modifiers() & Qt::ShiftModifier)   winMods |= 0x0004; // MOD_SHIFT
    if (event->modifiers() & Qt::MetaModifier)    winMods |= 0x0008; // MOD_WIN

    m_mods = winMods;
    m_vk = event->nativeVirtualKey();
    if (m_vk == 0) m_vk = key; // 兜底处理

    setText(keyToString(m_mods, m_vk));
}

QString HotkeyEdit::keyToString(uint mods, uint vk) {
    if (vk == 0) return "";
    QStringList parts;
    if (mods & 0x0002) parts << "Ctrl";
    if (mods & 0x0001) parts << "Alt";
    if (mods & 0x0004) parts << "Shift";
    if (mods & 0x0008) parts << "Win";
    
    QKeySequence ks(vk);
    parts << ks.toString();
    return parts.join(" + ");
}

// --- ShortcutEdit 实现 ---
ShortcutEdit::ShortcutEdit(QWidget* parent) : QLineEdit(parent) {
    setReadOnly(true);
    setPlaceholderText("录制快捷键...");
}

void ShortcutEdit::setKeySequence(const QKeySequence& seq) {
    m_seq = seq;
    setText(m_seq.toString());
}

void ShortcutEdit::keyPressEvent(QKeyEvent* event) {
    int key = event->key();
    if (key == Qt::Key_Escape || key == Qt::Key_Backspace) {
        m_seq = QKeySequence();
        setText("");
        return;
    }
    if (key >= Qt::Key_Control && key <= Qt::Key_Meta) return;

    m_seq = QKeySequence(event->modifiers() | key);
    setText(m_seq.toString());
}

// --- SettingsWindow 实现 ---
SettingsWindow::SettingsWindow(QWidget* parent)
    : FramelessDialog("系统设置", parent)
{
    if (m_btnPin) m_btnPin->hide();
    if (m_minBtn) m_minBtn->hide();
    if (m_maxBtn) m_maxBtn->hide();

    setFixedWidth(700);
    setMinimumHeight(400);
    
    initUi();
    loadSettings();

    QTimer::singleShot(50, [this]() { adjustHeightToContent(false); });
}

void SettingsWindow::initUi() {
    auto* mainLayout = new QHBoxLayout(m_contentArea);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    m_navBar = new QListWidget();
    m_navBar->setFixedWidth(160);
    m_navBar->setSpacing(0);
    m_navBar->setStyleSheet(
        "QListWidget { background-color: #1e1e1e; border: none; border-right: 1px solid #333; outline: none; padding: 0px; }"
        "QListWidget::item { height: 40px; min-height: 40px; max-height: 40px; padding: 0px; padding-left: 15px; margin: 0px; color: #aaa; border: none; }"
        "QListWidget::item:selected { background-color: #3e3e42; color: #3a90ff; border-left: 3px solid #3a90ff; }"
        "QListWidget::item:hover { background-color: #3e3e42; }"
    );
    
    QStringList categories = {"安全设置", "全局热键", "局内快捷键", "通用设置", "软件激活", "设备信息"};
    m_navBar->addItems(categories);
    connect(m_navBar, &QListWidget::currentRowChanged, this, &SettingsWindow::onCategoryChanged);

    m_contentStack = new QStackedWidget();
    m_contentStack->addWidget(createSecurityPage());
    m_contentStack->addWidget(createGlobalHotkeyPage());
    m_contentStack->addWidget(createAppShortcutPage());
    m_contentStack->addWidget(createGeneralPage());
    m_contentStack->addWidget(createActivationPage());
    m_contentStack->addWidget(createDeviceInfoPage());

    auto* scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setWidget(m_contentStack);
    scrollArea->setStyleSheet("QScrollArea { background: transparent; } QScrollBar:vertical { width: 8px; background: transparent; }");

    auto* rightLayout = new QVBoxLayout();
    rightLayout->setContentsMargins(20, 20, 20, 20);
    rightLayout->setSpacing(25); 
    rightLayout->addWidget(scrollArea, 1);
    
    auto* btnLayout = new QHBoxLayout();
    
    auto* btnRestore = new QPushButton("恢复默认设置");
    btnRestore->setFixedSize(120, 36);
    btnRestore->setStyleSheet("QPushButton { background: #444; color: #ccc; border-radius: 4px; font-weight: normal; }"
                              "QPushButton:hover { background: #555; color: white; }");
    connect(btnRestore, &QPushButton::clicked, this, &SettingsWindow::onRestoreDefaults);
    btnLayout->addWidget(btnRestore);

    btnLayout->addStretch();
    auto* btnSave = new QPushButton("保存并生效");
    btnSave->setFixedSize(120, 36);
    btnSave->setStyleSheet("QPushButton { background: #3a90ff; color: white; border-radius: 4px; font-weight: bold; }"
                           "QPushButton:hover { background: #2b7ae6; }");
    connect(btnSave, &QPushButton::clicked, this, &SettingsWindow::onSaveClicked);
    btnLayout->addWidget(btnSave);
    rightLayout->addLayout(btnLayout);

    mainLayout->addWidget(m_navBar);
    mainLayout->addLayout(rightLayout);
    
    m_navBar->setCurrentRow(0);
}

QWidget* SettingsWindow::createSecurityPage() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(20);

    m_lblPwdStatus = new QLabel("当前状态：未设置锁定窗口密码");
    m_lblPwdStatus->setStyleSheet("color: #888; font-size: 14px;");
    layout->addWidget(m_lblPwdStatus);

    m_btnSetPwd = new QPushButton("设置锁定窗口密码");
    m_btnModifyPwd = new QPushButton("修改启动密码");
    m_btnRemovePwd = new QPushButton("彻底移除密码");

    QString btnStyle = "QPushButton { height: 40px; background: #2d2d2d; color: #eee; border: 1px solid #444; border-radius: 6px; }"
                       "QPushButton:hover { background: #3d3d3d; }";
    m_btnSetPwd->setStyleSheet(btnStyle);
    m_btnModifyPwd->setStyleSheet(btnStyle);
    m_btnRemovePwd->setStyleSheet("QPushButton { height: 40px; background: #442222; color: #f66; border: 1px solid #633; border-radius: 6px; }"
                                  "QPushButton:hover { background: #552222; }");

    connect(m_btnSetPwd, &QPushButton::clicked, this, &SettingsWindow::onSetPassword);
    connect(m_btnModifyPwd, &QPushButton::clicked, this, &SettingsWindow::onModifyPassword);
    connect(m_btnRemovePwd, &QPushButton::clicked, this, &SettingsWindow::onRemovePassword);

    layout->addWidget(m_btnSetPwd);
    layout->addWidget(m_btnModifyPwd);
    layout->addWidget(m_btnRemovePwd);

    layout->addSpacing(10);
    m_checkIdleLock = new QCheckBox("30秒全系统闲置后自动锁定应用");
    m_checkIdleLock->setStyleSheet("color: #ccc; font-size: 14px;");
    
    connect(m_checkIdleLock, &QCheckBox::clicked, this, [this](bool checked) {
        if (!checked) {
            QSettings settings("ArcMeta", "ArcMeta");
            QString realPwd = settings.value("appPassword").toString();
            if (realPwd.isEmpty()) return;

            bool ok = false;
            QString input = QInputDialog::getText(this, "身份验证", 
                                                  "关闭自动锁定功能需要验证密码：", 
                                                  QLineEdit::Password, "", &ok);
            
            if (!ok || input != realPwd) {
                m_checkIdleLock->setChecked(true);
                if (ok) {
                    ToolTipOverlay::instance()->showText(QCursor::pos(), 
                        "<b style='color: #e74c3c;'>❌ 密码验证失败</b>");
                }
            } else {
                ToolTipOverlay::instance()->showText(QCursor::pos(), 
                    "<b style='color: #2ecc71;'>✅ 身份验证通过</b>");
            }
        }
    });
    
    layout->addWidget(m_checkIdleLock);
    layout->addStretch();
    return page;
}

QWidget* SettingsWindow::createActivationPage() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(15);

    layout->addWidget(new QLabel("软件激活："));
    auto* lblActivated = new QLabel("<div align='center'><b style='color: #2ecc71; font-size: 16px;'>✅ 已成功激活 (内部版本)</b><br><br><span style='color: #a0a0a0; font-size: 16px; font-family: monospace; letter-spacing: 2px;'>ARCMETA-INTERNAL-FULL-ACCESS</span></div>");
    lblActivated->setAlignment(Qt::AlignCenter);
    lblActivated->setStyleSheet("background: #1a1a1a; border: 1px solid #2ecc71; border-radius: 4px; padding: 20px;");
    layout->addWidget(lblActivated);
    
    auto* lblThanks = new QLabel("当前为 ArcMeta 内部预览版，全功能已解锁。");
    lblThanks->setAlignment(Qt::AlignCenter);
    lblThanks->setStyleSheet("color: #aaa; font-size: 13px; margin-top: 5px;");
    layout->addWidget(lblThanks);

    layout->addStretch();
    return page;
}

QWidget* SettingsWindow::createDeviceInfoPage() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(15);

    layout->addWidget(new QLabel("当前设备指纹信息："));

    m_editDeviceInfo = new QPlainTextEdit();
    m_editDeviceInfo->setReadOnly(true);
    m_editDeviceInfo->setStyleSheet("QPlainTextEdit { background: #1a1a1a; color: #3a90ff; border: 1px solid #333; border-radius: 4px; padding: 12px; font-family: 'Consolas', monospace; font-size: 13px; }");
    
    QString diskSn = HardwareInfoHelper::getDiskPhysicalSerialNumber();
    QString machineId = QSysInfo::machineUniqueId();
    if (machineId.isEmpty()) machineId = QSysInfo::bootUniqueId();
    
    QString info = QString("Disk SN: %1\nMachine ID: %2\nOS: %3")
                   .arg(diskSn.isEmpty() ? "Unknown" : diskSn)
                   .arg(machineId.isEmpty() ? "Unknown" : machineId)
                   .arg(QSysInfo::prettyProductName());
    
    m_editDeviceInfo->setPlainText(info);
    layout->addWidget(m_editDeviceInfo);

    auto* btnCopy = new QPushButton("复制设备指纹信息");
    btnCopy->setFixedHeight(40);
    btnCopy->setStyleSheet("QPushButton { background: #2d2d2d; color: #eee; border: 1px solid #444; border-radius: 4px; font-weight: bold; }"
                            "QPushButton:hover { background: #3d3d3d; color: #fff; }");
    connect(btnCopy, &QPushButton::clicked, this, &SettingsWindow::onCopyDeviceInfo);
    layout->addWidget(btnCopy);

    layout->addStretch();
    return page;
}

void SettingsWindow::onCopyDeviceInfo() {
    if (!m_editDeviceInfo) return;
    QString info = m_editDeviceInfo->toPlainText();
    QApplication::clipboard()->setText(info);
    ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #2ecc71;'>✅ 设备信息已成功复制到剪贴板</b>");
}

void SettingsWindow::onVerifySecretKey() {
    ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #3498db;'>ℹ️ 当前为 ArcMeta 内部版本，无需激活</b>");
}

QWidget* SettingsWindow::createGlobalHotkeyPage() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    
    auto addRow = [&](const QString& label, HotkeyEdit*& edit) {
        auto* hl = new QHBoxLayout();
        hl->addWidget(new QLabel(label));
        edit = new HotkeyEdit();
        edit->setFixedWidth(200);
        hl->addWidget(edit);
        layout->addLayout(hl);
    };

    layout->addWidget(new QLabel("系统全局热键，修改后点击保存立即生效："));
    layout->addSpacing(10);
    addRow("激活主界面窗口:", m_hkQuickWin);
    
    layout->addStretch();
    return page;
}

QWidget* SettingsWindow::createAppShortcutPage() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    
    auto* scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setStyleSheet("QScrollArea { border: none; background: transparent; }");
    
    auto* container = new QWidget();
    auto* vLayout = new QVBoxLayout(container);
    
    auto& sm = ShortcutManager::instance();
    QString currentCat = "";
    
    for (const auto& info : sm.getAllShortcuts()) {
        if (info.category != currentCat) {
            currentCat = info.category;
            auto* catLabel = new QLabel(currentCat);
            catLabel->setStyleSheet("color: #3a90ff; font-weight: bold; margin-top: 15px; border-bottom: 1px solid #333;");
            vLayout->addWidget(catLabel);
        }
        
        auto* row = new QHBoxLayout();
        row->addWidget(new QLabel(info.description));
        auto* edit = new ShortcutEdit();
        edit->setKeySequence(sm.getShortcut(info.id));
        edit->setProperty("id", info.id);
        edit->setFixedWidth(150);
        row->addWidget(edit);
        vLayout->addLayout(row);
    }
    
    scroll->setWidget(container);
    layout->addWidget(scroll);
    return page;
}

QWidget* SettingsWindow::createGeneralPage() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(15);

    m_checkAutoStart = new QCheckBox("开机自动启动");
    m_checkAutoStart->setStyleSheet("color: #ccc; font-size: 14px;");
    layout->addWidget(m_checkAutoStart);

    layout->addSpacing(10);
    m_checkCapsLockToEnter = new QCheckBox("将 CapsLock (大写锁定) 替代 Enter (回车键)");
    m_checkCapsLockToEnter->setStyleSheet("color: #ccc; font-size: 14px;");
    layout->addWidget(m_checkCapsLockToEnter);
    
    auto* capsTip = new QLabel("提示：开启后，单独按下 CapsLock 将触发回车键功能（全局生效）；\n若需要切换大写锁定状态，请使用组合键：Ctrl + CapsLock。");
    capsTip->setWordWrap(true);
    capsTip->setStyleSheet("color: #666; font-size: 12px; line-height: 1.4; padding-left: 24px;");
    layout->addWidget(capsTip);

    layout->addStretch();
    return page;
}

bool SettingsWindow::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        if (auto* edit = qobject_cast<QLineEdit*>(watched)) {
            if (watched->inherits("HotkeyEdit") || watched->inherits("ShortcutEdit")) {
                return FramelessDialog::eventFilter(watched, event);
            }
            if (keyEvent->key() == Qt::Key_Up) {
                edit->setCursorPosition(0);
                return true;
            } else if (keyEvent->key() == Qt::Key_Down) {
                edit->setCursorPosition(edit->text().length());
                return true;
            }
        }
    }
    return FramelessDialog::eventFilter(watched, event);
}

void SettingsWindow::onCategoryChanged(int index) {
    m_contentStack->setCurrentIndex(index);
}

void SettingsWindow::adjustHeightToContent(bool) {
    resize(700, 600);
}

void SettingsWindow::loadSettings() {
    updateSecurityUI();
    QSettings securitySettings("ArcMeta", "Security");
    m_checkIdleLock->setChecked(securitySettings.value("idleLockEnabled", false).toBool());

    QSettings hotkeys("ArcMeta", "Hotkeys");
    m_hkQuickWin->setKeyData(hotkeys.value("quickWin_mods", 0x0001).toUInt(), hotkeys.value("quickWin_vk", 0x20).toUInt());

    QSettings gs("ArcMeta", "General");
#ifdef Q_OS_WIN
    QSettings bootSettings("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run", QSettings::NativeFormat);
    m_checkAutoStart->setChecked(bootSettings.contains("ArcMeta"));
#endif

    bool capsLockToEnter = gs.value("capsLockToEnter", false).toBool();
    m_checkCapsLockToEnter->setChecked(capsLockToEnter);
    KeyboardHook::instance().setCapsLockToEnterEnabled(capsLockToEnter);
}

void SettingsWindow::updateSecurityUI() {
    QSettings settings("ArcMeta", "ArcMeta");
    bool hasPwd = !settings.value("appPassword").toString().isEmpty();
    m_btnSetPwd->setVisible(!hasPwd);
    m_btnModifyPwd->setVisible(hasPwd);
    m_btnRemovePwd->setVisible(hasPwd);
    m_lblPwdStatus->setText(hasPwd ? "当前状态：已启用启动密码" : "当前状态：未设置锁定窗口密码");
}

void SettingsWindow::onSetPassword() {
    CategoryPasswordDialog dlg("设置锁定窗口密码", this);
    if (dlg.exec() == QDialog::Accepted) {
        QSettings settings("ArcMeta", "ArcMeta");
        settings.setValue("appPassword", dlg.password());
        settings.setValue("appPasswordHint", dlg.passwordHint());
        updateSecurityUI();
    }
}

void SettingsWindow::onModifyPassword() {
    CategoryPasswordDialog dlg("修改启动密码", this);
    QSettings settings("ArcMeta", "ArcMeta");
    dlg.setInitialData(settings.value("appPasswordHint").toString());
    if (dlg.exec() == QDialog::Accepted) {
        settings.setValue("appPassword", dlg.password());
        settings.setValue("appPasswordHint", dlg.passwordHint());
        updateSecurityUI();
    }
}

void SettingsWindow::onRemovePassword() {
    QSettings settings("ArcMeta", "ArcMeta");
    QString realPwd = settings.value("appPassword").toString();
    bool ok = false;
    QString input = QInputDialog::getText(this, "身份验证", "请输入当前密码以移除：", QLineEdit::Password, "", &ok);
    if (ok && input == realPwd) {
        settings.remove("appPassword");
        settings.remove("appPasswordHint");
        updateSecurityUI();
    } else if (ok) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #e74c3c;'>❌ 密码错误，无法移除</b>");
    }
}

void SettingsWindow::onSaveClicked() {
    QSettings hotkeys("ArcMeta", "Hotkeys");
    hotkeys.setValue("quickWin_mods", m_hkQuickWin->mods());
    hotkeys.setValue("quickWin_vk", m_hkQuickWin->vk());
    HotkeyManager::instance().reapplyHotkeys();

    auto& sm = ShortcutManager::instance();
    auto edits = m_contentStack->widget(2)->findChildren<ShortcutEdit*>();
    for (auto* edit : edits) {
        sm.setShortcut(edit->property("id").toString(), edit->keySequence());
    }
    sm.save();

    QSettings gs("ArcMeta", "General");
#ifdef Q_OS_WIN
    QSettings bootSettings("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run", QSettings::NativeFormat);
    if (m_checkAutoStart->isChecked()) {
        QString appPath = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
        bootSettings.setValue("ArcMeta", "\"" + appPath + "\"");
    } else {
        bootSettings.remove("ArcMeta");
    }
#endif

    bool capsLockToEnter = m_checkCapsLockToEnter->isChecked();
    gs.setValue("capsLockToEnter", capsLockToEnter);
    KeyboardHook::instance().setCapsLockToEnterEnabled(capsLockToEnter);

    QSettings securitySettings("ArcMeta", "Security");
    securitySettings.setValue("idleLockEnabled", m_checkIdleLock->isChecked());

    ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #2ecc71;'>✅ 设置已保存并立即生效</b>");
}

void SettingsWindow::onRestoreDefaults() {
    bool ok = false;
    QString input = QInputDialog::getText(this, "恢复默认设置", "确认恢复默认设置？所有配置都将被重置。\n请输入“confirm”以继续：", QLineEdit::Normal, "", &ok);
    if (ok && input.toLower() == "confirm") {
        QSettings("ArcMeta", "Hotkeys").clear();
        QSettings("ArcMeta", "ArcMeta").clear();
        QSettings("ArcMeta", "Screenshot").clear();
        QSettings("ArcMeta", "Acquisition").clear();
        ShortcutManager::instance().resetToDefaults();
        ShortcutManager::instance().save();
        HotkeyManager::instance().reapplyHotkeys();
        loadSettings();
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #3498db;'>ℹ️ 已恢复默认设置</b>");
    }
}
