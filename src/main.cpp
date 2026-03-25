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
#include "core/ShortcutManager.h"
#include "core/OCRManager.h"
#include "ui/IconHelper.h"
#include "ui/ToolTipOverlay.h"
#include "ui/StringUtils.h"

// ==========================================
// 编译目标特定头文件隔离
// ==========================================
#ifdef RAPID_NOTES_TARGET
#include "core/HotkeyManager.h"
#include "core/ClipboardMonitor.h"
#include "core/KeyboardHook.h"
#include "core/ReminderService.h"
#include "core/HttpServer.h"
#include "ui/QuickWindow.h"
#include "ui/SystemTray.h"
#include "ui/Toolbox.h"
#include "ui/FireworksOverlay.h"
#include "ui/ScreenshotTool.h"
#include "ui/OCRWindow.h"
#include "ui/OCRResultWindow.h"
#include "ui/NoteEditWindow.h"
#include "ui/SearchAppWindow.h"
#include "ui/TagManagerWindow.h"
#include "ui/ColorPickerWindow.h"
#include "ui/PixelRulerOverlay.h"
#include "ui/HelpWindow.h"
#include "ui/TimePasteWindow.h"
#include "ui/PasswordGeneratorWindow.h"
#include "ui/TodoCalendarWindow.h"
#include "ui/SettingsWindow.h"
#else
#include "ui/MainWindow.h"
#include "ui/FireworksOverlay.h"
#include "ui/QuickPreview.h"
#endif

#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

int main(int argc, char *argv[]) {
    QApplication a(argc, argv);
    a.setOrganizationName("RapidDev");
    a.setWindowIcon(QIcon(":/app_icon.png"));

#ifdef RAPID_MANAGER_TARGET
    // ==========================================
    // 管理端 (RapidManager) 配置
    // ==========================================
    a.setApplicationName("RapidManager");
    QString serverName = "RapidManager_SingleInstance_Server";
    QString dbFileName = "manager_kernel.db";
    a.setQuitOnLastWindowClosed(true);
#else
    // ==========================================
    // 采集端 (RapidNotes) 配置
    // ==========================================
    a.setApplicationName("RapidNotes");
    QString serverName = "RapidNotes_SingleInstance_Server";
    QString dbFileName = "inspiration.db";
    a.setQuitOnLastWindowClosed(false);
#endif

    // 单实例运行保护
    QLocalSocket socket;
    socket.connectToServer(serverName);
    if (socket.waitForConnected(500)) {
#ifdef RAPID_NOTES_TARGET
        socket.write("SHOW");
        socket.waitForBytesWritten(1000);
#endif
        return 0;
    }
    QLocalServer::removeServer(serverName);
    QLocalServer server;
    server.listen(serverName);

    // 加载样式表
    QFile styleFile(":/qss/dark_style.qss");
    if (styleFile.open(QFile::ReadOnly)) {
        a.setStyleSheet(styleFile.readAll());
    }

    // 初始化物理隔离的数据库
    QString dbPath = QCoreApplication::applicationDirPath() + "/" + dbFileName;
    if (!DatabaseManager::instance().init(dbPath)) {
        QMessageBox::critical(nullptr, "启动失败", "数据库初始化失败，程序无法继续运行。");
        return -1;
    }

    // 程序退出流
    auto doSafeExit = [&]() {
        DatabaseManager::instance().closeAndPack();
        QApplication::quit();
    };

#ifdef RAPID_MANAGER_TARGET
    // ==========================================
    // 管理端 (RapidManager) 启动流
    // ==========================================
    FireworksOverlay::instance();
    MainWindow mainWin;
    mainWin.setObjectName("RapidMainWindow");
    mainWin.setWindowTitle("RapidManager - 数据管理终端");
    mainWin.show();

#else
    // ==========================================
    // 采集端 (RapidNotes) 启动流
    // ==========================================
    HttpServer::instance().start(23333);

    QuickWindow* quickWin = new QuickWindow();
    quickWin->setObjectName("QuickWindow");
    quickWin->showAuto();

    FireworksOverlay::instance(); 
    KeyboardHook::instance().start();
    HotkeyManager::instance().reapplyHotkeys();
    ReminderService::instance().start();

    // 信号处理
    QObject::connect(&server, &QLocalServer::newConnection, [&](){
        QLocalSocket* conn = server.nextPendingConnection();
        if (conn->waitForReadyRead(500)) {
            if (conn->readAll() == "SHOW") quickWin->showAuto();
            conn->disconnectFromServer();
        }
    });

    QObject::connect(&HotkeyManager::instance(), &HotkeyManager::hotkeyPressed, [&](int id){
        if (id == 1) { // 切换快速窗口
            if (quickWin->isVisible() && quickWin->isActiveWindow()) quickWin->hide();
            else quickWin->showAuto();
        }
    });

    // 托盘菜单
    SystemTray* tray = new SystemTray(&a);
    QObject::connect(tray, &SystemTray::showQuickWindow, [=](){ quickWin->showAuto(); });
    QObject::connect(tray, &SystemTray::quitApp, doSafeExit);
    tray->show();

    // 剪贴板自动记录
    QObject::connect(&ClipboardMonitor::instance(), &ClipboardMonitor::newContentDetected, 
        [](const QString& content, const QString& type, const QByteArray& data, const QString& sourceApp, const QString& sourceTitle){
        DatabaseManager::instance().addNoteAsync("New Note", content, {}, "", -1, type, data, sourceApp, sourceTitle);
    });
#endif

    int result = a.exec();
    DatabaseManager::instance().closeAndPack();
    return result;
}
