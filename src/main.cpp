#include <QSettings>
#include <QApplication>
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
 */

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
    qDebug() << "[Main] 数据库外壳路径:" << dbPath;

    if (!DatabaseManager::instance().init(dbPath)) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), 
            "<b style='color: #e74c3c;'>[ERR] 启动失败</b><br>无法初始化数据库！请检查写入权限或 SQLite 驱动。", 5000, QColor("#e74c3c"));
        QThread::msleep(3000); // 留出时间显示提示
        return -1;
    }

    // 1.0.5 启动 HTTP 服务，支持浏览器插件联动
    HttpServer::instance().start(23333);

    // 1.1 试用期与使用次数检查
    QVariantMap trialStatus = DatabaseManager::instance().getTrialStatus();
    qDebug() << "[Trial] 状态检查 - 剩余天数:" << trialStatus["days_left"].toInt() 
             << "使用次数:" << trialStatus["usage_count"].toInt();

    if (trialStatus["expired"].toBool() || trialStatus["usage_limit_reached"].toBool() || trialStatus["is_locked"].toBool()) {
        QString reason = "请联系获取助手：<b style='color: #3a90ff;'>Telegram：TLG_888</b>";
        if (trialStatus["is_locked"].toBool()) {
            reason = "今日激活尝试次数已达上限，软件已安全锁定。<br><br>" + reason;
        } else if (trialStatus["expired"].toBool()) {
            reason = "您的 30 天试用期已无剩余天数，感谢体验！<br><br>" + reason;
        } else {
            reason = "您的使用额度已用完（已使用 100 次）。<br><br>" + reason;
        }
            
        ActivationDialog dlg(reason);
        if (dlg.exec() != QDialog::Accepted) {
            DatabaseManager::instance().closeAndPack();
            return 0; // 用户放弃激活或直接关闭，安全退出
        }
        
        // 如果激活成功，重新获取状态以防万一
        trialStatus = DatabaseManager::instance().getTrialStatus();
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
    SearchAppWindow* searchWin = nullptr;
    TagManagerWindow* tagMgrWin = nullptr;
    ColorPickerWindow* colorPickerWin = nullptr;
    HelpWindow* helpWin = nullptr;
    TodoCalendarWindow* todoWin = nullptr;

    auto toggleWindow = [](QWidget* win, QWidget* parentWin = nullptr) {
        if (!win) return;
        
        // [OPTIMIZED] 简化切换逻辑。只要窗口可见，再次触发即隐藏（不管是否激活）。
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
    std::function<void(bool)> startCapture; // 合并后的截图/OCR 函数

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
            QObject::connect(toolbox, &Toolbox::showKeywordSearchRequested, [=, &searchWin](){
                if (!searchWin) {
                    searchWin = new SearchAppWindow();
                }
                searchWin->switchToKeywordSearch();
                toggleWindow(searchWin);
            });
            QObject::connect(toolbox, &Toolbox::showTagManagerRequested, [=, &tagMgrWin](){
                if (!tagMgrWin) {
                    tagMgrWin = new TagManagerWindow();
                    tagMgrWin->setObjectName("TagManagerWindow");
                }
                tagMgrWin->refreshData();
                toggleWindow(tagMgrWin);
            });
            QObject::connect(toolbox, &Toolbox::showFileSearchRequested, [=, &searchWin](){
                if (!searchWin) {
                    searchWin = new SearchAppWindow();
                }
                searchWin->switchToFileSearch();
                toggleWindow(searchWin);
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
            QObject::connect(toolbox, &Toolbox::showPixelRulerRequested, [](){
                auto* ruler = new PixelRulerOverlay(nullptr);
                ruler->setAttribute(Qt::WA_DeleteOnClose);
                ruler->show();
            });
            QObject::connect(toolbox, &Toolbox::showHelpRequested, [=, &helpWin](){
                if (!helpWin) {
                    helpWin = new HelpWindow();
                    helpWin->setObjectName("HelpWindow");
                }
                toggleWindow(helpWin);
            });
            QObject::connect(toolbox, &Toolbox::showTodoCalendarRequested, [=, &todoWin](){
                if (!todoWin) {
                    todoWin = new TodoCalendarWindow();
                    todoWin->setObjectName("TodoCalendarWindow");
                }
                toggleWindow(todoWin);
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
            QObject::connect(toolbox, &Toolbox::screenshotRequested, [=](){ startCapture(false); });
            QObject::connect(toolbox, &Toolbox::startOCRRequested, [=](){ startCapture(true); });
        }
        return toolbox;
    };

    showMainWindow = [=, &mainWin, &checkLockAndExecute, &getToolbox, &quickWin]() {
        checkLockAndExecute([=, &mainWin, &getToolbox, &quickWin](){
            if (!mainWin) {
                mainWin = new MainWindow();
                QObject::connect(mainWin, &MainWindow::toolboxRequested, [=](){ toggleWindow(getToolbox(), mainWin); });
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

    QObject::connect(quickWin, &QuickWindow::toolboxRequested, [=, &getToolbox](){ toggleWindow(getToolbox(), quickWin); });
    QObject::connect(quickWin, &QuickWindow::toggleMainWindowRequested, [=, &showMainWindow](){ showMainWindow(); });

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
            startCapture(false);
        } else if (id == 4) {
            checkLockAndExecute([&](){
                qDebug() << "[Acquire] 触发采集流程，开始环境检测...";
#ifdef Q_OS_WIN
                if (!StringUtils::isBrowserActive()) {
                    qDebug() << "[Acquire] 拒绝执行：当前窗口非浏览器环境。";
                    return;
                }
                ClipboardMonitor::instance().setIgnore(true);
                QApplication::clipboard()->clear();

                // [USER_REQUEST] 修复采集冲突：显式释放 S 键，防止弹出浏览器“另存为”对话框
                keybd_event('S', 0, KEYEVENTF_KEYUP, 0);
                keybd_event(VK_CONTROL, 0, 0, 0);
                keybd_event('C', 0, 0, 0);
                keybd_event('C', 0, KEYEVENTF_KEYUP, 0);
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

                    // [USER_REQUEST] 1:1 恢复旧版智能分对入库逻辑
                    auto pairs = StringUtils::smartSplitPairs(text);
                    if (pairs.isEmpty()) return;

                    int catId = -1;
                    if (quickWin && quickWin->isVisible()) {
                        catId = quickWin->getCurrentCategoryId();
                    }

                    for (const auto& pair : std::as_const(pairs)) {
                        QStringList tags = {"采集"};
                        if (StringUtils::containsThai(pair.second)) tags << "泰文";
                        DatabaseManager::instance().addNoteAsync(pair.first, pair.second, tags, "", catId, "text");
                    }
                    
                    QString feedback = QString("[OK] 已成功采集 %1 条灵感").arg(pairs.size());
                    ToolTipOverlay::instance()->showText(QCursor::pos(), feedback);
                });
            });
        } else if (id == 5) {
            // 全局锁定
            quickWin->doGlobalLock();
        } else if (id == 6) {
            // 截图取文
            startCapture(true);
        } else if (id == 7) {
            // [USER_REQUEST] 对齐旧版 ID 7：仅作为工具箱全局热键，移除干扰的纯净粘贴逻辑
            toggleWindow(getToolbox());
        } else if (id == 9) { // [USER_REQUEST] 将纯净粘贴重定向到 ID 9
            // 全局纯净粘贴
            QString text = QApplication::clipboard()->text();
            if (!text.isEmpty()) {
                // 1. 强制忽略下一次剪贴板变化，防止回环采集
                ClipboardMonitor::instance().skipNext();
                // 2. 重新存入剪贴板 (剥离富文本格式，QApplication::clipboard()->setText 默认处理为纯文本)
                QApplication::clipboard()->setText(text);
                
                // 3. 模拟 Ctrl+V (使用 SendInput 以获得更好的兼容性，并显式处理 Shift 状态)
#ifdef Q_OS_WIN
                // [CRITICAL] 锁定：强制抬起 Shift 键。
                // 用户的热键是 Ctrl+Shift+V，此时 Shift 是按下的。如果不抬起，目标应用会收到 Ctrl+Shift+V。
                INPUT inputs[6];
                memset(inputs, 0, sizeof(inputs));

                // 抬起 SHIFT
                inputs[0].type = INPUT_KEYBOARD;
                inputs[0].ki.wVk = VK_SHIFT;
                inputs[0].ki.wScan = MapVirtualKey(VK_SHIFT, MAPVK_VK_TO_VSC);
                inputs[0].ki.dwFlags = KEYEVENTF_KEYUP;

                // 按下 CTRL
                inputs[1].type = INPUT_KEYBOARD;
                inputs[1].ki.wVk = VK_CONTROL;
                inputs[1].ki.wScan = MapVirtualKey(VK_CONTROL, MAPVK_VK_TO_VSC);

                // 按下 V
                inputs[2].type = INPUT_KEYBOARD;
                inputs[2].ki.wVk = 'V';
                inputs[2].ki.wScan = MapVirtualKey('V', MAPVK_VK_TO_VSC);

                // 抬起 V
                inputs[3].type = INPUT_KEYBOARD;
                inputs[3].ki.wVk = 'V';
                inputs[3].ki.wScan = MapVirtualKey('V', MAPVK_VK_TO_VSC);
                inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;

                // 抬起 CTRL
                inputs[4].type = INPUT_KEYBOARD;
                inputs[4].ki.wVk = VK_CONTROL;
                inputs[4].ki.wScan = MapVirtualKey(VK_CONTROL, MAPVK_VK_TO_VSC);
                inputs[4].ki.dwFlags = KEYEVENTF_KEYUP;

                SendInput(5, inputs, sizeof(INPUT));
#endif
                ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #2ecc71;'>[OK] 已纯净粘贴文本</b>");
            }
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
    QObject::connect(tray, &SystemTray::showQuickWindow, quickWin, &QuickWindow::showAuto);
    QObject::connect(tray, &SystemTray::showTodoCalendar, [=, &todoWin](){
        if (!todoWin) {
            todoWin = new TodoCalendarWindow();
            todoWin->setObjectName("TodoCalendarWindow");
        }
        toggleWindow(todoWin);
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
            toggleWindow(helpWin);
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

        // 自动归档逻辑
        int catId = -1;
        if (DatabaseManager::instance().isAutoCategorizeEnabled()) {
            catId = DatabaseManager::instance().extensionTargetCategoryId();
        }
        
        QString title;
        QString finalContent = content;
        QString finalType = type;

        if (type == "image") {
            title = "[截图] " + QDateTime::currentDateTime().toString("MMdd_HHmm");
        } else if (type == "file") {
            QStringList files = content.split(";", Qt::SkipEmptyParts);
            if (!files.isEmpty()) {
                QFileInfo info(files.first());
                if (files.size() > 1) {
                    title = QString("Copied Files - %1 等 %2 个文件").arg(info.fileName()).arg((int)files.size());
                } else {
                    if (info.isDir()) {
                        title = QString("Copied Folder - %1").arg(info.fileName());
                    } else {
                        title = QString("Copied File - %1").arg(info.fileName());
                    }
                }
            } else {
                title = "[未知文件]";
            }
        } else {
            // 文本：统一逻辑，强制截取前40个字符作为标题，正文保存全部
            QString cleanContent = content.trimmed().replace("\r", " ").replace("\n", " ").simplified();
            title = cleanContent.left(40);
            if (title.isEmpty()) title = "无标题灵感";
        }

        // 自动生成类型标签与类型修正 (解耦逻辑)
        QStringList tags;
        
        if (type == "text") {
            QString trimmed = content.trimmed();

            // 颜色码识别逻辑
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

            // 恢复后的网址识别逻辑
            if (trimmed.startsWith("http://") || trimmed.startsWith("https://") || trimmed.startsWith("www.")) {
                finalType = "link";
                tags << "链接" << "网址";
                // 标题依然遵循 40 字符截取逻辑，不再强制修改为域名，以保持全程序统一
            }
        }
        
        DatabaseManager::instance().addNoteAsync(title, finalContent, tags, "", catId, finalType, data, sourceApp, sourceTitle);
    });

    int result = a.exec();
    
    // 退出前合壳并加密数据库
    DatabaseManager::instance().closeAndPack();
    
    return result;
}