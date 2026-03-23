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
#include "ui/IconHelper.h"
#include "ui/QuickWindow.h"
#include "ui/SystemTray.h"
#include "ui/Toolbox.h"

#include <QAbstractItemView>
#include <QHelpEvent>
#include <QModelIndex>

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
#include "db/Database.h"
#include "mft/MftReader.h"
#include "mft/UsnWatcher.h"

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

    // 加载全局样式表
    QFile styleFile(":/qss/dark_style.qss");
    if (styleFile.open(QFile::ReadOnly)) {
        a.setStyleSheet(styleFile.readAll());
    }

    // 1. 初始化笔记数据库 (inspiration.db)
    QString dbPath = QCoreApplication::applicationDirPath() + "/inspiration.db";
    if (!DatabaseManager::instance().init(dbPath)) {
        QString reason = DatabaseManager::instance().getLastError();
        if (reason.isEmpty()) reason = "无法加载加密外壳、解密失败或数据库损坏。";
        QMessageBox::critical(nullptr, "启动失败", reason);
        return -1;
    }

    // 1.0.5 启动 HTTP 服务
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
        DatabaseManager::instance().closeAndPack();
        QApplication::quit();
    };

    QVariantMap trialStatus = DatabaseManager::instance().getTrialStatus();
    if (trialStatus["fingerprint_mismatch"].toBool()) {
        QMessageBox::critical(nullptr, "系统提示", "[安全拦截] 检测到硬件指纹不匹配。程序将立即退出。");
        return 0;
    }

    if (!trialStatus["is_activated"].toBool()) {
        ActivationDialog dlg("欢迎使用 RapidNotes 正版软件。请输入授权密钥：");
        if (dlg.exec() != QDialog::Accepted) {
            doSafeExit();
            return 0; 
        }
    }

    // 2. 初始化核心 UI 组件 (快速笔记窗口与悬浮球)
    QuickWindow* quickWin = new QuickWindow();
    quickWin->setObjectName("QuickWindow");
    quickWin->showAuto();

    FireworksOverlay::instance(); 
    a.setWindowIcon(QIcon(":/app_icon.png"));

    // 4. 子窗口延迟加载策略
    ui::MainWindow* mainWin = nullptr;
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

    std::function<void()> showMainWindow;
    std::function<void(bool)> startCapture;

    auto getToolbox = [&]() -> Toolbox* {
        if (!toolbox) {
            toolbox = new Toolbox();
            toolbox->setObjectName("ToolboxLauncher");
            QObject::connect(toolbox, &Toolbox::visibilityChanged, quickWin, &QuickWindow::updateToolboxStatus);
            if (mainWin) {
                QObject::connect(toolbox, &Toolbox::visibilityChanged, mainWin, &ui::MainWindow::updateToolboxStatus);
                mainWin->updateToolboxStatus(toolbox->isVisible());
            }
            quickWin->updateToolboxStatus(toolbox->isVisible());
            
            QObject::connect(toolbox, &Toolbox::showTimePasteRequested, [=, &timePasteWin](){
                if (!timePasteWin) timePasteWin = new TimePasteWindow();
                WindowManager::toggle(timePasteWin);
            });
            QObject::connect(toolbox, &Toolbox::showPasswordGeneratorRequested, [=, &passwordGenWin](){
                if (!passwordGenWin) passwordGenWin = new PasswordGeneratorWindow();
                WindowManager::toggle(passwordGenWin);
            });
            QObject::connect(toolbox, &Toolbox::showOCRRequested, [=, &ocrWin](){
                if (!ocrWin) ocrWin = new OCRWindow();
                WindowManager::toggle(ocrWin);
            });
            QObject::connect(toolbox, &Toolbox::showKeywordSearchRequested, [=, &searchWin](){
                if (!searchWin) searchWin = new SearchAppWindow();
                searchWin->switchToKeywordSearch();
                WindowManager::toggle(searchWin);
            });
            QObject::connect(toolbox, &Toolbox::showTagManagerRequested, [=, &tagMgrWin](){
                if (!tagMgrWin) tagMgrWin = new TagManagerWindow();
                tagMgrWin->refreshData();
                WindowManager::toggle(tagMgrWin);
            });
            QObject::connect(toolbox, &Toolbox::showFileSearchRequested, [=, &searchWin](){
                if (!searchWin) searchWin = new SearchAppWindow();
                searchWin->switchToFileSearch();
                WindowManager::toggle(searchWin);
            });
            QObject::connect(toolbox, &Toolbox::showColorPickerRequested, [=, &colorPickerWin](){
                if (!colorPickerWin) colorPickerWin = new ColorPickerWindow();
                WindowManager::toggle(colorPickerWin);
            });
            QObject::connect(toolbox, &Toolbox::showPixelRulerRequested, [](){
                auto* ruler = new PixelRulerOverlay(nullptr);
                ruler->setAttribute(Qt::WA_DeleteOnClose);
                ruler->show();
            });
            QObject::connect(toolbox, &Toolbox::showHelpRequested, [=, &helpWin](){
                if (!helpWin) helpWin = new HelpWindow();
                WindowManager::toggle(helpWin);
            });
            QObject::connect(toolbox, &Toolbox::showTodoCalendarRequested, [=, &todoWin](){
                if (!todoWin) todoWin = new TodoCalendarWindow();
                WindowManager::toggle(todoWin);
            });

            QObject::connect(toolbox, &Toolbox::showMainWindowRequested, [=](){ showMainWindow(); });
            QObject::connect(toolbox, &Toolbox::showQuickWindowRequested, [=](){ quickWin->showAuto(); });
            QObject::connect(toolbox, &Toolbox::screenshotRequested, [=](){ startCapture(false); });
            QObject::connect(toolbox, &Toolbox::startOCRRequested, [=](){ startCapture(true); });
        }
        return toolbox;
    };

    showMainWindow = [=, &mainWin, &checkLockAndExecute, &getToolbox, &quickWin, &toolbox]() {
        checkLockAndExecute([=, &mainWin, &getToolbox, &quickWin, &toolbox](){
            if (!mainWin) {
                mainWin = new ui::MainWindow();
                QObject::connect(mainWin, &ui::MainWindow::toolboxRequested, [=](){ WindowManager::toggle(getToolbox(), mainWin); });
                if (toolbox) {
                    QObject::connect(toolbox, &Toolbox::visibilityChanged, mainWin, &ui::MainWindow::updateToolboxStatus);
                    mainWin->updateToolboxStatus(toolbox->isVisible());
                }
            }
            mainWin->showNormal();
            mainWin->activateWindow();
            mainWin->raise();
        });
    };

    startCapture = [=, &checkLockAndExecute](bool immediateOCR) {
        checkLockAndExecute([&](){
            auto* tool = new ScreenshotTool();
            tool->setAttribute(Qt::WA_DeleteOnClose);
            if (immediateOCR) tool->setImmediateOCRMode(true);
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
                int noteId = DatabaseManager::instance().addNote(title, "", {"截图"}, "", -1, "image", ba);
                if (isOcrRequest) {
                    auto* resWin = new OCRResultWindow(img, noteId);
                    QObject::connect(&OCRManager::instance(), &OCRManager::recognitionFinished, resWin, &OCRResultWindow::setRecognizedText);
                    resWin->show();
                    OCRManager::instance().recognizeAsync(img, noteId);
                }
            });
            tool->show();
        });
    };

    KeyboardHook::instance().start();
    MessageCaptureHandler::instance().init();
    HotkeyManager::instance().reapplyHotkeys();
    ReminderService::instance().start();

    QObject::connect(&HotkeyManager::instance(), &HotkeyManager::hotkeyPressed, [&](int id){
        if (id == 1) quickWin->showAuto();
        else if (id == 3) startCapture(false);
        else if (id == 6) startCapture(true);
        else if (id == 8) WindowManager::toggle(getToolbox());
    });

    SystemTray* tray = new SystemTray(&a);
    QObject::connect(tray, &SystemTray::showMainWindow, showMainWindow);
    QObject::connect(tray, &SystemTray::showQuickWindow, [=](){ quickWin->showAuto(); });
    QObject::connect(tray, &SystemTray::quitApp, doSafeExit);
    tray->show();

    QObject::connect(&ClipboardMonitor::instance(), &ClipboardMonitor::newContentDetected, 
        [](const QString& content, const QString& type, const QByteArray& data, const QString& sourceApp, const QString& sourceTitle){
        QThreadPool::globalInstance()->start([content, type, data, sourceApp, sourceTitle]() {
            DatabaseManager::instance().addNoteAsync(content.left(40), content, {}, "", -1, type, data, sourceApp, sourceTitle);
        });
    });

    return a.exec();
}
