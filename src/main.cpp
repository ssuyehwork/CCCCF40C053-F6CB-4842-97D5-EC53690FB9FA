#include <QSettings>
#include <QApplication>
#include <QThread>
#include <QFile>
#include <QCursor>
#include <QMessageBox>
#include <QCoreApplication>
#include <QDir>
#include <QDebug>
#include <QDateTime>
#include <QFileInfo>
#include <QSet>
#include <QBuffer>
#include <QUrl>
#include <QTimer>
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
#include "ui/FloatingBall.h"
#include "ui/QuickWindow.h"
#include "ui/SystemTray.h"
#include "ui/Toolbox.h"

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
    server.listen(serverName);

    // 加载样式
    QFile styleFile(":/qss/dark_style.qss");
    if (styleFile.open(QFile::ReadOnly)) a.setStyleSheet(styleFile.readAll());

    // 数据库初始化
    QString dbPath = QCoreApplication::applicationDirPath() + "/inspiration.db";
    if (!DatabaseManager::instance().init(dbPath)) return -1;
    HttpServer::instance().start(23333);

    // 激活检查
    QVariantMap trialStatus = DatabaseManager::instance().getTrialStatus();
    if (trialStatus["expired"].toBool() || trialStatus["usage_limit_reached"].toBool()) {
        ActivationDialog dlg("请激活软件");
        if (dlg.exec() != QDialog::Accepted) return 0;
    }

    QuickWindow* quickWin = new QuickWindow();
    quickWin->showAuto();
    FloatingBall* ball = new FloatingBall();
    a.setWindowIcon(FloatingBall::generateBallIcon());

    MainWindow* mainWin = nullptr;
    auto showMainWindow = [&]() {
        if (!mainWin) {
            mainWin = new MainWindow();
        }
        mainWin->showNormal();
        mainWin->activateWindow();
        mainWin->raise();
    };

    KeyboardHook::instance().start();
    MessageCaptureHandler::instance().init();
    HotkeyManager::instance().reapplyHotkeys();
    ReminderService::instance().start();

    // 托盘与系统连接 (省略部分 UI 细节以保持逻辑完整)
    SystemTray* tray = new SystemTray(&a);
    QObject::connect(tray, &SystemTray::showMainWindow, showMainWindow);
    tray->show();

    // 剪贴板核心逻辑
    QObject::connect(&ClipboardMonitor::instance(), &ClipboardMonitor::newContentDetected, 
        [=](const QString& content, const QString& type, const QByteArray& data, const QString& app, const QString& stitle){
        int catId = DatabaseManager::instance().isAutoCategorizeEnabled() ? DatabaseManager::instance().activeCategoryId() : -1;
        QString title;
        QString finalType = type;

        if (type == "image") {
            title = "[截图] " + QDateTime::currentDateTime().toString("MMdd_HHmm");
        } else if (type == "file") {
            QStringList files = content.split(";", Qt::SkipEmptyParts);
            if (files.size() > 0) {
                QFileInfo info(files.first());
                if (files.size() > 1) {
                    title = QString("Copied %1 Files").arg(files.size());
                    QSet<QString> exts;
                    for (const QString& f : files) {
                        QFileInfo fi(f);
                        exts.insert(fi.isDir() ? "[dir]" : fi.suffix().toLower());
                    }
                    if (exts.size() >= 2) finalType = "multiple";
                    else if (exts.size() == 1 && exts.contains("psd")) finalType = "psd";
                } else {
                    title = "File: " + info.fileName();
                    if (info.suffix().toLower() == "psd") finalType = "psd";
                }
            }
        } else {
            title = content.trimmed().left(40).simplified();
            if (title.isEmpty()) title = "无标题灵感";
        }

        DatabaseManager::instance().addNoteAsync(title, content, {}, "", catId, finalType, data, app, stitle);
    });

    int result = a.exec();
    DatabaseManager::instance().closeAndPack();
    return result;
}
