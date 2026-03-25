#include <QApplication>
#include <QFile>
#include <QMessageBox>
#include <QLocalServer>
#include <QLocalSocket>
#include <QDir>
#include <QScreen>
#include "core/DatabaseManager.h"
#include "ui/MainWindow.h"
#include "ui/ToolTipOverlay.h"
#include "ui/FireworksOverlay.h"

/**
 * RapidManager - 独立数据管理终端
 * 2026-03-xx 按照用户要求：仅保留 MainWindow 界面，实现单窗口闭环管理。
 */
int main(int argc, char *argv[]) {
    QApplication a(argc, argv);
    
    // 设置应用名称，锁定配置域
    a.setApplicationName("RapidManager");
    a.setOrganizationName("RapidDev");
    a.setQuitOnLastWindowClosed(true);

    // 单实例运行保护
    QString serverName = "RapidManager_SingleInstance_Server";
    QLocalSocket socket;
    socket.connectToServer(serverName);
    if (socket.waitForConnected(500)) return 0;
    QLocalServer::removeServer(serverName);
    QLocalServer server;
    server.listen(serverName);

    // 加载样式表
    QFile styleFile(":/qss/dark_style.qss");
    if (styleFile.open(QFile::ReadOnly)) a.setStyleSheet(styleFile.readAll());

    // 1. 初始化数据库 (manager.db)
    QString dbPath = QCoreApplication::applicationDirPath() + "/manager.db";
    if (!DatabaseManager::instance().init(dbPath)) {
        QMessageBox::critical(nullptr, "启动失败", DatabaseManager::instance().getLastError());
        return -1;
    }

    // 2. 启动特效
    FireworksOverlay::instance(); 

    // 3. 显示主窗口
    MainWindow mainWin;
    mainWin.setObjectName("RapidMainWindow");
    mainWin.setWindowTitle("RapidManager - 数据管理终端");
    mainWin.show();

    int result = a.exec();
    DatabaseManager::instance().closeAndPack();
    return result;
}
