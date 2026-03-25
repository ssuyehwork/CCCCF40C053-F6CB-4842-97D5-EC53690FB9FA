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

    // 设置独立的应用名称，确保 QSettings 物理隔离
    a.setApplicationName("RapidManager");
    a.setOrganizationName("RapidDev");
    a.setQuitOnLastWindowClosed(true); // 管理端主窗口关闭即退出程序

    // 单实例运行保护 (使用独立的 Server Name)
    QString serverName = "RapidManager_SingleInstance_Server";
    QLocalSocket socket;
    socket.connectToServer(serverName);
    if (socket.waitForConnected(500)) {
        return 0;
    }
    QLocalServer::removeServer(serverName);
    QLocalServer server;
    server.listen(serverName);

    // 加载全局样式表
    QFile styleFile(":/qss/dark_style.qss");
    if (styleFile.open(QFile::ReadOnly)) {
        a.setStyleSheet(styleFile.readAll());
    }

    // 1. 初始化独立数据库 (manager.db)
    // 2026-03-xx 按照用户要求：物理隔离数据库，不再共用 inspiration.db
    QString dbPath = QCoreApplication::applicationDirPath() + "/manager.db";

    if (!DatabaseManager::instance().init(dbPath)) {
        QString reason = DatabaseManager::instance().getLastError();
        QMessageBox::critical(nullptr, "启动失败 (RapidManager)",
            QString("<b>管理终端初始化失败：</b><br><br>%1").arg(reason));
        return -1;
    }

    // 2026-03-xx 按照用户要求：彻底移除正版授权校验逻辑，直接进入管理终端

    // 3. 启动特效层 (UI 增强)
    FireworksOverlay::instance();

    // 4. 加载主管理窗口
    MainWindow mainWin;
    mainWin.setObjectName("RapidMainWindow");
    mainWin.setWindowTitle("RapidManager - 数据管理终端");

    // 2026-03-xx 按照用户要求：MainWindow 独立化，屏蔽原本发往 QuickWindow 的信号连接
    // 这里的 MainWindow 将作为一个完全自包含的实体运行

    mainWin.show();

    int result = a.exec();

    // 退出前合壳加密保存
    DatabaseManager::instance().closeAndPack();

    return result;
}
