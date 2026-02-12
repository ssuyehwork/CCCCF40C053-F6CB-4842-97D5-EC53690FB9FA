#include <QSettings>
#include <QApplication>
#include <QFile>
#include <QToolTip>
#include <QCursor>
#include <QMessageBox>
#include <QCoreApplication>
#include <QDir>
#include <QDebug>
#include <QDateTime>
#include <QFileInfo>
#include <QBuffer>
#include <QTimer>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>
#include <functional>
#include "core/DatabaseManager.h"
#include "core/HotkeyManager.h"
#include "core/ClipboardMonitor.h"
#include "core/OCRManager.h"
#include "ui/MainWindow.h"
#include "ui/FloatingBall.h"
#include "ui/QuickWindow.h"
#include "ui/SystemTray.h"
#include "ui/Toolbox.h"
#include "ui/TimePasteWindow.h"
#include "ui/PasswordGeneratorWindow.h"
#include "ui/OCRWindow.h"
#include "ui/OCRResultWindow.h"
#include "ui/KeywordSearchWindow.h"
#include "ui/FileStorageWindow.h"
#include "ui/TagManagerWindow.h"
#include "ui/FileSearchWindow.h"
#include "ui/ColorPickerWindow.h"
#include "ui/HelpWindow.h"
#include "ui/FireworksOverlay.h"
#include "ui/ScreenshotTool.h"
#include "ui/SettingsWindow.h"
#include "ui/StringUtils.h"
#include "core/KeyboardHook.h"
#include "core/FileCryptoHelper.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

#ifdef Q_OS_WIN
/**
 * @brief 判定当前活跃窗口是否为浏览器
 */
static bool isBrowserActive() {
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return false;

    DWORD pid;
    GetWindowThreadProcessId(hwnd, &pid);
    
    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!process) return false;

    wchar_t buffer[MAX_PATH];
    // 使用 GetModuleFileNameExW 获取完整路径
    if (GetModuleFileNameExW(process, NULL, buffer, MAX_PATH)) {
        QString exePath = QString::fromWCharArray(buffer).toLower();
        QString exeName = QFileInfo(exePath).fileName();
        qDebug() << "[Acquire] 当前活跃窗口进程:" << exeName;

        static const QStringList browserExes = {
            "chrome.exe", "msedge.exe", "firefox.exe", "brave.exe", 
            "opera.exe", "iexplore.exe", "vivaldi.exe", "safari.exe"
        };
        
        CloseHandle(process);
        return browserExes.contains(exeName);
    }

    CloseHandle(process);
    return false;
}
#endif

int main(int argc, char *argv[]) {
    QApplication a(argc, argv);
    a.setApplicationName("RapidNotes");
    a.setOrganizationName("RapidDev");
    a.setQuitOnLastWindowClosed(false);

    // 单实例运行保护
    QString serverName = "RapidNotes_SingleInstance_Server";
    QLocalSocket socket;
    socket.connectToServer(serverName);
    if (socket.waitForConnected(500)) {
        // 如果已经运行，发送 SHOW 信号并退出当前进程
        socket.write("SHOW");
        socket.waitForBytesWritten(1000);
        return 0;
    }
    QLocalServer::removeServer(serverName);
    QLocalServer server;
    if (!server.listen(serverName)) {
        qWarning() << "无法启动单实例服务器";
    }

    // 加载全局样式表
    QFile styleFile(":/qss/dark_style.qss");
    if (styleFile.open(QFile::ReadOnly)) {
        a.setStyleSheet(styleFile.readAll());
    }

    // 1. 初始化数据库 (外壳文件名改为 inspiration.db)
    QString dbPath = QCoreApplication::applicationDirPath() + "/inspiration.db";
    qDebug() << "[Main] 数据库外壳路径:" << dbPath;

    if (!DatabaseManager::instance().init(dbPath)) {
        QMessageBox::critical(nullptr, "启动失败", 
            "无法初始化数据库！\n请检查是否有写入权限，或缺少 SQLite 驱动。");
        return -1;
    }


    // 1.1 试用期与使用次数检查
    QVariantMap trialStatus = DatabaseManager::instance().getTrialStatus();
    if (trialStatus["expired"].toBool() || trialStatus["usage_limit_reached"].toBool()) {
        QString reason = trialStatus["expired"].toBool() ? "您的 30 天试用期已结束。" : "您的 500 次使用额度已用完。";
        QMessageBox::information(nullptr, "试用结束", 
            reason + "\n感谢您体验 RapidNotes！如需继续使用，请联系开发者获取授权。");
        DatabaseManager::instance().closeAndPack();
        return 0;
    }

    // 2. 初始化核心 UI 组件 (极速窗口与悬浮球)
    QuickWindow* quickWin = new QuickWindow();
    quickWin->setObjectName("QuickWindow");
    quickWin->showAuto();

    // 3. 初始化特效层与悬浮球
    FireworksOverlay::instance(); 
    FloatingBall* ball = new FloatingBall();
    ball->setObjectName("FloatingBall");

    a.setWindowIcon(FloatingBall::generateBallIcon());

    // 4. 子窗口延迟加载策略
    MainWindow* mainWin = nullptr;
    Toolbox* toolbox = nullptr;
    TimePasteWindow* timePasteWin = nullptr;
    PasswordGeneratorWindow* passwordGenWin = nullptr;
    OCRWindow* ocrWin = nullptr;
    KeywordSearchWindow* keywordSearchWin = nullptr;
    TagManagerWindow* tagMgrWin = nullptr;
    FileStorageWindow* fileStorageWin = nullptr;
    FileSearchWindow* fileSearchWin = nullptr;
    ColorPickerWindow* colorPickerWin = nullptr;
    HelpWindow* helpWin = nullptr;

    auto toggleWindow = [](QWidget* win, QWidget* parentWin = nullptr) {
        if (!win) return;
        if (win->isVisible()) {
            win->hide();
        } else {
            if (parentWin && win->objectName() != "ToolboxLauncher") {
                if (parentWin->objectName() == "QuickWindow") {
                    win->move(parentWin->x() - win->width() - 10, parentWin->y());
                } else {
                    win->move(parentWin->geometry().center() - win->rect().center());
                }
            }
            win->show();
            win->raise();
            win->activateWindow();
        }
    };

    auto checkLockAndExecute = [&](std::function<void()> func) {
        if (quickWin->isLocked()) {
            quickWin->showAuto();
            return;
        }
        func();
    };

    std::function<void()> showMainWindow;
    std::function<void()> startScreenshot;
    std::function<void()> startImmediateOCR;

    auto getToolbox = [&]() -> Toolbox* {
        if (!toolbox) {
            toolbox = new Toolbox();
            toolbox->setObjectName("ToolboxLauncher");
            
            QObject::connect(toolbox, &Toolbox::showTimePasteRequested, [=, &timePasteWin](){
                if (!timePasteWin) {
                    timePasteWin = new TimePasteWindow();
                    timePasteWin->setObjectName("TimePasteWindow");
                }
                toggleWindow(timePasteWin);
            });
            QObject::connect(toolbox, &Toolbox::showPasswordGeneratorRequested, [=, &passwordGenWin](){
                if (!passwordGenWin) {
                    passwordGenWin = new PasswordGeneratorWindow();
                    passwordGenWin->setObjectName("PasswordGeneratorWindow");
                }
                toggleWindow(passwordGenWin);
            });
            QObject::connect(toolbox, &Toolbox::showOCRRequested, [=, &ocrWin](){
                if (!ocrWin) {
                    ocrWin = new OCRWindow();
                    ocrWin->setObjectName("OCRWindow");
                }
                toggleWindow(ocrWin);
            });
            QObject::connect(toolbox, &Toolbox::showKeywordSearchRequested, [=, &keywordSearchWin](){
                if (!keywordSearchWin) {
                    keywordSearchWin = new KeywordSearchWindow();
                    keywordSearchWin->setObjectName("KeywordSearchWindow");
                }
                toggleWindow(keywordSearchWin);
            });
            QObject::connect(toolbox, &Toolbox::showTagManagerRequested, [=, &tagMgrWin](){
                if (!tagMgrWin) {
                    tagMgrWin = new TagManagerWindow();
                    tagMgrWin->setObjectName("TagManagerWindow");
                }
                tagMgrWin->refreshData();
                toggleWindow(tagMgrWin);
            });
            QObject::connect(toolbox, &Toolbox::showFileStorageRequested, [=, &fileStorageWin, &mainWin, &quickWin](){
                if (!fileStorageWin) {
                    fileStorageWin = new FileStorageWindow();
                    fileStorageWin->setObjectName("FileStorageWindow");
                }
                int catId = -1;
                if (quickWin->isVisible()) catId = quickWin->getCurrentCategoryId();
                else if (mainWin && mainWin->isVisible()) catId = mainWin->getCurrentCategoryId();
                fileStorageWin->setCurrentCategory(catId);
                toggleWindow(fileStorageWin);
            });
            QObject::connect(toolbox, &Toolbox::showFileSearchRequested, [=, &fileSearchWin](){
                if (!fileSearchWin) {
                    fileSearchWin = new FileSearchWindow();
                    fileSearchWin->setObjectName("FileSearchWindow");
                }
                toggleWindow(fileSearchWin);
            });
            QObject::connect(toolbox, &Toolbox::showColorPickerRequested, [=, &colorPickerWin](){
                if (!colorPickerWin) {
                    colorPickerWin = new ColorPickerWindow();
                    colorPickerWin->setObjectName("ColorPickerWindow");
                }
                toggleWindow(colorPickerWin);
            });
            QObject::connect(toolbox, &Toolbox::startColorPickerRequested, [=, &colorPickerWin](){
                if (!colorPickerWin) {
                    colorPickerWin = new ColorPickerWindow();
                    colorPickerWin->setObjectName("ColorPickerWindow");
                }
                colorPickerWin->startScreenPicker();
            });
            QObject::connect(toolbox, &Toolbox::showHelpRequested, [=, &helpWin](){
                if (!helpWin) {
                    helpWin = new HelpWindow();
                    helpWin->setObjectName("HelpWindow");
                }
                toggleWindow(helpWin);
            });

            QObject::connect(toolbox, &Toolbox::showMainWindowRequested, [=](){ showMainWindow(); });
            QObject::connect(toolbox, &Toolbox::showQuickWindowRequested, [=](){ quickWin->showAuto(); });
            QObject::connect(toolbox, &Toolbox::screenshotRequested, [=](){ startScreenshot(); });
            QObject::connect(toolbox, &Toolbox::startOCRRequested, [=](){ startImmediateOCR(); });
        }
        return toolbox;
    };

    showMainWindow = [=, &mainWin, &checkLockAndExecute, &getToolbox, &fileStorageWin, &quickWin]() {
        checkLockAndExecute([=, &mainWin, &getToolbox, &fileStorageWin, &quickWin](){
            if (!mainWin) {
                mainWin = new MainWindow();
                QObject::connect(mainWin, &MainWindow::toolboxRequested, [=](){ toggleWindow(getToolbox(), mainWin); });
                QObject::connect(mainWin, &MainWindow::fileStorageRequested, [=, &mainWin, &fileStorageWin](){
                    if (!fileStorageWin) {
                        fileStorageWin = new FileStorageWindow();
                        fileStorageWin->setObjectName("FileStorageWindow");
                    }
                    fileStorageWin->setCurrentCategory(mainWin->getCurrentCategoryId());
                    toggleWindow(fileStorageWin, mainWin);
                });
            }
            mainWin->showNormal();
            mainWin->activateWindow();
            mainWin->raise();
        });
    };

    startImmediateOCR = [=, &checkLockAndExecute]() {
        static bool isScreenshotActive = false;
        if (isScreenshotActive) return;

        checkLockAndExecute([&](){
            isScreenshotActive = true;
            auto* tool = new ScreenshotTool();
            tool->setAttribute(Qt::WA_DeleteOnClose);
            tool->setImmediateOCRMode(true);
            
            QObject::connect(tool, &ScreenshotTool::destroyed, [=](){
                isScreenshotActive = false;
            });
            
            QObject::connect(tool, &ScreenshotTool::screenshotCaptured, [=](const QImage& img){
                QSettings settings("RapidNotes", "OCR");
                bool autoCopy = settings.value("autoCopy", false).toBool();
                static int immediateOcrIdCounter = 10000;
                int taskId = immediateOcrIdCounter++;
                auto* resWin = new OCRResultWindow(img, taskId);
                QObject::connect(&OCRManager::instance(), &OCRManager::recognitionFinished, 
                                 resWin, &OCRResultWindow::setRecognizedText);
                
                if (autoCopy) {
                    QToolTip::showText(QCursor::pos(), StringUtils::wrapToolTip("<b style='color: #3498db;'>⏳ 正在识别文字...</b>"), nullptr, {}, 1000);
                } else {
                    resWin->show();
                }
                OCRManager::instance().recognizeAsync(img, taskId);
            });
            tool->show();
        });
    };

    startScreenshot = [=, &checkLockAndExecute]() {
        static bool isNormalScreenshotActive = false;
        if (isNormalScreenshotActive) return;

        checkLockAndExecute([&](){
            isNormalScreenshotActive = true;
            auto* tool = new ScreenshotTool();
            tool->setAttribute(Qt::WA_DeleteOnClose);
            QObject::connect(tool, &ScreenshotTool::destroyed, [=](){ isNormalScreenshotActive = false; });
            QObject::connect(tool, &ScreenshotTool::screenshotCaptured, [=](const QImage& img){
                QApplication::clipboard()->setImage(img);
                QByteArray ba;
                QBuffer buffer(&ba);
                buffer.open(QIODevice::WriteOnly);
                img.save(&buffer, "PNG");
                QString title = "[截屏] " + QDateTime::currentDateTime().toString("MMdd_HHmm");
                int noteId = DatabaseManager::instance().addNote(title, "[正在进行文字识别...]", QStringList() << "截屏", "", -1, "image", ba);
                OCRManager::instance().recognizeAsync(img, noteId);
            });
            tool->show();
        });
    };

    QObject::connect(quickWin, &QuickWindow::toolboxRequested, [=, &getToolbox](){ toggleWindow(getToolbox(), quickWin); });
    QObject::connect(quickWin, &QuickWindow::toggleMainWindowRequested, [=, &showMainWindow](){ showMainWindow(); });

    // 5. 开启全局键盘钩子 (支持快捷键重映射)
    KeyboardHook::instance().start();

    // 6. 注册全局热键 (从配置加载)
    HotkeyManager::instance().reapplyHotkeys();
    
    QObject::connect(&HotkeyManager::instance(), &HotkeyManager::hotkeyPressed, [&](int id){
        if (id == 1) {
            if (quickWin->isVisible() && quickWin->isActiveWindow()) {
                quickWin->hide();
            } else {
                quickWin->showAuto();
            }
        } else if (id == 2) {
            checkLockAndExecute([&](){
                // 收藏最后一条灵感
                auto notes = DatabaseManager::instance().searchNotes("");
                if (!notes.isEmpty()) {
                    int lastId = notes.first()["id"].toInt();
                    DatabaseManager::instance().updateNoteState(lastId, "is_favorite", 1);
                    qDebug() << "[Main] 已收藏最新灵感 ID:" << lastId;
                }
            });
        } else if (id == 3) {
            startScreenshot();
        } else if (id == 4) {
            checkLockAndExecute([&](){
                // 全局采集：仅限浏览器 -> 清空剪贴板 -> 模拟 Ctrl+C -> 获取剪贴板 -> 智能拆分 -> 入库
#ifdef Q_OS_WIN
                if (!isBrowserActive()) {
                    qDebug() << "[Acquire] 当前非浏览器窗口，忽略采集指令。";
                    return;
                }

                // 1. 务必清空剪贴板，防止残留
                QApplication::clipboard()->clear();
                // 屏蔽监听器的下一次捕获，防止重复入库
                ClipboardMonitor::instance().skipNext();

                // 2. 模拟 Ctrl+C
                // 关键修复：由于热键是 Ctrl+Shift+S，此时物理 Shift 和 S 键很可能仍被按下。
                // 如果不显式释放 Shift，Ctrl+C 会变成 Ctrl+Shift+C (在浏览器中通常是打开开发者工具而非复制)。
                keybd_event(VK_SHIFT, 0, KEYEVENTF_KEYUP, 0);
                keybd_event('S', 0, KEYEVENTF_KEYUP, 0);

                keybd_event(VK_CONTROL, 0, 0, 0);
                keybd_event('C', 0, 0, 0);
                keybd_event('C', 0, KEYEVENTF_KEYUP, 0);
                // 这里不要立即抬起 Control，因为抬起太快可能导致目标窗口还没来得及接收到组合键
#endif
                // 增加延迟至 300ms，为浏览器处理复制请求提供更充裕的时间
                QTimer::singleShot(300, [=](){
                    // 此时再彻底释放 Ctrl (可选，防止干扰后续操作)
#ifdef Q_OS_WIN
                    keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
#endif
                    QString text = QApplication::clipboard()->text();
                    if (text.trimmed().isEmpty()) {
                        qWarning() << "[Acquire] 剪贴板为空，采集失败。";
                        QToolTip::showText(QCursor::pos(), StringUtils::wrapToolTip("<b style='color:#e74c3c;'>✖ 未能采集到内容，请确保已选中浏览器中的文本</b>"), nullptr, {}, 2000);
                        return;
                    }

                    auto pairs = StringUtils::smartSplitPairs(text);
                    if (pairs.isEmpty()) return;

                    int catId = -1;
                    if (quickWin && quickWin->isVisible()) {
                        catId = quickWin->getCurrentCategoryId();
                    }

                    for (const auto& pair : std::as_const(pairs)) {
                        DatabaseManager::instance().addNoteAsync(pair.first, pair.second, {"采集"}, "", catId, "text");
                    }
                    
                    // 成功反馈 (ToolTip)
                    QString feedback = pairs.size() > 1 
                        ? QString("✔ 已批量采集 %1 条灵感").arg(pairs.size())
                        : "✔ 已采集灵感: " + (pairs[0].first.length() > 20 ? pairs[0].first.left(17) + "..." : pairs[0].first);

                    QToolTip::showText(QCursor::pos(), StringUtils::wrapToolTip(QString("<b style='color: #2ecc71;'>%1</b>").arg(feedback)), 
                        nullptr, {}, 2000);
                });
            });
        } else if (id == 5) {
            // 全局锁定
            quickWin->doGlobalLock();
        } else if (id == 6) {
            // 文字识别
            startImmediateOCR();
        }
    });

    // 监听 OCR 完成信号并更新笔记内容 (排除工具箱特有的立即识别 ID)
    // 必须指定 context 对象 (&DatabaseManager::instance()) 确保回调在正确的线程执行
    QObject::connect(&OCRManager::instance(), &OCRManager::recognitionFinished, &DatabaseManager::instance(), [](const QString& text, int noteId){
        if (noteId > 0 && noteId < 10000) {
            DatabaseManager::instance().updateNoteState(noteId, "content", text);
        }
    });

    // 7. 系统托盘
    QObject::connect(&server, &QLocalServer::newConnection, [&](){
        QLocalSocket* conn = server.nextPendingConnection();
        if (conn->waitForReadyRead(500)) {
            QByteArray data = conn->readAll();
            if (data == "SHOW") {
                quickWin->showAuto();
            }
            conn->disconnectFromServer();
        }
    });

    SystemTray* tray = new SystemTray(&a);
    QObject::connect(tray, &SystemTray::showMainWindow, showMainWindow);
    QObject::connect(tray, &SystemTray::showQuickWindow, quickWin, &QuickWindow::showAuto);
    
    // 初始化托盘菜单中悬浮球的状态
    tray->updateBallAction(ball->isVisible());
    QObject::connect(tray, &SystemTray::toggleFloatingBall, [=](bool visible){
        if (visible) ball->show();
        else ball->hide();
        ball->savePosition(); // 立即记忆状态
        tray->updateBallAction(visible);
    });

    QObject::connect(tray, &SystemTray::showHelpRequested, [=, &helpWin](){
        if (!helpWin) {
            helpWin = new HelpWindow();
            helpWin->setObjectName("HelpWindow");
        }
        toggleWindow(helpWin);
    });
    QObject::connect(tray, &SystemTray::showSettings, [=](){
        static QPointer<SettingsWindow> settingsWin;
        if (settingsWin) {
            settingsWin->showNormal();
            settingsWin->raise();
            settingsWin->activateWindow();
            return;
        }

        settingsWin = new SettingsWindow();
        settingsWin->setObjectName("SettingsWindow");
        settingsWin->setAttribute(Qt::WA_DeleteOnClose);

        // 核心修复：先计算位置并移动，确保窗口 show() 的那一刻就在正确的位置，杜绝闪烁
        QScreen *screen = QGuiApplication::primaryScreen();
        if (screen) {
            QRect screenGeom = screen->geometry();
            settingsWin->move(screenGeom.center() - settingsWin->rect().center());
        }

        settingsWin->show();
        settingsWin->raise();
        settingsWin->activateWindow();
    });
    QObject::connect(tray, &SystemTray::quitApp, &a, &QApplication::quit);
    tray->show();

    QObject::connect(ball, &FloatingBall::doubleClicked, [&](){
        quickWin->showAuto();
    });
    QObject::connect(ball, &FloatingBall::requestMainWindow, showMainWindow);
    QObject::connect(ball, &FloatingBall::requestQuickWindow, quickWin, &QuickWindow::showAuto);
    QObject::connect(ball, &FloatingBall::requestToolbox, [=, &getToolbox](){
        checkLockAndExecute([=, &getToolbox](){ toggleWindow(getToolbox()); });
    });
    QObject::connect(ball, &FloatingBall::requestNewIdea, [=](){
        checkLockAndExecute([=](){
            NoteEditWindow* win = new NoteEditWindow();
            QObject::connect(win, &NoteEditWindow::noteSaved, quickWin, &QuickWindow::refreshData);
            win->show();
        });
    });

    // 8. 监听剪贴板 (智能标题与自动分类)
    QObject::connect(&ClipboardMonitor::instance(), &ClipboardMonitor::clipboardChanged, [=](){
        // 触发烟花爆炸特效
        FireworksOverlay::instance()->explode(QCursor::pos());
    });

    QObject::connect(&ClipboardMonitor::instance(), &ClipboardMonitor::newContentDetected, 
        [=](const QString& content, const QString& type, const QByteArray& data,
            const QString& sourceApp, const QString& sourceTitle){
        qDebug() << "[Main] 接收到剪贴板信号:" << type << "来自:" << sourceApp;
        
        QString title;
        if (type == "image") {
            title = "[图片] " + QDateTime::currentDateTime().toString("MMdd_HHmm");
        } else if (type == "file") {
            QStringList files = content.split(";", Qt::SkipEmptyParts);
            if (!files.isEmpty()) {
                QString firstFileName = QFileInfo(files.first()).fileName();
                if (files.size() > 1) title = QString("[多文件] %1 等%2个文件").arg(firstFileName).arg(files.size());
                else title = "[文件] " + firstFileName;
            } else {
                title = "[未知文件]";
            }
        } else {
            // 文本：取第一行
            QString firstLine = content.section('\n', 0, 0).trimmed();
            if (firstLine.isEmpty()) title = "无标题灵感";
            else {
                title = firstLine.left(40);
                if (firstLine.length() > 40) title += "...";
            }
        }

        // 自动归档逻辑
        int catId = -1;
        if (quickWin && quickWin->isAutoCategorizeEnabled()) {
            catId = quickWin->getCurrentCategoryId();
        }

        // 自动生成类型标签与类型修正 (解耦逻辑)
        QStringList tags;
        QString finalType = type;
        
        if (type == "text") {
            QString trimmed = content.trimmed();
            if (trimmed.startsWith("http://") || trimmed.startsWith("https://") || trimmed.startsWith("www.")) {
                finalType = "link";
                tags << "链接" << "网址";

                // 提取二级域名作为标题和标签 (例如: https://www.google.com -> Google)
                QUrl url(trimmed.startsWith("www.") ? "http://" + trimmed : trimmed);
                QString host = url.host();
                if (host.startsWith("www.")) host = host.mid(4);
                QStringList hostParts = host.split('.');
                if (hostParts.size() >= 2) {
                    // 通常取倒数第二部分 (如 google.com -> google)
                    // 简单处理: 排除一些常见的二级后缀 (可选，这里先实现基础逻辑)
                    QString sld = hostParts[hostParts.size() - 2];
                    if (!sld.isEmpty()) {
                        sld[0] = sld[0].toUpper();
                        title = sld;
                        if (!tags.contains(sld)) tags << sld;
                    }
                }
            }
        }
        
        DatabaseManager::instance().addNoteAsync(title, content, tags, "", catId, finalType, data, sourceApp, sourceTitle);
    });

    int result = a.exec();
    
    // 退出前合壳并加密数据库
    DatabaseManager::instance().closeAndPack();
    
    return result;
}