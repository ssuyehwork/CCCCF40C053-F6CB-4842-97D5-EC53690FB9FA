#include <QSettings>
#include <QApplication>
#include <QColor>
#include <QThread>
#include <QFile>
#include <QCursor>
#include <QMessageBox>
#include <QCoreApplication>
#include <QDir>
#include <QDebug>
#include <QDateTime>
#include <QFileInfo>
#include <QBuffer>
#include <QUrl>
#include <QTimer>
#include <QThreadPool>
#include <QElapsedTimer>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>
#include <QRegularExpression>
#include <QKeyEvent>
#include <QLineEdit>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QTextCursor>
#include <QTextDocument>
#include <functional>
#include <utility>
#include "core/DatabaseManager.h"
#include "core/HotkeyManager.h"
#include "core/ClipboardMonitor.h"
#include "core/OCRManager.h"
#include "ui/MainWindow.h"
#include "ui/FloatingBall.h"
#include "ui/QuickWindow.h"
#include "ui/SystemTray.h"
#include "ui/Toolbox.h"

#include <QAbstractItemView>
#include <QHelpEvent>
#include <QModelIndex>

/**
 * @brief [REMOVED] 全局拦截器已移除。
 * 过往版本中的 GlobalInputKeyFilter 和 GlobalToolTipFilter 存在严重的交互干扰：
 * 1. 强制重定义 QLineEdit 的上下键，导致具有历史记录功能的输入框导航失效。
 * 2. 强制接管原生 ToolTip，在复杂多屏环境下可能导致弹出位置异常。
 * 现已改由各组件按需实现。


#include "ui/TimePasteWindow.h"
#include "ui/PasswordGeneratorWindow.h"
#include "ui/OCRWindow.h"
#include "ui/OCRResultWindow.h"
#include "ui/TagManagerWindow.h"
#include "ui/SearchAppWindow.h"
#include "ui/ColorPickerWindow.h"
#include "ui/PixelRulerOverlay.h"
#include "ui/HelpWindow.h"
#include "ui/FireworksOverlay.h"
#include "ui/ScreenshotTool.h"
#include "ui/SettingsWindow.h"
#include "ui/ActivationDialog.h"
#include "ui/TodoCalendarWindow.h"
#include "ui/ToolTipOverlay.h"
#include "ui/StringUtils.h"
#include "core/KeyboardHook.h"
#include "core/MessageCaptureHandler.h"
#include "core/ReminderService.h"
#include "core/FileCryptoHelper.h"
#include "core/FileStorageHelper.h"
#include "core/HttpServer.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
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
    /* qDebug() << "[Main] 数据库外壳路径:" << dbPath; */

    if (!DatabaseManager::instance().init(dbPath)) {
        // 2026-03-15 [UI-FIX] 启动失败时显示更具体的原因。
        // 鉴于目前出现启动死锁，改用 QMessageBox 这种模态窗口。
        // 模态窗口有自己的事件循环，能确保在进程终结前把错误文本完整渲染给用户。
        QString reason = DatabaseManager::instance().getLastError();
        if (reason.isEmpty()) reason = "无法加载加密外壳、解密失败或数据库损坏。";
        
        QMessageBox::critical(nullptr, "启动失败 (RapidNotes)", 
            QString("<b>程序初始化遭遇异常，无法继续：</b><br><br>%1<br><br>建议尝试删除 data 目录下的 kernel 文件后重试。").arg(reason));
            
        return -1;
    }

    // 1.0.5 启动 HTTP 服务，支持浏览器插件联动
    HttpServer::instance().start(23333);

    // [ARCH-CLEANUP] 定义统一的程序退出流
    auto doSafeExit = [&]() {
        static bool isExiting = false;
        if (isExiting) return;
        isExiting = true;

        // 2026-03-15 核心优化：视觉先行。先关闭/隐藏所有窗口，再执行耗时的合壳操作
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            if (widget && widget->objectName() != "ToolTipOverlay") {
                widget->hide();
            }
        }

        QScreen *screen = QGuiApplication::primaryScreen();
        if (screen) {
            QPoint center = screen->geometry().center();
            ToolTipOverlay::instance()->showText(center, 
                "<b style='color: #2ecc71; font-size: 16px;'>🚀 程序正在退出...</b>", 0);
            // 增加刷新时长，确保所有 hide 事件及 Tip 绘制在合壳卡顿前完成
            qApp->processEvents(QEventLoop::AllEvents, 300);
        }
        
        DatabaseManager::instance().closeAndPack();
        QApplication::quit();
    };

    // 1.1 2026-03-xx 按照用户要求：正版授权强制校验逻辑
    QVariantMap trialStatus = DatabaseManager::instance().getTrialStatus();

    // [CRITICAL] 跨设备一致性检查：如果指纹不匹配（解密失败），视为非法拷贝运行，直接拦截退出
    if (trialStatus["fingerprint_mismatch"].toBool()) {
        // 2026-03-xx 按照用户要求：检测到硬件指纹不匹配时，弹出告知并强制重置激活状态后退出。
        QMessageBox::critical(nullptr, "系统提示", "<b>[安全拦截] 检测到硬件指纹不匹配。</b><br><br>由于当前设备的硬件指纹与授权记录不符，系统已自动重置本地激活状态以确保证版授权安全。<br><br>请联系管理员获取适用于当前新设备的专属授权码，并重新进行激活。程序将立即退出。");
        return 0;
    }

    // 强制激活流：未激活状态下必须通过 ActivationDialog 验证，否则不允许进入主程序
    if (!trialStatus["is_activated"].toBool()) {
        QString reason = "<b>欢迎使用 RapidNotes 正版软件</b><br><br>检测到当前设备尚未激活，请输入您的专属授权密钥以继续：";
            
        ActivationDialog dlg(reason);
        if (dlg.exec() != QDialog::Accepted) {
            doSafeExit();
            return 0; 
        }
        // 验证成功后，重新同步最新的授权状态
        trialStatus = DatabaseManager::instance().getTrialStatus();
    }

    // 2. 初始化核心 UI 组件 (快速笔记窗口与悬浮球)
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
    SearchAppWindow* searchWin = nullptr;
    TagManagerWindow* tagMgrWin = nullptr;
    ColorPickerWindow* colorPickerWin = nullptr;
    HelpWindow* helpWin = nullptr;
    TodoCalendarWindow* todoWin = nullptr;

    // [WINDOW_MANAGER_PRE] 临时内部类，未来可迁移至独立文件
    struct WindowManager {
        static void toggle(QWidget* win, QWidget* parentWin = nullptr) {
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
    std::function<void(bool)> startCapture; // 合并后的截图/OCR 函数

    auto getToolbox = [&]() -> Toolbox* {
        if (!toolbox) {
            toolbox = new Toolbox();
            toolbox->setObjectName("ToolboxLauncher");

            // 2026-03-22 [NEW] 同步工具箱可见性状态到 QuickWindow
            QObject::connect(toolbox, &Toolbox::visibilityChanged, quickWin, &QuickWindow::updateToolboxStatus);
            // 如果 MainWindow 已存在，也同步过去
            if (mainWin) {
                QObject::connect(toolbox, &Toolbox::visibilityChanged, mainWin, &MainWindow::updateToolboxStatus);
                mainWin->updateToolboxStatus(toolbox->isVisible());
            }
            // 初始状态同步
            quickWin->updateToolboxStatus(toolbox->isVisible());
            
            QObject::connect(toolbox, &Toolbox::showTimePasteRequested, [=, &timePasteWin](){
                if (!timePasteWin) {
                    timePasteWin = new TimePasteWindow();
                    timePasteWin->setObjectName("TimePasteWindow");
                }
                WindowManager::toggle(timePasteWin);
            });
            QObject::connect(toolbox, &Toolbox::showPasswordGeneratorRequested, [=, &passwordGenWin](){
                if (!passwordGenWin) {
                    passwordGenWin = new PasswordGeneratorWindow();
                    passwordGenWin->setObjectName("PasswordGeneratorWindow");
                }
                WindowManager::toggle(passwordGenWin);
            });
            QObject::connect(toolbox, &Toolbox::showOCRRequested, [=, &ocrWin](){
                if (!ocrWin) {
                    ocrWin = new OCRWindow();
                    ocrWin->setObjectName("OCRWindow");
                }
                WindowManager::toggle(ocrWin);
            });
            QObject::connect(toolbox, &Toolbox::showKeywordSearchRequested, [=, &searchWin](){
                if (!searchWin) {
                    searchWin = new SearchAppWindow();
                }
                searchWin->switchToKeywordSearch();
                WindowManager::toggle(searchWin);
            });
            QObject::connect(toolbox, &Toolbox::showTagManagerRequested, [=, &tagMgrWin](){
                if (!tagMgrWin) {
                    tagMgrWin = new TagManagerWindow();
                    tagMgrWin->setObjectName("TagManagerWindow");
                }
                tagMgrWin->refreshData();
                WindowManager::toggle(tagMgrWin);
            });
            QObject::connect(toolbox, &Toolbox::showFileSearchRequested, [=, &searchWin](){
                if (!searchWin) {
                    searchWin = new SearchAppWindow();
                }
                searchWin->switchToFileSearch();
                WindowManager::toggle(searchWin);
            });
            QObject::connect(toolbox, &Toolbox::showColorPickerRequested, [=, &colorPickerWin](){
                if (!colorPickerWin) {
                    colorPickerWin = new ColorPickerWindow();
                    colorPickerWin->setObjectName("ColorPickerWindow");
                }
                WindowManager::toggle(colorPickerWin);
            });
            QObject::connect(toolbox, &Toolbox::startColorPickerRequested, [=, &colorPickerWin](){
                if (!colorPickerWin) {
                    colorPickerWin = new ColorPickerWindow();
                    colorPickerWin->setObjectName("ColorPickerWindow");
                }
                colorPickerWin->startScreenPicker();
            });
            QObject::connect(toolbox, &Toolbox::showPixelRulerRequested, [](){
                // 2026-03-xx 核心修复：全局单例保护检查
                for (QWidget* top : QApplication::topLevelWidgets()) {
                    if (top->objectName() == "PixelRulerOverlay") return;
                }
                auto* ruler = new PixelRulerOverlay(nullptr);
                ruler->setAttribute(Qt::WA_DeleteOnClose);
                ruler->show();
            });
            QObject::connect(toolbox, &Toolbox::showHelpRequested, [=, &helpWin](){
                if (!helpWin) {
                    helpWin = new HelpWindow();
                    helpWin->setObjectName("HelpWindow");
                }
                WindowManager::toggle(helpWin);
            });
            QObject::connect(toolbox, &Toolbox::showTodoCalendarRequested, [=, &todoWin](){
                if (!todoWin) {
                    todoWin = new TodoCalendarWindow();
                    todoWin->setObjectName("TodoCalendarWindow");
                }
                WindowManager::toggle(todoWin);
            });
            QObject::connect(toolbox, &Toolbox::showAlarmRequested, [=, &todoWin](){
                DatabaseManager::Todo t;
                t.title = "新闹钟";
                t.reminderTime = QDateTime::currentDateTime().addSecs(60);
                t.repeatMode = 1; // 默认每天重复
                
                // [ARCH-RECONSTRUCT] 使用独立的 AlarmEditDialog 替代 TodoEditDialog
                QWidget* parent = (todoWin && todoWin->isVisible()) ? todoWin : nullptr;
                auto* dlg = new AlarmEditDialog(t, parent);
                dlg->setAttribute(Qt::WA_DeleteOnClose);
                
                if (!parent) {
                    QScreen *screen = QGuiApplication::primaryScreen();
                    if (screen) {
                        dlg->move(screen->availableGeometry().center() - QPoint(200, 150));
                    }
                }
                
                QObject::connect(dlg, &QDialog::accepted, [dlg, &todoWin](){
                    DatabaseManager::instance().addTodo(dlg->getTodo());
                    if (todoWin) {
                        QMetaObject::invokeMethod(todoWin, "refreshTodos", Qt::QueuedConnection);
                    }
                });
                
                dlg->show();
                dlg->raise();
                dlg->activateWindow();
            });

            QObject::connect(toolbox, &Toolbox::showMainWindowRequested, [=](){ showMainWindow(); });
            QObject::connect(toolbox, &Toolbox::showQuickWindowRequested, [=](){ quickWin->showAuto(); });
            QObject::connect(toolbox, &Toolbox::newNoteRequested, [=](){
                NoteEditWindow* win = new NoteEditWindow();
                QObject::connect(win, &NoteEditWindow::noteSaved, quickWin, &QuickWindow::refreshData);
                win->show();
            });
            QObject::connect(toolbox, &Toolbox::screenshotRequested, [=](){ startCapture(false); });
            QObject::connect(toolbox, &Toolbox::startOCRRequested, [=](){ startCapture(true); });
        }
        return toolbox;
    };

    showMainWindow = [=, &mainWin, &checkLockAndExecute, &getToolbox, &quickWin, &toolbox]() {
        checkLockAndExecute([=, &mainWin, &getToolbox, &quickWin, &toolbox](){
            if (!mainWin) {
                mainWin = new MainWindow();
                QObject::connect(mainWin, &MainWindow::toolboxRequested, [=](){ WindowManager::toggle(getToolbox(), mainWin); });

                // 2026-03-22 [NEW] 如果工具箱已存在，同步信号到新创建的 MainWindow
                if (toolbox) {
                    QObject::connect(toolbox, &Toolbox::visibilityChanged, mainWin, &MainWindow::updateToolboxStatus);
                    mainWin->updateToolboxStatus(toolbox->isVisible());
                }
            }
            mainWin->showNormal();
            mainWin->activateWindow();
            mainWin->raise();
        });
    };

    startCapture = [=, &checkLockAndExecute](bool immediateOCR) {
        static bool isCaptureActive = false;
        if (isCaptureActive) return;

        checkLockAndExecute([&](){
            isCaptureActive = true;
            auto* tool = new ScreenshotTool();
            tool->setAttribute(Qt::WA_DeleteOnClose);
            if (immediateOCR) tool->setImmediateOCRMode(true);
            
            QObject::connect(tool, &ScreenshotTool::destroyed, [=](){ isCaptureActive = false; });
            
            QObject::connect(tool, &ScreenshotTool::screenshotCaptured, [=](const QImage& img, bool isOcrRequest){
                if (!isOcrRequest) {
                    ClipboardMonitor::instance().skipNext();
                    QApplication::clipboard()->setImage(img);
                }
                
                QByteArray ba;
                QBuffer buffer(&ba);
                buffer.open(QIODevice::WriteOnly);
                img.save(&buffer, "PNG");
                
                QString title = (isOcrRequest ? "[截图取文] " : "[截图] ") + QDateTime::currentDateTime().toString("MMdd_HHmm");
                QStringList tags = isOcrRequest ? (QStringList() << "截图" << "截图取文") : (QStringList() << "截图");
                QString initialContent = isOcrRequest ? "[正在进行文字识别...]" : "";
                // 如果是直接 OCR 模式，类型设为 ocr_text
                QString itemType = immediateOCR ? "ocr_text" : "image";

                int noteId = DatabaseManager::instance().addNote(title, initialContent, tags, "", -1, itemType, ba);
                
                if (isOcrRequest) {
                    QVariantMap existing = DatabaseManager::instance().getNoteById(noteId);
                    QString currentContent = existing.value("content").toString();
                    
                    QSettings settings("RapidNotes", "OCR");
                    bool autoCopy = settings.value("autoCopy", false).toBool();
                    bool silent = settings.value("silentCapture", false).toBool();

                    // 优化：如果该图已有识别结果，直接复用而不重复触发 OCR
                    if (!currentContent.isEmpty() && currentContent != initialContent) {
                        if (!autoCopy) {
                            auto* resWin = new OCRResultWindow(img, noteId);
                            resWin->setRecognizedText(currentContent, noteId);
                            resWin->show();
                        } else {
                            QApplication::clipboard()->setText(currentContent);
                            ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #2ecc71;'>[OK] 已从库中恢复识别结果并复制</b>");
                        }
                        return;
                    }

                    auto* resWin = new OCRResultWindow(img, noteId);
                    QObject::connect(&OCRManager::instance(), &OCRManager::recognitionFinished, 
                                     resWin, &OCRResultWindow::setRecognizedText);
                    
                    if (autoCopy || silent) {
                        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #3498db;'>[OCR] 正在识别文字...</b>");
                    } else {
                        resWin->show();
                    }
                    OCRManager::instance().recognizeAsync(img, noteId);
                }
            });
            tool->show();
        });
    };

    // [USER_REQUEST] 定义可复用的采集逻辑
    auto doAcquire = [=, &checkLockAndExecute, &quickWin]() {
        checkLockAndExecute([&](){
            /* qDebug() << "[Acquire] 触发采集流程，开始环境检测..."; */
#ifdef Q_OS_WIN
            // [USER_REQUEST] 核心修复：支持从 UI 按钮点击触发的采集。
            // 如果是通过点击 UI 按钮触发，当前活跃窗口是 RapidNotes。
            // 我们必须检测记录的 m_lastActiveHwnd 是否为浏览器，并先切回该窗口。
            HWND target = GetForegroundWindow();
            bool isFromUI = (target == (HWND)quickWin->winId());
            
            if (isFromUI) {
                target = quickWin->m_lastActiveHwnd;
                if (target && IsWindow(target)) {
                    if (IsIconic(target)) ShowWindow(target, SW_RESTORE);
                    SetForegroundWindow(target);
                    // 留出时间让窗口激活
                    QThread::msleep(100); 
                }
            }

            if (!StringUtils::isBrowserWindow(target)) {
                /* qDebug() << "[Acquire] 拒绝执行：目标窗口非浏览器环境。"; */
                ToolTipOverlay::instance()->showText(QCursor::pos(), "✖ 智能采集仅支持浏览器环境");
                return;
            }

            ClipboardMonitor::instance().setIgnore(true);
            QApplication::clipboard()->clear();

            // 如果是通过热键触发，需要释放可能按下的 S 键以防干扰
            keybd_event('S', 0, KEYEVENTF_KEYUP, 0); 

            keybd_event(VK_CONTROL, 0, 0, 0);
            keybd_event('C', 0, 0, 0);
            keybd_event('C', 0, KEYEVENTF_KEYUP, 0);
            // 这里不立即抬起 Ctrl，因为某些应用接收消息较慢
#endif
            QTimer::singleShot(500, [=](){
#ifdef Q_OS_WIN
                keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
#endif
                QString text = QApplication::clipboard()->text();
                ClipboardMonitor::instance().setIgnore(false);
                if (text.trimmed().isEmpty()) {
                    ToolTipOverlay::instance()->showText(QCursor::pos(), "✖ 未能采集到内容，请确保已选中浏览器中的文本");
                    return;
                }
                auto pairs = StringUtils::smartSplitPairs(text);
                if (pairs.isEmpty()) return;
                int catId = quickWin->getCurrentCategoryId();
                for (const auto& pair : std::as_const(pairs)) {
                    QStringList tags = {"采集"};
                    if (StringUtils::containsThai(pair.first) || StringUtils::containsThai(pair.second)) tags << "泰文";
                    DatabaseManager::instance().addNoteAsync(pair.first, pair.second, tags, "", catId, "text");
                }
                ToolTipOverlay::instance()->showText(QCursor::pos(), QString("[OK] 已智能采集 %1 条灵感").arg(pairs.size()));
            });
        });
    };

    // [USER_REQUEST] 定义可复用的纯净粘贴逻辑
    auto doPurePaste = [=, &quickWin]() {
        QString text = QApplication::clipboard()->text();
        if (!text.isEmpty()) {
            ClipboardMonitor::instance().skipNext();
            QApplication::clipboard()->setText(text);
#ifdef Q_OS_WIN
            // [USER_REQUEST] 核心修复：点击 UI 按钮粘贴时，必须切回先前活跃的目标窗口
            HWND target = GetForegroundWindow();
            if (target == (HWND)quickWin->winId()) {
                target = quickWin->m_lastActiveHwnd;
                if (target && IsWindow(target)) {
                    if (IsIconic(target)) ShowWindow(target, SW_RESTORE);
                    SetForegroundWindow(target);
                    QThread::msleep(200); // 粘贴需要更稳定的激活状态
                }
            }

            INPUT inputs[6];
            memset(inputs, 0, sizeof(inputs));
            // 确保用户的 Shift 已抬起 (针对 Ctrl+Shift+V 热键)
            inputs[0].type = INPUT_KEYBOARD; inputs[0].ki.wVk = VK_SHIFT; inputs[0].ki.dwFlags = KEYEVENTF_KEYUP;
            // 模拟 Ctrl+V
            inputs[1].type = INPUT_KEYBOARD; inputs[1].ki.wVk = VK_CONTROL;
            inputs[2].type = INPUT_KEYBOARD; inputs[2].ki.wVk = 'V';
            inputs[3].type = INPUT_KEYBOARD; inputs[3].ki.wVk = 'V'; inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
            inputs[4].type = INPUT_KEYBOARD; inputs[4].ki.wVk = VK_CONTROL; inputs[4].ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(5, inputs, sizeof(INPUT));
#endif
            ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #2ecc71;'>[OK] 已纯净粘贴文本</b>");
        }
    };

    QObject::connect(quickWin, &QuickWindow::toolboxRequested, [=, &getToolbox](){ WindowManager::toggle(getToolbox(), quickWin); });
    QObject::connect(quickWin, &QuickWindow::toggleMainWindowRequested, [=, &showMainWindow](){ showMainWindow(); });
    // 2026-03-xx 按照用户要求，移除已被废弃的恶意信号连接逻辑 (screenshot/acquire/purePaste)

    // 5. 开启全局键盘钩子 (支持快捷键重映射)
    KeyboardHook::instance().start();
    MessageCaptureHandler::instance().init();

    // 6. 注册全局热键 (从配置加载)
    HotkeyManager::instance().reapplyHotkeys();

    // [NEW] 启动提醒服务
    ReminderService::instance().start();
    QObject::connect(&ReminderService::instance(), &ReminderService::todoReminderTriggered, [&](const DatabaseManager::Todo& todo){
        auto* dlg = new TodoReminderDialog(todo);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->setWindowFlags(dlg->windowFlags() | Qt::WindowStaysOnTopHint);
        
        QObject::connect(dlg, &TodoReminderDialog::snoozeRequested, [todo](int minutes){
            DatabaseManager::Todo updatedTodo = todo;
            updatedTodo.reminderTime = QDateTime::currentDateTime().addSecs(minutes * 60);
            DatabaseManager::instance().updateTodo(updatedTodo);
            ReminderService::instance().removeNotifiedId(todo.id); // 允许再次提醒
        });
        
        dlg->show();
        dlg->raise();
        dlg->activateWindow();
    });
    
    // 初始化通用设置 (回车捕获)
    QSettings generalSettings("RapidNotes", "General");
    KeyboardHook::instance().setEnterCaptureEnabled(generalSettings.value("enterCapture", false).toBool());

    // [USER_REQUEST] 响应来自底层钩子的强制锁定请求，解决热键冲突问题
    QObject::connect(&KeyboardHook::instance(), &KeyboardHook::globalLockRequested, [&](){
        quickWin->doGlobalLock();
    });
    
    QObject::connect(&HotkeyManager::instance(), &HotkeyManager::hotkeyPressed, [&](int id){
        if (id == 1) {
            if (quickWin->isVisible() && quickWin->isActiveWindow()) {
                quickWin->hide();
            } else {
                // [USER_REQUEST] 热键唤起前，立即捕获当前活动窗口。
                // 这在窗口已显示但未激活，且用户通过热键再次触发时尤为关键，能确保 m_lastActiveHwnd 始终指向“真正的”外部目标。
                quickWin->recordLastActiveWindow(nullptr);
                quickWin->showAuto();
            }
        } else if (id == 2) {
            checkLockAndExecute([&](){
                // [USER_REQUEST] 核心修复：改用物理 ID 绝对定位，收藏“绝对最后创建”的那条数据
                int lastId = DatabaseManager::instance().getLastCreatedNoteId();
                if (lastId > 0) {
                    DatabaseManager::instance().updateNoteState(lastId, "is_favorite", 1);
                    /* qDebug() << "[Main] 已成功执行 Ctrl+Shift+E 一键收藏 -> ID:" << lastId; */
                    
                    // 2026-03-xx 按照项目规范，提供视觉反馈
                    ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #F2B705;'>★ 已收藏最后一条灵感</b>");
                } else {
                    ToolTipOverlay::instance()->showText(QCursor::pos(), "✖ 未发现可收藏的灵感");
                }
            });
        } else if (id == 3) {
            startCapture(false);
        } else if (id == 4) {
            doAcquire();
        } else if (id == 5) {
            // 全局锁定
            quickWin->doGlobalLock();
        } else if (id == 6) {
            // 截图取文
            startCapture(true);
        } else if (id == 7) {
            doPurePaste();
        } else if (id == 8) {
            // 用户要求：全局呼出工具箱
            WindowManager::toggle(getToolbox());
        } else if (id == 9) {
            // 2026-03-20 [NEW] 全局 Alt+A 连击菜单
            quickWin->showContextNotesMenu();
        }
    });

    // 监听 OCR 完成信号并更新笔记内容
    // 必须指定 context 对象 (&DatabaseManager::instance()) 确保回调在正确的线程执行
    QObject::connect(&OCRManager::instance(), &OCRManager::recognitionFinished, &DatabaseManager::instance(), [](const QString& text, int noteId){
        if (noteId > 0) {
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
    QObject::connect(tray, &SystemTray::showQuickWindow, [=](){
        quickWin->recordLastActiveWindow(nullptr);
        quickWin->showAuto();
    });
    QObject::connect(tray, &SystemTray::showTodoCalendar, [=, &todoWin](){
        if (!todoWin) {
            todoWin = new TodoCalendarWindow();
            todoWin->setObjectName("TodoCalendarWindow");
        }
        WindowManager::toggle(todoWin);
    });
    
    // 初始化托盘菜单中悬浮球的状态
    tray->updateBallAction(ball->isVisible());
    QObject::connect(tray, &SystemTray::toggleFloatingBall, [=](bool visible){
        if (visible) ball->show();
        else ball->hide();
        ball->savePosition(); // 立即记忆状态
        tray->updateBallAction(visible);
    });

    QObject::connect(tray, &SystemTray::showHelpRequested, [=, &helpWin](){
        checkLockAndExecute([=, &helpWin](){
            if (!helpWin) {
                helpWin = new HelpWindow();
                helpWin->setObjectName("HelpWindow");
            }
            WindowManager::toggle(helpWin);
        });
    });
    QObject::connect(tray, &SystemTray::showSettings, [=](){
        checkLockAndExecute([=](){
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
    });
    QObject::connect(tray, &SystemTray::quitApp, doSafeExit);
    tray->show();

    QObject::connect(ball, &FloatingBall::doubleClicked, [&](){
        quickWin->recordLastActiveWindow(nullptr);
        quickWin->showAuto();
    });
    QObject::connect(ball, &FloatingBall::requestMainWindow, showMainWindow);
    QObject::connect(ball, &FloatingBall::requestQuickWindow, [=](){
        quickWin->recordLastActiveWindow(nullptr);
        quickWin->showAuto();
    });
    QObject::connect(ball, &FloatingBall::requestToolbox, [=, &getToolbox](){
        checkLockAndExecute([=, &getToolbox](){ WindowManager::toggle(getToolbox()); });
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
        // [DIAG] 追踪烟花特效耗时
        QElapsedTimer fw;
        fw.start();
        // 触发烟花爆炸特效
        FireworksOverlay::instance()->explode(QCursor::pos());
        /* qDebug() << "[Clipboard->ToolTip DIAG] FireworksOverlay::explode 耗时 =" << fw.elapsed() << "ms"; */
    });

    // [REPAIR] 2026-03-xx 核心修复：主线程解放方案
    // ToolTip 显示后紧随的大量同步逻辑（字符串处理、异步 DB 排队等）会阻塞事件循环，
    // 导致计时器信号无法准时派发。我们将重处理逻辑推入 singleShot(0)，确保事件循环立刻回转。
    QObject::connect(&ClipboardMonitor::instance(), &ClipboardMonitor::newContentDetected, 
        [quickWin](const QString& content, const QString& type, const QByteArray& data,
            const QString& sourceApp, const QString& sourceTitle){
        
        static bool s_isShowingCopyTip = false;
        if (s_isShowingCopyTip) return;

        // [DIAG] 诊断计时器：追踪主线程各阶段耗时
        QElapsedTimer diagClock;
        diagClock.start();
        /* qDebug() << "[Clipboard->ToolTip DIAG] ===== 信号入口 ====="; */

        // ✅ 第一步：【绝对优先】先处理 ToolTip，不夹杂任何其他准备逻辑
        // 我们通过直接构造 QSettings 耗时约 0~1ms，在此之后立即获取鼠标并显示 ToolTip
        QSettings gs("RapidNotes", "General");
        bool showTip = gs.value("showCopyToolTip", false).toBool();
        QPoint tipPos = QCursor::pos(); // 立即捕获鼠标位置

        if (showTip) {
            if (content.trimmed().isEmpty() && type.isEmpty()) {
                // 静默处理（识别失败等空情况）
            } else {
                QString displayContent;
                if (!type.isEmpty() && type != "image" && type != "file" && type != "folder" && type != "files" && type != "folders") {
                    displayContent = content.trimmed().left(20);
                    if (content.trimmed().length() > 20) displayContent += "...";
                } else {
                    if (type == "image") {
                        displayContent = "图片";
                    } else if (type == "file" || type == "folder" || type == "files" || type == "folders") {
                        QStringList paths = content.split(";", Qt::SkipEmptyParts);
                        if (!paths.isEmpty()) {
                            QString firstPath = paths.first();
                            if (firstPath.endsWith("/") || firstPath.endsWith("\\")) firstPath.chop(1);
                            QString firstName = QFileInfo(firstPath).fileName();
                            if (firstName.isEmpty()) firstName = firstPath;
                            
                            if (paths.size() > 1) {
                                QString suffix = QString(" 等 %1 个项目").arg((int)paths.size());
                                int maxNameLen = qMax(3, 20 - suffix.length());
                                if (firstName.length() > maxNameLen) {
                                    displayContent = firstName.left(maxNameLen - 2) + ".." + suffix;
                                } else {
                                    displayContent = firstName + suffix;
                                }
                            } else {
                                displayContent = firstName;
                            }
                        } else {
                            displayContent = "文件";
                        }
                    } else {
                        displayContent = type;
                    }
                }

                // [CRITICAL] 绝对优先执行显示，甚至在后台重处理线程开启之前
                s_isShowingCopyTip = true;
                ToolTipOverlay::instance()->showText(tipPos, 
                    QString("<b style='color: #2ecc71;'>已复制: %1</b>").arg(displayContent.toHtmlEscaped()), 700, QColor("#2ecc71"));
                
                // 给节流变量设置放行定时器
                QTimer::singleShot(750, [](){ s_isShowingCopyTip = false; });
            }
        }

        /* qDebug() << "[Clipboard->ToolTip DIAG] ToolTip处理完成 | 已耗时 =" << diagClock.elapsed() << "ms"; */

        // ✅ 第二步：[FIX] 2026-03-14 将重处理逻辑移至后台线程执行，彻底释放主线程事件循环
        // 旧方案 singleShot(0) 仍在主线程执行，文件检测/正则/DB写入等耗时操作会阻塞事件循环，
        // 导致 m_hideTimer 的 timeout 信号无法准时派发，表现为 ToolTip 显示 2-3 秒。
        // 新方案使用 QThreadPool 后台线程，主线程仅负责 ToolTip 显示/隐藏。
        QThreadPool::globalInstance()->start([content, type, data, sourceApp, sourceTitle]() {
            QElapsedTimer heavyClock;
            heavyClock.start();
            /* qDebug() << "[Clipboard->ToolTip DIAG] 后台线程开始执行"; */
            int catId = -1;
            if (DatabaseManager::instance().isAutoCategorizeEnabled()) {
                catId = DatabaseManager::instance().extensionTargetCategoryId();
            }
            
            QString title;
            QString finalContent = content;
            QString finalType = type;

            if (type == "image") {
                title = "[截图] " + QDateTime::currentDateTime().toString("MMdd_HHmm");
            } else if (type == "file" || type == "text") {
                QStringList files;
                if (type == "file") {
                    files = content.split(";", Qt::SkipEmptyParts);
                } else {
                    QString trimmed = content.trimmed();
                    if ((trimmed.startsWith("\"") && trimmed.endsWith("\"")) || (trimmed.startsWith("'") && trimmed.endsWith("'"))) {
                        trimmed = trimmed.mid(1, trimmed.length() - 2);
                    }
                    QFileInfo info(trimmed);
                    if (info.exists() && info.isAbsolute()) {
                        files << trimmed;
                        finalType = info.isDir() ? "folder" : "file";
                    }
                }

                if (!files.isEmpty()) {
                    QString firstPath = files.first();
                    if (firstPath.endsWith("/") || firstPath.endsWith("\\")) firstPath.chop(1);
                    QFileInfo info(firstPath);
                    QString name = info.fileName();
                    if (name.isEmpty()) name = firstPath;

                    if (files.size() > 1) {
                        int dirCount = 0;
                        for (const QString& path : files) {
                            if (QFileInfo(path).isDir()) dirCount++;
                        }
                        if (dirCount == files.size()) {
                            title = QString("Copied Folders - %1 等 %2 个文件夹").arg(name).arg((int)files.size());
                            finalType = "folders";
                        } else if (dirCount == 0) {
                            title = QString("Copied Files - %1 等 %2 个文件").arg(name).arg((int)files.size());
                            finalType = "files";
                        } else {
                            title = QString("Copied Items - %1 等 %2 个项目").arg(name).arg((int)files.size());
                            finalType = "files";
                        }
                    } else {
                        if (info.isDir()) {
                            title = "Copied Folder - " + name;
                            finalType = "folder"; 
                        } else {
                            title = "Copied File - " + name;
                            finalType = "file";
                        }
                    }
                } else if (type == "file") {
                    title = "[未知文件]";
                } else {
                    QString firstLine = content.section('\n', 0, 0).trimmed();
                    if (firstLine.isEmpty()) title = "无标题灵感";
                    else {
                        title = firstLine.left(40);
                        if (firstLine.length() > 40) title += "...";
                    }
                }
            }

            QStringList tags;
            if (type == "text") {
                QString trimmed = content.trimmed();
                static QRegularExpression hexRegex("^#([A-Fa-f0-9]{6}|[A-Fa-f0-9]{3})$");
                static QRegularExpression rgbRegex(R"(^(\d{1,3}),\s*(\d{1,3}),\s*(\d{1,3})$)");

                QRegularExpressionMatch hexMatch = hexRegex.match(trimmed);
                bool isColor = false;
                if (hexMatch.hasMatch()) {
                    if (!tags.contains("HEX")) tags << "HEX";
                    isColor = true;
                } else {
                    QRegularExpressionMatch rgbMatch = rgbRegex.match(trimmed);
                    if (rgbMatch.hasMatch()) {
                        int r = rgbMatch.captured(1).toInt();
                        int g = rgbMatch.captured(2).toInt();
                        int b = rgbMatch.captured(3).toInt();
                        if (r <= 255 && g <= 255 && b <= 255) {
                            if (!tags.contains("RGB")) tags << "RGB";
                            isColor = true;
                        }
                    }
                }
                if (isColor) {
                    for (const QString& t : {"色码", "色值", "颜值", "颜色码"}) {
                        if (!tags.contains(t)) tags << t;
                    }
                }

                if (trimmed.startsWith("http://") || trimmed.startsWith("https://") || trimmed.startsWith("www.")) {
                    finalType = "link";
                    tags << "链接" << "网址";
                    QUrl url(trimmed.startsWith("www.") ? "http://" + trimmed : trimmed);
                    QString host = url.host();
                    QStringList hostParts = host.split('.', Qt::SkipEmptyParts);
                    QString domainTitle;
                    if (!hostParts.isEmpty()) {
                        if (hostParts.first().toLower() == "www" && hostParts.size() > 1) domainTitle = hostParts[1];
                        else domainTitle = hostParts.first();
                    }
                    if (!domainTitle.isEmpty()) {
                        domainTitle[0] = domainTitle[0].toUpper();
                        title = domainTitle;
                        for (QString part : std::as_const(hostParts)) {
                            part = part.trimmed();
                            if (part.toLower() == "www" || part.toLower() == "com" || part.toLower() == "cn" || part.toLower() == "net") continue;
                            if (!part.isEmpty()) {
                                part[0] = part[0].toUpper();
                                if (!tags.contains(part)) tags << part;
                            }
                        }
                    }
                }
            }
            
            if (!finalType.isEmpty()) {
                /* qDebug() << "[Clipboard->ToolTip DIAG] addNoteAsync 准备调用 | 后台线程已耗时 =" << heavyClock.elapsed() << "ms"; */
                DatabaseManager::instance().addNoteAsync(title, finalContent, tags, "", catId, finalType, data, sourceApp, sourceTitle);
                /* qDebug() << "[Clipboard->ToolTip DIAG] addNoteAsync 返回 | 后台线程总耗时 =" << heavyClock.elapsed() << "ms"; */
            }
        });
    });

    int result = a.exec();
    
    // [BLOCK] 如果正常循环结束（例如调用了 quit），确保执行最后一遍物理清理
    DatabaseManager::instance().closeAndPack();
    
    return result;
}