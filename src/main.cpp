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
#include "meta/SyncQueue.h"
#include "core/HotkeyManager.h"
#include "ui/MainWindow.h"
#include "ui/IconHelper.h"
#include "ui/SystemTray.h"

#include <QAbstractItemView>
#include <QHelpEvent>
#include <QModelIndex>

/**
 * @brief [REMOVED] 全局拦截器与剪贴板监听已移除。
 */

#include "ui/HelpWindow.h"
#include "ui/SettingsWindow.h"
#include "ui/ActivationDialog.h"
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
        
        ArcMeta::SyncQueue::instance().stop();
        DatabaseManager::instance().closeAndPack();
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
    QString dbPath = QCoreApplication::applicationDirPath() + "/inspiration.db";

    if (!DatabaseManager::instance().init(dbPath)) {
        QString reason = DatabaseManager::instance().getLastError();
        if (reason.isEmpty()) reason = "无法加载加密外壳、解密失败或数据库损坏。";
        
        QMessageBox::critical(nullptr, "启动失败 (ArcMeta)", 
            QString("<b>程序初始化遭遇异常，无法继续：</b><br><br>%1<br><br>建议尝试删除 data 目录下的 kernel 文件后重试。").arg(reason));
            
        return -1;
    }

    // 1.0.5 启动 HTTP 服务
    HttpServer::instance().start(23333);

    // 1.0.6 启动元数据同步引擎
    ArcMeta::SyncQueue::instance().start();

    // 1.1 2026-03-xx 按照用户要求：正版授权强制校验逻辑
    QVariantMap trialStatus = DatabaseManager::instance().getTrialStatus();

    if (trialStatus["fingerprint_mismatch"].toBool()) {
        QMessageBox::critical(nullptr, "系统提示", "<b>[安全拦截] 检测到硬件指纹不匹配。</b><br><br>由于当前设备的硬件指纹与授权记录不符，系统已自动重置本地激活状态以确保证版授权安全。<br><br>请联系管理员获取适用于当前新设备的专属授权码，并重新进行激活。程序将立即退出。");
        return 0;
    }

    if (!trialStatus["is_activated"].toBool()) {
        QString reason = "<b>欢迎使用 ArcMeta 正版软件</b><br><br>检测到当前设备尚未激活，请输入您的专属授权密钥以继续：";
            
        ActivationDialog dlg(reason);
        if (dlg.exec() != QDialog::Accepted) {
            doSafeExit();
            return 0; 
        }
        trialStatus = DatabaseManager::instance().getTrialStatus();
    }

    // 2. 2026-03-23 瘦身：不再初始化 ArcMeta 与 ClipboardMonitor

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

    QObject::connect(&HotkeyManager::instance(), &HotkeyManager::hotkeyPressed, [&](int id){
        if (id == 1) {
            if (mainWin->isVisible() && mainWin->isActiveWindow()) {
                mainWin->hide();
            } else {
                showMainWindow();
            }
        }
    });

    // 7. 系统托盘
    QObject::connect(&server, &QLocalServer::newConnection, [&](){
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
    QObject::connect(tray, &SystemTray::showMainWindow, showMainWindow);

    QObject::connect(tray, &SystemTray::showHelpRequested, [=, &helpWin](){
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
        
        QScreen *screen = QGuiApplication::primaryScreen();
        if (screen) {
            QRect screenGeom = screen->geometry();
            settingsWin->move(screenGeom.center() - settingsWin->rect().center());
        }
        
        settingsWin->show();
        settingsWin->raise();
        settingsWin->activateWindow();
    });
    QObject::connect(tray, &SystemTray::quitApp, doSafeExit);
    tray->show();

    // 默认显示主界面
    showMainWindow();

    int result = a.exec();
    
    // [BLOCK] 确保正常退出时也执行合壳与数据库关闭逻辑
    ArcMeta::SyncQueue::instance().stop();
    DatabaseManager::instance().closeAndPack();
    
    return result;
}
