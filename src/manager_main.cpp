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

int main(int argc, char *argv[]) {
    QApplication a(argc, argv);

    a.setApplicationName("RapidManager");
    a.setOrganizationName("RapidDev");
    a.setQuitOnLastWindowClosed(true);

    QString serverName = "RapidManager_SingleInstance_Server";
    QLocalSocket socket;
    socket.connectToServer(serverName);
    if (socket.waitForConnected(500)) return 0;

    QLocalServer::removeServer(serverName);
    QLocalServer server;
    server.listen(serverName);

    QFile styleFile(":/qss/dark_style.qss");
    if (styleFile.open(QFile::ReadOnly)) a.setStyleSheet(styleFile.readAll());

    QString dbPath = QCoreApplication::applicationDirPath() + "/manager.db";
    if (!DatabaseManager::instance().init(dbPath)) {
        QMessageBox::critical(nullptr, "启动失败", "数据库初始化失败。");
        return -1;
    }

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
