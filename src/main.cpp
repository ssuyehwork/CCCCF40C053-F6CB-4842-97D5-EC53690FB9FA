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
#include "db/Database.h"
#include "core/CoreController.h"
#include "core/HotkeyManager.h"
#include "ui/MainWindow.h"
#include "ui/IconHelper.h"
#include "ui/SystemTray.h"

#include <QAbstractItemView>
#include <QHelpEvent>
#include <QModelIndex>

#include "ui/HelpWindow.h"
#include "ui/SettingsWindow.h"
#include "ui/ToolTipOverlay.h"
#include "ui/StringUtils.h"
#include "core/KeyboardHook.h"
#include "core/FileCryptoHelper.h"
#include "core/FileStorageHelper.h"
#include "core/HttpServer.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

int main(int argc, char *argv[]) {
    QApplication a(argc, argv);
    
    a.setApplicationName("ArcMeta");
    a.setOrganizationName("RapidDev");
    a.setQuitOnLastWindowClosed(false);

    // [ARCH-CLEANUP] 定义统一的程序退出流
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
        
        QApplication::quit();
    };

    // 单实例运行保护
    QString serverName = "ArcMeta_SingleInstance_Server";
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

    // 1. 初始化数据库
    std::wstring dbPath = (QCoreApplication::applicationDirPath() + "/arcmeta.db").toStdWString();

    if (!ArcMeta::Database::instance().init(dbPath)) {
        QMessageBox::critical(nullptr, "启动失败 (ArcMeta)", 
            "<b>程序初始化遭遇异常，无法继续。</b><br><br>建议尝试删除数据文件后重试。");
        return -1;
    }

    // 1.0.5 启动 HTTP 服务
    HttpServer::instance().start(23333);

    // 1.1 启动核心控制器
    ArcMeta::CoreController::instance().startSystem();

    // 2026-03-20 恢复原版笔记本图标
    a.setWindowIcon(QIcon(":/app_icon.png"));

    // 4. 初始化 MainWindow (默认启动窗口)
    MainWindow* mainWin = new MainWindow();
    HelpWindow* helpWin = nullptr;

    auto showMainWindow = [=, &mainWin]() {
        if (!mainWin) return;
        mainWin->showNormal();
        mainWin->activateWindow();
        mainWin->raise();
    };

    // 5. 开启全局键盘钩子
    KeyboardHook::instance().start();

    // 6. 注册全局热键
    HotkeyManager::instance().reapplyHotkeys();

    // 2026-04-04 按照用户要求修复 MSVC 重载转换错误：补全 connect 上下文对象 (&a)
    QObject::connect(&HotkeyManager::instance(), &HotkeyManager::hotkeyPressed, &a, [&](int id){
        if (id == 1) {
            if (mainWin->isVisible() && mainWin->isActiveWindow()) {
                mainWin->hide();
            } else {
                showMainWindow();
            }
        }
    });

    // 7. 系统处理单实例请求
    QObject::connect(&server, &QLocalServer::newConnection, &a, [&](){
        QLocalSocket* conn = server.nextPendingConnection();
        if (conn->waitForReadyRead(500)) {
            QByteArray data = conn->readAll();
            if (data == "SHOW") {
                showMainWindow();
            }
            conn->disconnectFromServer();
        }
    });

    SystemTray* tray = new SystemTray(&a);
    QObject::connect(tray, &SystemTray::showMainWindow, &a, [=](){ showMainWindow(); });

    QObject::connect(tray, &SystemTray::showHelpRequested, &a, [=, &helpWin](){
        if (!helpWin) {
            helpWin = new HelpWindow();
            helpWin->setObjectName("HelpWindow");
        }
        if (helpWin->isVisible()) helpWin->hide();
        else {
            helpWin->show();
            helpWin->raise();
            helpWin->activateWindow();
        }
    });

    QObject::connect(tray, &SystemTray::showSettings, &a, [=](){
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

    QObject::connect(tray, &SystemTray::quitApp, &a, doSafeExit);
    tray->show();

    // 默认显示主界面
    showMainWindow();

    return a.exec();
}

