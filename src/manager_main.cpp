#include <QApplication>
#include <QFile>
#include <QMessageBox>
#include <QLocalServer>
#include <QLocalSocket>
#include <QDir>
#include <QStandardPaths>
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

    // [CRITICAL] 自动路径修复逻辑：优先尝试程序目录，若无权限则平滑迁移至用户文档目录
    QString dbPath = QCoreApplication::applicationDirPath() + "/manager.db";
    QFileInfo dbInfo(dbPath);
    QDir dbDir = dbInfo.absoluteDir();

    // 如果目录不存在，尝试创建
    if (!dbDir.exists()) {
        dbDir.mkpath(".");
    }

    // 预检目录写入权限。如果无法写入（如在 C:/Program Files 下），则切换至 AppData
    QFile testFile(dbPath + ".test");
    if (!testFile.open(QIODevice::WriteOnly)) {
        QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir(appDataPath).mkpath(".");
        dbPath = appDataPath + "/manager.db";
    } else {
        testFile.remove();
    }

    if (!DatabaseManager::instance().init(dbPath)) {
        QMessageBox::critical(nullptr, "启动失败",
            QString("数据库初始化失败。\n\n详情: %1\n路径: %2")
            .arg(DatabaseManager::instance().getLastError(), dbPath));
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
