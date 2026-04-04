#include <QApplication>
#include <QMessageBox>
#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <windows.h>
#include <shellapi.h>
#include "ui/MainWindow.h"
#include "db/Database.h"
#include "db/SyncEngine.h"
#include "meta/SyncQueue.h"
#include "core/CoreController.h"

// 2026-04-04 [STRATEGY] 彻底转型为 ArcMeta 超级资源管理器。
// 移除所有 RapidNotes 相关的笔记逻辑与 DatabaseManager。

/**
 * @brief 自定义日志处理程序
 */
void customMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg) {
    Q_UNUSED(context);
    QFile logFile("arcmeta_debug.log");
    if (logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        QTextStream textStream(&logFile);
        QString timeStr = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
        QString level;
        switch (type) {
            case QtDebugMsg:    level = "DEBUG";    break;
            case QtInfoMsg:     level = "INFO ";    break;
            case QtWarningMsg:  level = "WARN ";    break;
            case QtCriticalMsg: level = "CRIT ";    break;
            case QtFatalMsg:    level = "FATAL";    break;
        }
        textStream << QString("[%1][%2] %3").arg(timeStr, level, msg) << Qt::endl;
        logFile.close();
    }
}

int main(int argc, char *argv[]) {
    qInstallMessageHandler(customMessageHandler);
    qDebug() << "================ ArcMeta 启动 (超级资源管理器) ==================";

    QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QApplication a(argc, argv);
    a.setApplicationName("ArcMeta");
    a.setOrganizationName("ArcMetaTeam");

    // 加载全局样式表 (保持 UI 风格不变)
    QFile styleFile(":/qss/dark_style.qss");
    if (styleFile.open(QFile::ReadOnly)) {
        a.setStyleSheet(styleFile.readAll());
    }

    // 初始化数据库
    std::wstring dbPath = L"arcmeta.db";
    if (!ArcMeta::Database::instance().init(dbPath)) {
        QMessageBox::critical(nullptr, "错误", "无法初始化数据库，程序即将退出。");
        return -1;
    }

    // 构造主窗口
    MainWindow w;

    // 绑定初始化完成信号
    QObject::connect(&ArcMeta::CoreController::instance(), &ArcMeta::CoreController::initializationFinished, &w, [&w]() {
        qDebug() << "[Main] 系统就绪，正在打开窗口...";
        w.show();
    }, Qt::QueuedConnection);

    // 启动异步引擎
    ArcMeta::CoreController::instance().startSystem();

    int ret = a.exec();

    // 优雅退出
    ArcMeta::SyncQueue::instance().stop();

    return ret;
}
