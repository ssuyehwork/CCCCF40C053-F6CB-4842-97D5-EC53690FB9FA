#include "ClipboardMonitor.h"
#include <QMimeData>
#include <QDebug>
#include <QApplication>
#include <QImage>
#include <QBuffer>
#include <QUrl>
#include <QFileInfo>
#include <QDir>
#include <QCryptographicHash>
#include <QTimer>

#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

ClipboardMonitor& ClipboardMonitor::instance() {
    static ClipboardMonitor inst;
    return inst;
}

ClipboardMonitor::ClipboardMonitor(QObject* parent) : QObject(parent) {
    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this, &ClipboardMonitor::onClipboardChanged);
    qDebug() << "[ClipboardMonitor] 初始化完成，开始监听...";
}

void ClipboardMonitor::skipNext() {
    m_skipNext = true;
    // [CRITICAL] 增加 2 秒自动过期逻辑。
    // 如果系统模拟复制操作失败（如目标应用卡死），dataChanged 信号将永远不会触发。
    // 此时若不重置标志位，用户手动进行的下一次复制也会被永久忽略。
    QTimer::singleShot(2000, this, [this]() {
        if (m_skipNext) {
            m_skipNext = false;
            qDebug() << "[ClipboardMonitor] skipNext 标志位因超时已自动重置";
        }
    });
}

void ClipboardMonitor::onClipboardChanged() {
    // [CRITICAL] 只要剪贴板发生变化就触发信号（用于烟花特效），不等待后续过滤
    emit clipboardChanged();

    bool forced = m_forceNext;
    QString forcedType = m_forcedType;
    m_forceNext = false;
    m_forcedType = "";

    if (m_skipNext || m_ignore) {
        m_skipNext = false;
        return;
    }

    // 抓取来源窗口信息 (对标 Ditto)
    QString sourceApp = forced ? "RapidNotes (内建工具)" : "未知应用";
    QString sourceTitle = "未知窗口";

    // 1. 过滤本程序自身的复制 (通过进程 ID 判定，比 activeWindow 更可靠)
#ifdef Q_OS_WIN
    HWND hwnd = GetForegroundWindow();
    if (hwnd) {
        DWORD processId;
        GetWindowThreadProcessId(hwnd, &processId);
        
        // 既然已经拿到了 HWND 和 PID，直接抓取标题和应用名，避免重复调用系统 API
        wchar_t title[512];
        if (GetWindowTextW(hwnd, title, 512)) {
            sourceTitle = QString::fromWCharArray(title);
        }

        // [CRITICAL] 过滤逻辑精细化：仅针对主窗口和极速窗口的常规复制操作进行拦截（防止回环）。
        // 如果开启了 forced (forceNext)，说明是内建工具的主动行为，必须予以记录，无论当前窗口是谁。
        if (processId == GetCurrentProcessId()) {
            if (!forced) {
                // [NOTE] 如果没有强制标志，且活跃窗口是主界面，则判定为需要过滤的内部回环
                if (sourceTitle == "RapidNotes" || sourceTitle == "快速笔记") {
                    return;
                }
            } else {
                // 如果是强制记录，确保来源 App 显示为内建工具
                sourceApp = "RapidNotes (内建工具)";
            }
        }

        HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processId);
        if (hProcess) {
            wchar_t exePath[MAX_PATH];
            if (GetModuleFileNameExW(hProcess, NULL, exePath, MAX_PATH)) {
                sourceApp = QFileInfo(QString::fromWCharArray(exePath)).baseName();
            }
            CloseHandle(hProcess);
        }
    }
#else
    QWidget* activeWin = QApplication::activeWindow();
    if (activeWin && !forced) {
        QString title = activeWin->windowTitle();
        if (title == "RapidNotes" || title == "快速笔记") {
            return;
        }
    }
#endif

    const QMimeData* mimeData = QGuiApplication::clipboard()->mimeData();
    if (!mimeData) return;

    QString type;
    QString content;
    QByteArray dataBlob;

    // 优先级 1: 本地文件
    if (mimeData->hasUrls()) {
        QStringList paths;
        for (const QUrl& url : mimeData->urls()) {
            if (url.isLocalFile()) {
                paths << QDir::toNativeSeparators(url.toLocalFile());
            }
        }
        if (!paths.isEmpty()) {
            type = "file";
            content = paths.join(";");
        }
    }

    // 优先级 2: 截图 (仅当不是文件时)
    if (type.isEmpty() && mimeData->hasImage()) {
        QImage img = qvariant_cast<QImage>(mimeData->imageData());
        if (!img.isNull()) {
            type = "image";
            content = "[截图]";
            QBuffer buffer(&dataBlob);
            buffer.open(QIODevice::WriteOnly);
            img.save(&buffer, "PNG");
        }
    }

    // 优先级 3: 文本 (包括网页链接)
    if (type.isEmpty() && mimeData->hasText() && !mimeData->text().trimmed().isEmpty()) {
        type = "text";
        content = mimeData->text();
    }

    // 如果都没有识别出来，则忽略
    if (type.isEmpty()) return;

    // [CRITICAL] 如果指定了强制类型，则覆盖，用于区分普通文本与截图识别出的文字
    if (forced && !forcedType.isEmpty()) {
        type = forcedType;
    }

    // SHA256 去重
    QByteArray hashData = dataBlob.isEmpty() ? content.toUtf8() : dataBlob;
    QString currentHash = QCryptographicHash::hash(hashData, QCryptographicHash::Sha256).toHex();
    
    if (currentHash == m_lastHash) return;
    m_lastHash = currentHash;

    qDebug() << "[ClipboardMonitor] 捕获新内容 (来自:" << sourceApp << "):" << type << content.left(30);
    
    emit newContentDetected(content, type, dataBlob, sourceApp, sourceTitle);
}