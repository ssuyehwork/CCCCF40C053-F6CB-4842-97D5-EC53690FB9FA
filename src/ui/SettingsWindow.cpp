#include "SettingsWindow.h"
#include "CategoryPasswordDialog.h"
#include "../core/HotkeyManager.h"
#include "../core/ShortcutManager.h"
#include <QHBoxLayout>
#include <QSettings>
#include <QFileDialog>
#include <QScrollArea>
#include <QApplication>
#include <QInputDialog>
#include <QCheckBox>
#include <QPlainTextEdit>
#include "ToolTipOverlay.h"
#include "../core/DatabaseManager.h"
#include "../core/KeyboardHook.h"

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
    
    // 简单模拟 VK 到 字符串转换
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
    setFixedSize(700, 500);
    initUi();
    loadSettings();
}

void SettingsWindow::initUi() {
    auto* mainLayout = new QHBoxLayout(m_contentArea);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // 左侧导航
    m_navBar = new QListWidget();
    m_navBar->setFixedWidth(160);
    m_navBar->setSpacing(0);
    m_navBar->setStyleSheet(
        "QListWidget { background-color: #1e1e1e; border: none; border-right: 1px solid #333; outline: none; padding: 0px; }"
        "QListWidget::item { height: 40px; min-height: 40px; max-height: 40px; padding: 0px; padding-left: 15px; margin: 0px; color: #aaa; border: none; }"
        "QListWidget::item:selected { background-color: #2d2d2d; color: #3a90ff; border-left: 3px solid #3a90ff; }"
        "QListWidget::item:hover { background-color: #252525; }"
    );
    
    QStringList categories = {"安全设置", "全局热键", "局内快捷键", "截图设置", "通用设置", "软件激活"};
    m_navBar->addItems(categories);
    connect(m_navBar, &QListWidget::currentRowChanged, this, &SettingsWindow::onCategoryChanged);

    // 右侧内容
    m_contentStack = new QStackedWidget();
    m_contentStack->addWidget(createSecurityPage());
    m_contentStack->addWidget(createGlobalHotkeyPage());
    m_contentStack->addWidget(createAppShortcutPage());
    m_contentStack->addWidget(createScreenshotPage());
    m_contentStack->addWidget(createGeneralPage());
    m_contentStack->addWidget(createActivationPage());

    auto* rightLayout = new QVBoxLayout();
    rightLayout->setContentsMargins(20, 20, 20, 20);
    rightLayout->addWidget(m_contentStack);
    
    // 底部按钮
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
    layout->addStretch();
    return page;
}

QWidget* SettingsWindow::createActivationPage() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setSpacing(15);

    QVariantMap trialStatus = DatabaseManager::instance().getTrialStatus();
    bool isActive = trialStatus["is_activated"].toBool();

    if (isActive) {
        layout->addWidget(new QLabel("软件激活："));
        
        QString code = trialStatus["activation_code"].toString();
        QString masked = "";
        for (int i = 0; i < code.length(); ++i) {
            if (code[i] == '-') masked += '-';
            else if (i < 2 || (i >= 6 && i <= 7) || i >= code.length() - 4) masked += code[i];
            else masked += '*';
        }
        if (masked.isEmpty()) masked = "CA****82-****-********E25C";
        
        auto* lblActivated = new QLabel(QString("<div align='center'><b style='color: #2ecc71; font-size: 16px;'>✅ 已成功激活</b><br><br><span style='color: #a0a0a0; font-size: 16px; font-family: monospace; letter-spacing: 2px;'>%1</span></div>").arg(masked));
        lblActivated->setAlignment(Qt::AlignCenter);
        lblActivated->setStyleSheet("background: #1a1a1a; border: 1px solid #2ecc71; border-radius: 4px; padding: 20px;");
        layout->addWidget(lblActivated);
        
        auto* lblThanks = new QLabel("感谢您的支持！");
        lblThanks->setAlignment(Qt::AlignCenter);
        lblThanks->setStyleSheet("color: #aaa; font-size: 13px; margin-top: 5px;");
        layout->addWidget(lblThanks);
        
        layout->addStretch();
        return page;
    }

    layout->addWidget(new QLabel("软件激活："));
    
    m_editSecretKey = new QLineEdit();
    m_editSecretKey->setEchoMode(QLineEdit::Password);
    m_editSecretKey->setPlaceholderText("请输入激活密钥...");
    m_editSecretKey->setStyleSheet("QLineEdit { height: 36px; padding: 0 10px; background: #1a1a1a; color: #fff; border: 1px solid #333; border-radius: 4px; }");
    layout->addWidget(m_editSecretKey);

    m_lblRemainingAttempts = new QLabel();
    int failed = trialStatus["failed_attempts"].toInt();
    m_lblRemainingAttempts->setText(QString("今日剩余尝试次数: <b style='color: #f39c12;'>%1</b> / 4").arg(4 - failed));
    m_lblRemainingAttempts->setAlignment(Qt::AlignRight);
    m_lblRemainingAttempts->setStyleSheet("color: #888; font-size: 11px;");
    layout->addWidget(m_lblRemainingAttempts);

    auto* btnActivate = new QPushButton("立即激活");
    btnActivate->setFixedHeight(40);
    btnActivate->setStyleSheet("QPushButton { background: #3a90ff; color: white; border-radius: 4px; font-weight: bold; }"
                               "QPushButton:hover { background: #2b7ae6; }");
    connect(btnActivate, &QPushButton::clicked, this, &SettingsWindow::onVerifySecretKey);
    layout->addWidget(btnActivate);

    auto* lblContact = new QLabel("联系激活：<b style='color: #4a90e2;'>Telegram：TLG_888</b>");
    lblContact->setAlignment(Qt::AlignCenter);
    lblContact->setStyleSheet("color: #aaa; font-size: 13px; margin-top: 5px;");
    layout->addWidget(lblContact);

    layout->addWidget(new QLabel("<span style='color: #666; font-size: 11px;'>提示：输入正确的密钥并激活。</span>"));

    layout->addStretch();
    return page;
}



void SettingsWindow::onVerifySecretKey() {
    if (!m_editSecretKey) return;
    
    QString key = m_editSecretKey->text().trimmed();
    if (DatabaseManager::instance().verifyActivationCode(key)) {
        m_editSecretKey->clear();
        ToolTipOverlay::instance()->showText(QCursor::pos(), 
            "<b style='color: #2ecc71;'>✅ 激活成功，感谢支持！</b>", 5000, QColor("#2ecc71"));
            
        // 成功激活后，将导航栏的"软件激活"项移除，并可能切换到另一个设置页（或关闭弹窗）
        // 简单处理：给用户文字提示，UI不再需要停留在激活输入界面
        auto* oldPage = m_contentStack->widget(5);
        
        // 移除旧控件并添加已激活文本
        QWidget* parent = m_editSecretKey->parentWidget();
        if (parent) {
            QVBoxLayout* layout = qobject_cast<QVBoxLayout*>(parent->layout());
            if (layout) {
                // 清理所有旧的子控件 (跳过第一个"软件激活：")
                QLayoutItem *child;
                while ((child = layout->takeAt(1)) != nullptr) {
                    if (child->widget()) {
                        child->widget()->deleteLater();
                    }
                    delete child;
                }
                
                // 设置为null防崩溃
                m_editSecretKey = nullptr;
                m_lblRemainingAttempts = nullptr;
                
                QString code = key;
                QString masked = "";
                for (int i = 0; i < code.length(); ++i) {
                    if (code[i] == '-') masked += '-';
                    else if (i < 2 || (i >= 6 && i <= 7) || i >= code.length() - 4) masked += code[i];
                    else masked += '*';
                }
                if (masked.isEmpty()) masked = "CA****82-****-********E25C";
                
                auto* lblActivated = new QLabel(QString("<div align='center'><b style='color: #2ecc71; font-size: 16px;'>✅ 已成功激活</b><br><br><span style='color: #a0a0a0; font-size: 16px; font-family: monospace; letter-spacing: 2px;'>%1</span></div>").arg(masked));
                lblActivated->setAlignment(Qt::AlignCenter);
                lblActivated->setStyleSheet("background: #1a1a1a; border: 1px solid #2ecc71; border-radius: 4px; padding: 20px;");
                layout->addWidget(lblActivated);
                
                auto* lblThanks = new QLabel("感谢您的支持！");
                lblThanks->setAlignment(Qt::AlignCenter);
                lblThanks->setStyleSheet("color: #aaa; font-size: 13px; margin-top: 5px;");
                layout->addWidget(lblThanks);
                
                layout->addStretch();
            }
        }
    } else {
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #e74c3c;'>❌ 密钥错误，激活失败</b>");
        
        if (m_lblRemainingAttempts) {
            int failed = DatabaseManager::instance().getTrialStatus()["failed_attempts"].toInt();
            m_lblRemainingAttempts->setText(QString("今日剩余尝试次数: <b style='color: #f39c12;'>%1</b> / 4").arg(4 - failed));
        }
    }
}

QWidget* SettingsWindow::createGlobalHotkeyPage() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    
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
    addRow("激活极速窗口:", m_hkQuickWin);
    addRow("快速收藏/加星:", m_hkFavorite);
    addRow("截图功能:", m_hkScreenshot);
    addRow("截图取文 (OCR):", m_hkOcr);
    addRow("浏览器文本采集:", m_hkAcquire);
    addRow("全局锁定:", m_hkLock);
    
    layout->addStretch();
    return page;
}

QWidget* SettingsWindow::createAppShortcutPage() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    
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

QWidget* SettingsWindow::createScreenshotPage() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    
    layout->addWidget(new QLabel("截图自动保存路径："));
    auto* row = new QHBoxLayout();
    m_editScreenshotPath = new QLineEdit();
    row->addWidget(m_editScreenshotPath);
    
    auto* btnBrowse = new QPushButton("浏览...");
    connect(btnBrowse, &QPushButton::clicked, this, &SettingsWindow::onBrowsePath);
    row->addWidget(btnBrowse);
    layout->addLayout(row);
    
    layout->addWidget(new QLabel("提示：若未设置，默认保存至程序目录下的 /RPN_screenshot"));

    layout->addSpacing(20);
    layout->addWidget(new QLabel("截图取文 (OCR) 设置："));
    m_checkOcrAutoCopy = new QCheckBox("OCR识别后自动复制并隐藏窗口");
    m_checkOcrAutoCopy->setStyleSheet("color: #ccc; font-size: 14px;");
    layout->addWidget(m_checkOcrAutoCopy);

    layout->addStretch();
    return page;
}

QWidget* SettingsWindow::createGeneralPage() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setSpacing(15);

    m_checkEnterCapture = new QCheckBox("开启全局消息捕获 (回车键拦截)");
    m_checkEnterCapture->setStyleSheet("color: #ccc; font-size: 14px;");
    layout->addWidget(m_checkEnterCapture);

    auto* tip = new QLabel("提示：开启后，在其它应用中按下回车键会触发自动全选复制并存入灵感库。\n该功能具有侵入性，建议仅在特定采集场景下开启。");
    tip->setWordWrap(true);
    tip->setStyleSheet("color: #666; font-size: 12px;");
    layout->addWidget(tip);

    layout->addSpacing(20);
    layout->addWidget(new QLabel("浏览器采集进程白名单 (每行一个 .exe)："));
    m_editBrowserExes = new QPlainTextEdit();
    m_editBrowserExes->setPlaceholderText("例如:\nchrome.exe\nmsedge.exe");
    m_editBrowserExes->setStyleSheet("QPlainTextEdit { background: #1a1a1a; color: #eee; border: 1px solid #333; border-radius: 4px; padding: 5px; }");
    m_editBrowserExes->setFixedHeight(120);
    layout->addWidget(m_editBrowserExes);

    layout->addStretch();
    return page;
}

void SettingsWindow::onCategoryChanged(int index) {
    m_contentStack->setCurrentIndex(index);
}

void SettingsWindow::loadSettings() {
    // 1. 加载安全设置
    updateSecurityUI();

    // 2. 加载全局热键
    QSettings hotkeys("RapidNotes", "Hotkeys");
    m_hkQuickWin->setKeyData(hotkeys.value("quickWin_mods", 0x0001).toUInt(), hotkeys.value("quickWin_vk", 0x20).toUInt());
    m_hkFavorite->setKeyData(hotkeys.value("favorite_mods", 0x0002 | 0x0004).toUInt(), hotkeys.value("favorite_vk", 0x45).toUInt());
    m_hkScreenshot->setKeyData(hotkeys.value("screenshot_mods", 0x0002 | 0x0001).toUInt(), hotkeys.value("screenshot_vk", 0x41).toUInt());
    m_hkOcr->setKeyData(hotkeys.value("ocr_mods", 0x0002 | 0x0001).toUInt(), hotkeys.value("ocr_vk", 0x51).toUInt());
    m_hkAcquire->setKeyData(hotkeys.value("acquire_mods", 0x0002).toUInt(), hotkeys.value("acquire_vk", 0x53).toUInt());
    m_hkLock->setKeyData(hotkeys.value("lock_mods", 0x0002 | 0x0004).toUInt(), hotkeys.value("lock_vk", 0x4C).toUInt());

    // 3. 局内快捷键在创建页面时已加载

    // 4. 加载截图路径及 OCR 设置
    QSettings ss("RapidNotes", "Screenshot");
    m_editScreenshotPath->setText(ss.value("savePath", qApp->applicationDirPath() + "/RPN_screenshot").toString());

    QSettings ocr("RapidNotes", "OCR");
    m_checkOcrAutoCopy->setChecked(ocr.value("autoCopy", false).toBool());

    // 5. 加载通用设置
    QSettings gs("RapidNotes", "General");
    bool enterCapture = gs.value("enterCapture", false).toBool();
    m_checkEnterCapture->setChecked(enterCapture);
    KeyboardHook::instance().setEnterCaptureEnabled(enterCapture);

    // 加载浏览器白名单
    QSettings as("RapidNotes", "Acquisition");
    QStringList browserExes = as.value("browserExes").toStringList();
    if (browserExes.isEmpty()) {
        browserExes = {
            "chrome.exe", "msedge.exe", "firefox.exe", "brave.exe", 
            "opera.exe", "iexplore.exe", "vivaldi.exe", "safari.exe",
            "arc.exe", "sidekick.exe", "maxthon.exe", "thorium.exe"
        };
    }
    m_editBrowserExes->setPlainText(browserExes.join("\n"));
}

void SettingsWindow::updateSecurityUI() {
    QSettings settings("RapidNotes", "QuickWindow");
    bool hasPwd = !settings.value("appPassword").toString().isEmpty();
    
    m_btnSetPwd->setVisible(!hasPwd);
    m_btnModifyPwd->setVisible(hasPwd);
    m_btnRemovePwd->setVisible(hasPwd);
    m_lblPwdStatus->setText(hasPwd ? "当前状态：已启用启动密码" : "当前状态：未设置锁定窗口密码");
}

void SettingsWindow::onSetPassword() {
    CategoryPasswordDialog dlg("设置锁定窗口密码", this);
    if (dlg.exec() == QDialog::Accepted) {
        QSettings settings("RapidNotes", "QuickWindow");
        settings.setValue("appPassword", dlg.password());
        settings.setValue("appPasswordHint", dlg.passwordHint());
        updateSecurityUI();
    }
}

void SettingsWindow::onModifyPassword() {
    // 简单起见，这里复用对话框，逻辑上通常先验证旧密码，这里按提示直接覆盖或弹出交互
    CategoryPasswordDialog dlg("修改启动密码", this);
    QSettings settings("RapidNotes", "QuickWindow");
    dlg.setInitialData(settings.value("appPasswordHint").toString());
    if (dlg.exec() == QDialog::Accepted) {
        settings.setValue("appPassword", dlg.password());
        settings.setValue("appPasswordHint", dlg.passwordHint());
        updateSecurityUI();
    }
}

void SettingsWindow::onRemovePassword() {
    // 移除前需要验证（此处为了逻辑闭环简单弹窗验证，或要求输入当前密码）
    QSettings settings("RapidNotes", "QuickWindow");
    QString realPwd = settings.value("appPassword").toString();

    // 弹出简单对话框要求确认
    bool ok = false;
    QString input = QInputDialog::getText(this, "身份验证", "请输入当前密码以移除：", QLineEdit::Password, "", &ok);
    if (ok && input == realPwd) {
        settings.remove("appPassword");
        settings.remove("appPasswordHint");
        updateSecurityUI();
    } else if (ok) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), 
            "<b style='color: #e74c3c;'>❌ 密码错误，无法移除</b>");
    }
}

void SettingsWindow::onBrowsePath() {
    QString dir = QFileDialog::getExistingDirectory(this, "选择截图保存目录", m_editScreenshotPath->text());
    if (!dir.isEmpty()) {
        m_editScreenshotPath->setText(dir);
    }
}

void SettingsWindow::onSaveClicked() {
    // 1. 保存全局热键
    QSettings hotkeys("RapidNotes", "Hotkeys");
    hotkeys.setValue("quickWin_mods", m_hkQuickWin->mods());
    hotkeys.setValue("quickWin_vk", m_hkQuickWin->vk());
    hotkeys.setValue("favorite_mods", m_hkFavorite->mods());
    hotkeys.setValue("favorite_vk", m_hkFavorite->vk());
    hotkeys.setValue("screenshot_mods", m_hkScreenshot->mods());
    hotkeys.setValue("screenshot_vk", m_hkScreenshot->vk());
    hotkeys.setValue("ocr_mods", m_hkOcr->mods());
    hotkeys.setValue("ocr_vk", m_hkOcr->vk());
    hotkeys.setValue("acquire_mods", m_hkAcquire->mods());
    hotkeys.setValue("acquire_vk", m_hkAcquire->vk());
    hotkeys.setValue("lock_mods", m_hkLock->mods());
    hotkeys.setValue("lock_vk", m_hkLock->vk());
    
    HotkeyManager::instance().reapplyHotkeys();

    // 2. 保存局内快捷键
    auto& sm = ShortcutManager::instance();
    auto edits = m_contentStack->widget(2)->findChildren<ShortcutEdit*>();
    for (auto* edit : edits) {
        sm.setShortcut(edit->property("id").toString(), edit->keySequence());
    }
    sm.save();

    // 3. 保存截图及 OCR 设置
    QSettings ss("RapidNotes", "Screenshot");
    ss.setValue("savePath", m_editScreenshotPath->text());

    QSettings ocr("RapidNotes", "OCR");
    ocr.setValue("autoCopy", m_checkOcrAutoCopy->isChecked());

    // 4. 保存通用设置
    QSettings gs("RapidNotes", "General");
    bool enterCapture = m_checkEnterCapture->isChecked();
    gs.setValue("enterCapture", enterCapture);
    KeyboardHook::instance().setEnterCaptureEnabled(enterCapture);

    // 保存浏览器白名单
    QSettings as("RapidNotes", "Acquisition");
    QStringList browserExes = m_editBrowserExes->toPlainText().split("\n", Qt::SkipEmptyParts);
    for(QString& s : browserExes) s = s.trimmed().toLower();
    as.setValue("browserExes", browserExes);

    ToolTipOverlay::instance()->showText(QCursor::pos(), 
        "<b style='color: #2ecc71;'>✅ 设置已保存并立即生效</b>");
}

void SettingsWindow::onRestoreDefaults() {
    bool ok = false;
    QString input = QInputDialog::getText(this, "恢复默认设置", 
                                          "确认恢复默认设置？所有配置都将被重置。\n请输入“confirm”以继续：", 
                                          QLineEdit::Normal, "", &ok);
    
    if (ok && input.toLower() == "confirm") {
        // 1. 清除各部分的设置
        QSettings("RapidNotes", "Hotkeys").clear();
        QSettings("RapidNotes", "QuickWindow").clear();
        QSettings("RapidNotes", "Screenshot").clear();
        QSettings("RapidNotes", "Acquisition").clear();
        
        // 2. 局内快捷键重置
        ShortcutManager::instance().resetToDefaults();
        ShortcutManager::instance().save();
        
        // 3. 立即重载热键
        HotkeyManager::instance().reapplyHotkeys();
        
        // 4. 重新加载界面
        loadSettings();
        
        ToolTipOverlay::instance()->showText(QCursor::pos(), 
            "<b style='color: #3498db;'>ℹ️ 已恢复默认设置</b>");
    }
}
