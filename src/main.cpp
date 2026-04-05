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
#include <QScreen>
#include <QClipboard>
#include <QMimeData>
#include <functional>
#include <utility>
#include "core/DatabaseManager.h"
#include "core/HotkeyManager.h"
#include "core/ClipboardMonitor.h"
#include "core/OCRManager.h"
#include "ui/IconHelper.h"
#include "ui/QuickWindow.h"
#include "ui/SystemTray.h"
#include "ui/Toolbox.h"
#include "ui/NoteEditWindow.h"
#include "ui/OCRResultWindow.h"
#include "ui/ScreenshotTool.h"

#include <QAbstractItemView>
#include <QHelpEvent>
#include <QModelIndex>

/**
 * @brief [REMOVED] 全局拦截器已移除。
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
        socket.write("SHOW");
        socket.waitForBytesWritten(1000);
        return 0;
    }
    QLocalServer::removeServer(serverName);
    QLocalServer server;
    if (!server.listen(serverName)) {
        qWarning() << "无法启动单实例服务器";
    }

    QFile styleFile(":/qss/dark_style.qss");
    if (styleFile.open(QFile::ReadOnly)) {
        a.setStyleSheet(styleFile.readAll());
    }

    // 1. 初始化数据库
    QString dbPath = QCoreApplication::applicationDirPath() + "/inspiration.db";
    if (!DatabaseManager::instance().init(dbPath)) {
        QString reason = DatabaseManager::instance().getLastError();
        if (reason.isEmpty()) reason = "无法加载加密外壳、解密失败或数据库损坏。";
        QMessageBox::critical(nullptr, "启动失败 (RapidNotes)", 
            QString("<b>程序初始化遭遇异常，无法继续：</b><br><br>%1").arg(reason));
        return -1;
    }

    HttpServer::instance().start(23333);

    auto doSafeExit = [&]() {
        static bool isExiting = false;
        if (isExiting) return;
        isExiting = true;

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
            qApp->processEvents(QEventLoop::AllEvents, 300);
        }
        
        DatabaseManager::instance().closeAndPack();
        QApplication::quit();
    };

    QVariantMap trialStatus = DatabaseManager::instance().getTrialStatus();
    if (trialStatus["fingerprint_mismatch"].toBool()) {
        QMessageBox::critical(nullptr, "系统提示", "<b>[安全拦截] 检测到硬件指纹不匹配。</b>");
        return 0;
    }

    if (!trialStatus["is_activated"].toBool()) {
        QString reason = "<b>欢迎使用 RapidNotes 正版软件</b><br><br>请输入您的专属授权密钥以继续：";
        ActivationDialog dlg(reason);
        if (dlg.exec() != QDialog::Accepted) {
            doSafeExit();
            return 0; 
        }
        trialStatus = DatabaseManager::instance().getTrialStatus();
    }

    // 2. 初始化核心 UI 组件
    QuickWindow* quickWin = new QuickWindow();
    quickWin->setObjectName("QuickWindow");
    quickWin->showAuto();

    FireworksOverlay::instance(); 
    a.setWindowIcon(QIcon(":/app_icon.png"));

    // 4. 子窗口延迟加载
    Toolbox* toolbox = nullptr;
    TimePasteWindow* timePasteWin = nullptr;
    PasswordGeneratorWindow* passwordGenWin = nullptr;
    OCRWindow* ocrWin = nullptr;
    SearchAppWindow* searchWin = nullptr;
    TagManagerWindow* tagMgrWin = nullptr;
    ColorPickerWindow* colorPickerWin = nullptr;
    HelpWindow* helpWin = nullptr;
    TodoCalendarWindow* todoWin = nullptr;

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

    std::function<void(bool)> startCapture;

    auto getToolbox = [&]() -> Toolbox* {
        if (!toolbox) {
            toolbox = new Toolbox();
            toolbox->setObjectName("ToolboxLauncher");

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
                t.repeatMode = 1;
                
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
                QString itemType = immediateOCR ? "ocr_text" : "image";

                int noteId = DatabaseManager::instance().addNote(title, initialContent, tags, "", -1, itemType, ba);
                
                if (isOcrRequest) {
                    QVariantMap existing = DatabaseManager::instance().getNoteById(noteId);
                    QString currentContent = existing.value("content").toString();
                    
                    QSettings settings("RapidNotes", "OCR");
                    bool autoCopy = settings.value("autoCopy", false).toBool();
                    bool silent = settings.value("silentCapture", false).toBool();

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

    auto doAcquire = [=, &checkLockAndExecute, &quickWin]() {
        checkLockAndExecute([&](){
#ifdef Q_OS_WIN
            HWND target = GetForegroundWindow();
            bool isFromUI = (target == (HWND)quickWin->winId());
            
            if (isFromUI) {
                target = quickWin->m_lastActiveHwnd;
                if (target && IsWindow(target)) {
                    if (IsIconic(target)) ShowWindow(target, SW_RESTORE);
                    SetForegroundWindow(target);
                    QThread::msleep(100); 
                }
            }

            if (!StringUtils::isBrowserWindow(target)) {
                ToolTipOverlay::instance()->showText(QCursor::pos(), "✖ 智能采集仅支持浏览器环境");
                return;
            }

            ClipboardMonitor::instance().setIgnore(true);
            QApplication::clipboard()->clear();
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
                    ToolTipOverlay::instance()->showText(QCursor::pos(), "✖ 未能采集到内容");
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

    auto doPurePaste = [=, &quickWin]() {
        QString text = QApplication::clipboard()->text();
        if (!text.isEmpty()) {
            ClipboardMonitor::instance().skipNext();
            QApplication::clipboard()->setText(text);
#ifdef Q_OS_WIN
            HWND target = GetForegroundWindow();
            if (target == (HWND)quickWin->winId()) {
                target = quickWin->m_lastActiveHwnd;
                if (target && IsWindow(target)) {
                    if (IsIconic(target)) ShowWindow(target, SW_RESTORE);
                    SetForegroundWindow(target);
                    QThread::msleep(200);
                }
            }

            INPUT inputs[6];
            memset(inputs, 0, sizeof(inputs));
            inputs[0].type = INPUT_KEYBOARD; inputs[0].ki.wVk = VK_SHIFT; inputs[0].ki.dwFlags = KEYEVENTF_KEYUP;
            inputs[1].type = INPUT_KEYBOARD; inputs[1].ki.wVk = VK_CONTROL;
            inputs[2].type = INPUT_KEYBOARD; inputs[2].ki.wVk = 'V';
            inputs[3].type = INPUT_KEYBOARD; inputs[3].ki.wVk = 'V'; inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
            inputs[4].type = INPUT_KEYBOARD; inputs[4].ki.wVk = VK_CONTROL; inputs[4].ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(5, inputs, sizeof(INPUT));
#endif
            ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #2ecc71;'>[OK] 已纯净粘贴文本</b>");
        }
    };

    KeyboardHook::instance().start();
    HotkeyManager::instance().reapplyHotkeys();
    ReminderService::instance().start();

    QObject::connect(&ReminderService::instance(), &ReminderService::todoReminderTriggered, [&](const DatabaseManager::Todo& todo){
        auto* dlg = new TodoReminderDialog(todo);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->setWindowFlags(dlg->windowFlags() | Qt::WindowStaysOnTopHint);
        
        QObject::connect(dlg, &TodoReminderDialog::snoozeRequested, [todo](int minutes){
            DatabaseManager::Todo updatedTodo = todo;
            updatedTodo.reminderTime = QDateTime::currentDateTime().addSecs(minutes * 60);
            DatabaseManager::instance().updateTodo(updatedTodo);
            ReminderService::instance().removeNotifiedId(todo.id);
        });
        
        dlg->show();
        dlg->raise();
        dlg->activateWindow();
    });
    
    QObject::connect(&KeyboardHook::instance(), &KeyboardHook::globalLockRequested, [&](){
        quickWin->doGlobalLock();
    });
    
    QObject::connect(&HotkeyManager::instance(), &HotkeyManager::hotkeyPressed, [&](int id){
        if (id == 1) {
            if (quickWin->isVisible() && quickWin->isActiveWindow()) {
                quickWin->hide();
            } else {
                quickWin->recordLastActiveWindow(nullptr);
                quickWin->showAuto();
            }
        } else if (id == 2) {
            checkLockAndExecute([&](){
                int lastId = DatabaseManager::instance().getLastCreatedNoteId();
                if (lastId > 0) {
                    DatabaseManager::instance().updateNoteState(lastId, "is_favorite", 1);
                    ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #F2B705;'>★ 已收藏最后一条灵感</b>");
                }
            });
        } else if (id == 3) {
            startCapture(false);
        } else if (id == 4) {
            doAcquire();
        } else if (id == 5) {
            quickWin->doGlobalLock();
        } else if (id == 6) {
            startCapture(true);
        } else if (id == 7) {
            doPurePaste();
        } else if (id == 8) {
            WindowManager::toggle(getToolbox());
        } else if (id == 9) {
            quickWin->showContextNotesMenu();
        }
    });

    QObject::connect(&OCRManager::instance(), &OCRManager::recognitionFinished, &DatabaseManager::instance(), [](const QString& text, int noteId){
        if (noteId > 0) {
            DatabaseManager::instance().updateNoteState(noteId, "content", text);
        }
    });

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

    QObject::connect(&ClipboardMonitor::instance(), &ClipboardMonitor::clipboardChanged, [=](){
        FireworksOverlay::instance()->explode(QCursor::pos());
    });

    QObject::connect(&ClipboardMonitor::instance(), &ClipboardMonitor::newContentDetected, 
        [quickWin](const QString& content, const QString& type, const QByteArray& data,
            const QString& sourceApp, const QString& sourceTitle){
        
        static bool s_isShowingCopyTip = false;
        if (s_isShowingCopyTip) return;

        QElapsedTimer diagClock;
        diagClock.start();

        QSettings gs("RapidNotes", "General");
        bool showTip = gs.value("showCopyToolTip", false).toBool();
        QPoint tipPos = QCursor::pos();

        if (showTip) {
            if (content.trimmed().isEmpty() && type.isEmpty()) {
            } else {
                QString displayContent;
                if (!type.isEmpty() && type != "image" && type != "file" && type != "folder" && type != "files" && type != "folders") {
                    displayContent = content.trimmed().left(20);
                    if (content.trimmed().length() > 20) displayContent += "...";
                } else {
                    if (type == "image") displayContent = "图片";
                    else if (type == "file" || type == "folder" || type == "files" || type == "folders") {
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
                            } else displayContent = firstName;
                        } else displayContent = "文件";
                    } else displayContent = type;
                }

                s_isShowingCopyTip = true;
                ToolTipOverlay::instance()->showText(tipPos, 
                    QString("<b style='color: #2ecc71;'>已复制: %1</b>").arg(displayContent.toHtmlEscaped()), 700, QColor("#2ecc71"));
                QTimer::singleShot(750, [](){ s_isShowingCopyTip = false; });
            }
        }

        QThreadPool::globalInstance()->start([content, type, data, sourceApp, sourceTitle]() {
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
                        for (const QString& path : files) if (QFileInfo(path).isDir()) dirCount++;
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
                } else if (type == "file") title = "[未知文件]";
                else {
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
                    for (const QString& t : {"色码", "色值", "颜值", "颜色码"}) if (!tags.contains(t)) tags << t;
                    catId = DatabaseManager::instance().getOrCreateCategoryByName("Color");
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
                DatabaseManager::instance().addNoteAsync(title, finalContent, tags, "", catId, finalType, data, sourceApp, sourceTitle);
            }
        });
    });

    int result = a.exec();
    DatabaseManager::instance().closeAndPack();
    return result;
}
