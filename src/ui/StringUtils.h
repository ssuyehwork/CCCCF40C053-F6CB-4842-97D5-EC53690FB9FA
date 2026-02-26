#ifndef STRINGUTILS_H
#define STRINGUTILS_H

#include <QString>
#include <QTextDocument>
#include <QMimeData>
#include <QClipboard>
#include <QApplication>
#include <QRegularExpression>
#include <QSettings>
#include <QStringList>
#include <QVariantList>
#include <QUrl>
#include <QDir>
#include <QProcess>
#include <QDateTime>
#include <QFileInfo>
#include <QDebug>
#include <vector>
#include <functional>
#include "../core/ClipboardMonitor.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

class StringUtils {
#ifdef Q_OS_WIN
    // [NEW] 基于事件驱动的浏览器检测缓存与回调
    inline static bool m_browserCacheValid = false;
    inline static bool m_isBrowserActiveCache = false;
    inline static std::function<void(bool)> m_focusCallback = nullptr;

    static void CALLBACK WinEventProc(HWINEVENTHOOK, DWORD event, HWND, LONG, LONG, DWORD, DWORD) {
        if (event == EVENT_SYSTEM_FOREGROUND) {
            m_browserCacheValid = false; // 前台窗口切换，失效缓存
            bool active = isBrowserActive(); 
            qDebug() << "[StringUtils] 前台窗口切换 -> 浏览器激活状态:" << active;
            if (m_focusCallback) m_focusCallback(active);
        }
    }
#endif

public:
    static QString getToolTipStyle() {
        return "QToolTip { color: #ffffff; background-color: #2D2D2D; border: 1px solid #555555; border-radius: 6px; padding: 5px 10px; }";
    }

    static QString wrapToolTip(const QString& text) {
        if (text.isEmpty()) return text;
        if (text.startsWith("<html>")) return text;
        return QString("<html><span style='white-space:nowrap;'>%1</span></html>").arg(text);
    }

    /**
     * @brief 注册焦点变化回调 (用于动态管理系统热键)
     */
    static void setFocusCallback(std::function<void(bool)> cb) {
#ifdef Q_OS_WIN
        m_focusCallback = cb;
#endif
    }

    /**
     * @brief 判定当前活跃窗口是否为浏览器 (基于 WinEventHook 驱动的高效缓存与 HWND 即时校验)
     */
    static bool isBrowserActive() {
#ifdef Q_OS_WIN
        static bool hookInstalled = false;
        if (!hookInstalled) {
            // 监听前台窗口切换事件
            SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, NULL, 
                           WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);
            hookInstalled = true;
            qDebug() << "[StringUtils] WinEventHook (Foreground) 已安装";
        }

        HWND hwnd = GetForegroundWindow();
        static HWND lastHwnd = nullptr;

        // 如果窗口句柄没变且缓存有效，直接返回结果
        if (m_browserCacheValid && hwnd == lastHwnd) {
            return m_isBrowserActiveCache;
        }

        lastHwnd = hwnd;
        m_isBrowserActiveCache = false;
        
        if (hwnd) {
            DWORD pid;
            GetWindowThreadProcessId(hwnd, &pid);
            
            // 尝试获取进程路径 (优先使用受限访问权限以提高成功率)
            HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid);
            if (!process) process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
            
            if (process) {
                wchar_t buffer[MAX_PATH];
                if (GetModuleFileNameExW(process, NULL, buffer, MAX_PATH)) {
                    QString exePath = QString::fromWCharArray(buffer).toLower();
                    QString exeName = QFileInfo(exePath).fileName();

                    static QStringList browserExes;
                    static qint64 lastLoadTime = 0;
                    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();

                    // 浏览器进程列表配置缓存 (5秒刷新一次)
                    if (currentTime - lastLoadTime > 5000 || browserExes.isEmpty()) {
                        QSettings acquisitionSettings("RapidNotes", "Acquisition");
                        browserExes = acquisitionSettings.value("browserExes").toStringList();
                        if (browserExes.isEmpty()) {
                            browserExes = {
                                "chrome.exe", "msedge.exe", "firefox.exe", "brave.exe", 
                                "opera.exe", "iexplore.exe", "vivaldi.exe", "safari.exe",
                                "arc.exe", "sidekick.exe", "maxthon.exe", "thorium.exe",
                                "librewolf.exe", "waterfox.exe"
                            };
                        }
                        lastLoadTime = currentTime;
                    }
                    m_isBrowserActiveCache = browserExes.contains(exeName, Qt::CaseInsensitive);
                    qDebug() << "[StringUtils] 活性检测 -> 进程:" << exeName << "是浏览器:" << m_isBrowserActiveCache;
                }
                CloseHandle(process);
            } else {
                qDebug() << "[StringUtils] 无法访问进程 (PID:" << pid << ")";
            }
        }

        m_browserCacheValid = true;
        return m_isBrowserActiveCache;
#else
        return false;
#endif
    }

    /**
     * @brief 判定文本是否包含非中文、非空白、非标点的“第二门语言”字符
     */
    static bool containsOtherLanguage(const QString& text) {
        static QRegularExpression otherLangRegex(R"([^\s\p{P}\x{4e00}-\x{9fa5}\x{3400}-\x{4dbf}\x{f900}-\x{faff}]+)");
        return text.contains(otherLangRegex);
    }

    /**
     * @brief 智能识别语言：判断文本是否包含中文 (扩展匹配范围以提高准确度)
     */
    static bool containsChinese(const QString& text) {
        // [OPTIMIZED] 扩展 CJK 范围，包含基本汉字及扩展区，确保如泰语+中文组合时能精准识别
        static QRegularExpression chineseRegex("[\\x{4e00}-\\x{9fa5}\\x{3400}-\\x{4dbf}\\x{f900}-\\x{faff}]+");
        return text.contains(chineseRegex);
    }

    /**
     * @brief 判断文本是否包含泰文
     */
    static bool containsThai(const QString& text) {
        static QRegularExpression thaiRegex("[\\x{0e00}-\\x{0e7f}]+");
        return text.contains(thaiRegex);
    }

    /**
     * @brief 智能语言拆分：中文作为标题，非中文作为内容 (增强单行及混合语言处理)
     */
    static void smartSplitLanguage(const QString& text, QString& title, QString& content) {
        QString trimmedText = text.trimmed();
        if (trimmedText.isEmpty()) {
            title = "新笔记";
            content = "";
            return;
        }

        static QRegularExpression chineseRegex("[\\x{4e00}-\\x{9fa5}\\x{3400}-\\x{4dbf}\\x{f900}-\\x{faff}]+");
        
        bool hasChinese = containsChinese(trimmedText);
        bool hasOther = containsOtherLanguage(trimmedText);

        if (hasChinese && hasOther) {
            // [CRITICAL] 混合语言拆分逻辑：提取所有中文块作为标题
            QStringList chineseBlocks;
            QRegularExpressionMatchIterator i = chineseRegex.globalMatch(trimmedText);
            while (i.hasNext()) {
                chineseBlocks << i.next().captured();
            }
            title = chineseBlocks.join(" ").simplified();

            // 移除中文块后的剩余部分作为正文内容 (保留原有外语结构)
            QString remaining = trimmedText;
            remaining.replace(chineseRegex, " ");
            content = remaining.simplified();
            
            if (title.isEmpty()) title = "未命名灵感";
            if (content.isEmpty()) content = trimmedText;
        } else {
            // 单一语种：首行作为标题，全文作为内容
            QStringList lines = trimmedText.split(QRegularExpression("[\\r\\n]+"), Qt::SkipEmptyParts);
            if (!lines.isEmpty()) {
                title = lines[0].trimmed();
                if (title.length() > 60) title = title.left(57) + "...";
                content = trimmedText;
            } else {
                title = "新笔记";
                content = trimmedText;
            }
        }
    }

    /**
     * @brief 增强版配对拆分：支持偶数行配对、单行拆分及多行混合拆分
     */
    static QList<QPair<QString, QString>> smartSplitPairs(const QString& text) {
        QList<QPair<QString, QString>> results;
        QStringList lines = text.split(QRegularExpression("[\\r\\n]+"), Qt::SkipEmptyParts);
        if (lines.isEmpty()) return results;

        // [NEW] 检测是否每一行本身就是混合双语（如：Thai Chinese）
        bool allLinesMixed = true;
        for (const QString& line : lines) {
            if (!(containsChinese(line) && containsOtherLanguage(line))) {
                allLinesMixed = false;
                break;
            }
        }

        // 如果每一行都是混合的，则按行独立创建笔记
        if (allLinesMixed && lines.size() > 1) {
            for (const QString& line : lines) {
                QString t, c;
                smartSplitLanguage(line, t, c);
                results.append({t, c});
            }
            return results;
        }

        // 偶数行配对拆分：每两行为一组，中文优先级策略
        if (lines.size() > 1 && lines.size() % 2 == 0) {
            for (int i = 0; i < lines.size(); i += 2) {
                QString line1 = lines[i].trimmed();
                QString line2 = lines[i+1].trimmed();
                
                bool c1 = containsChinese(line1);
                bool c2 = containsChinese(line2);
                
                if (c1 && !c2) {
                    results.append({line1, line2});
                } else if (!c1 && c2) {
                    results.append({line2, line1});
                } else {
                    results.append({line1, line2});
                }
            }
        } else {
            // 单文本块或奇数行：使用智能拆分逻辑
            QString title, content;
            smartSplitLanguage(text, title, content);
            results.append({title, content});
        }
        
        return results;
    }

public:
    static bool isRichText(const QString& text) {
        QString trimmed = text.trimmed();
        return trimmed.startsWith("<!DOCTYPE", Qt::CaseInsensitive) || 
               trimmed.startsWith("<html", Qt::CaseInsensitive) || 
               trimmed.contains("<style", Qt::CaseInsensitive) ||
               Qt::mightBeRichText(text);
    }

    static bool isHtml(const QString& text) {
        return isRichText(text);
    }

    static QString htmlToPlainText(const QString& html) {
        if (!isHtml(html)) return html;
        QTextDocument doc;
        doc.setHtml(html);
        return doc.toPlainText();
    }

    static void copyNoteToClipboard(const QString& content) {
        ClipboardMonitor::instance().skipNext();
        QMimeData* mimeData = new QMimeData();
        if (isHtml(content)) {
            mimeData->setHtml(content);
            mimeData->setText(htmlToPlainText(content));
        } else {
            mimeData->setText(content);
        }
        QApplication::clipboard()->setMimeData(mimeData);
    }

    /**
     * @brief 简繁转换 (利用 Windows 原生 API)
     * @param toSimplified true 为转简体，false 为转繁体
     */
    static QString convertChineseVariant(const QString& text, bool toSimplified) {
#ifdef Q_OS_WIN
        if (text.isEmpty()) return text;
        
        // 转换为宽字符
        std::wstring wstr = text.toStdWString();
        DWORD flags = toSimplified ? LCMAP_SIMPLIFIED_CHINESE : LCMAP_TRADITIONAL_CHINESE;
        
        // 第一次调用获取长度
        int size = LCMapStringEx(LOCALE_NAME_USER_DEFAULT, flags, wstr.c_str(), -1, NULL, 0, NULL, NULL, 0);
        if (size > 0) {
            std::vector<wchar_t> buffer(size);
            // 第二次调用执行转换
            LCMapStringEx(LOCALE_NAME_USER_DEFAULT, flags, wstr.c_str(), -1, buffer.data(), size, NULL, NULL, 0);
            return QString::fromWCharArray(buffer.data());
        }
#endif
        return text;
    }

    /**
     * @brief 记录最近访问或使用的分类
     */
    static void recordRecentCategory(int catId) {
        if (catId <= 0) return;
        QSettings settings("RapidNotes", "QuickWindow");
        QVariantList recentCats = settings.value("recentCategories").toList();
        
        // 转换为 int 列表方便操作
        QList<int> ids;
        for(const auto& v : recentCats) ids << v.toInt();
        
        ids.removeAll(catId);
        ids.prepend(catId);
        
        // 限制为最近 10 个
        while (ids.size() > 10) ids.removeLast();
        
        QVariantList result;
        for(int id : ids) result << id;
        settings.setValue("recentCategories", result);
        settings.sync();
    }

    /**
     * @brief 获取最近访问或使用的分类 ID 列表
     */
    static QVariantList getRecentCategories() {
        QSettings settings("RapidNotes", "QuickWindow");
        return settings.value("recentCategories").toList();
    }

    /**
     * [CRITICAL] 统一笔记预览 HTML 生成逻辑。
     * 1. 此函数为 MainWindow 预览卡片与 QuickPreview (空格预览) 的 Single Source of Truth。
     * 2. 若标题、内容、数据均为空，必须返回空字符串以消除视觉分割线。
     * 3. 修改此函数将同步影响全局预览效果，请务必保持两者视觉高度统一。
     */
    static QString generateNotePreviewHtml(const QString& title, const QString& content, const QString& type, const QByteArray& data) {
        if (title.isEmpty() && content.isEmpty() && data.isEmpty()) return "";

        QString titleHtml = QString("<h3 style='color: #eee; margin-bottom: 5px;'>%1</h3>").arg(title.toHtmlEscaped());
        QString hrHtml = "<hr style='border: 0; border-top: 1px solid #444; margin: 10px 0;'>";
        QString html;

        if (type == "color") {
            html = QString("%1%2"
                           "<div style='margin: 20px; text-align: center;'>"
                           "  <div style='background-color: %3; width: 100%; height: 200px; border-radius: 12px; border: 1px solid #555;'></div>"
                           "  <h1 style='color: white; margin-top: 20px; font-family: Consolas; font-size: 32px;'>%3</h1>"
                           "</div>")
                   .arg(titleHtml, hrHtml, content);
        } else if (type == "image" && !data.isEmpty()) {
            html = QString("%1%2<div style='text-align: center;'><img src='data:image/png;base64,%3' width='450'></div>")
                   .arg(titleHtml, hrHtml, QString(data.toBase64()));
        } else {
            QString body;
            if (isRichText(content)) {
                body = content;
            } else {
                body = content.toHtmlEscaped();
                body.replace("\n", "<br>");
                body = QString("<div style='line-height: 1.6; color: #ccc; font-size: 13px;'>%1</div>").arg(body);
            }
            html = QString("%1%2%3").arg(titleHtml, hrHtml, body);
        }
        return html;
    }

    /**
     * @brief 提取第一个网址，支持自动补全协议头
     */
    static QString extractFirstUrl(const QString& text) {
        if (text.isEmpty()) return "";
        // 支持识别纯文本或 HTML 中的 URL
        QString plainText = text.contains("<") ? htmlToPlainText(text) : text;
        static QRegularExpression urlRegex(R"((https?://[^\s<>"]+|www\.[^\s<>"]+))", QRegularExpression::CaseInsensitiveOption);
        QRegularExpressionMatch match = urlRegex.match(plainText);
        if (match.hasMatch()) {
            QString url = match.captured(1);
            if (url.startsWith("www.", Qt::CaseInsensitive)) url = "http://" + url;
            return url;
        }
        return "";
    }

    /**
     * @brief [NEW] 从 MimeData 中健壮地提取本地文件路径，支持 URL 列表和文本形式的 file:/// 链接
     */
    static QStringList extractLocalPathsFromMime(const QMimeData* mime) {
        QStringList paths;
        if (mime->hasUrls()) {
            for (const QUrl& url : mime->urls()) {
                if (url.isLocalFile()) {
                    paths << QDir::toNativeSeparators(url.toLocalFile());
                } else {
                    // 处理可能带有 file:/// 但未被 Qt 识别为 localFile 的情况 (如特殊字符未转码)
                    QString s = url.toString();
                    if (s.startsWith("file:///")) {
                        paths << QDir::toNativeSeparators(QUrl(s).toLocalFile());
                    }
                }
            }
        }
        
        // 如果 Urls 为空，尝试从 Text 中提取 (处理某些应用只提供文本形式路径的情况)
        if (paths.isEmpty() && mime->hasText()) {
            QString text = mime->text().trimmed();
            // 处理单行 file:///
            if (text.startsWith("file:///")) {
                paths << QDir::toNativeSeparators(QUrl(text).toLocalFile());
            } else {
                // 处理可能是物理绝对路径的情况
                QFileInfo info(text);
                if (info.exists() && info.isAbsolute()) {
                    paths << QDir::toNativeSeparators(text);
                }
            }
        }
        return paths;
    }

    /**
     * @brief [NEW] 启用 WS_MINIMIZEBOX 以支持任务栏最小化，启用 WS_THICKFRAME 以允许 Windows 响应 NCHITTEST 缩放指令
     */
    static void applyTaskbarMinimizeStyle(void* winId) {
#ifdef Q_OS_WIN
        HWND hwnd = (HWND)winId;
        LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
        // [CRITICAL] 必须包含 WS_THICKFRAME (即 WS_SIZEBOX)，否则系统会忽略 WM_NCHITTEST 返回的 HTLEFT/HTRIGHT 等缩放指令
        SetWindowLongPtr(hwnd, GWL_STYLE, style | WS_MINIMIZEBOX | WS_SYSMENU | WS_THICKFRAME);
#endif
    }

    /**
     * @brief 在资源管理器中定位路径，支持预处理
     */
    static void locateInExplorer(const QString& path, bool select = true) {
#ifdef Q_OS_WIN
        if (path.isEmpty()) return;
        // 使用 QUrl::fromUserInput 处理包含 file:/// 协议或 URL 编码字符的路径
        QString localPath = QUrl::fromUserInput(path).toLocalFile();
        if (localPath.isEmpty()) localPath = path;
        // 统一转换为系统原生路径格式
        localPath = QDir::toNativeSeparators(localPath);
        
        QStringList args;
        if (select) {
            args << "/select," << localPath;
        } else {
            args << localPath;
        }
        QProcess::startDetached("explorer.exe", args);
#endif
    }
};

#endif // STRINGUTILS_H
