#ifndef STRINGUTILS_H
#define STRINGUTILS_H

#include <QString>
#include <QTextDocument>
#include <QMimeData>
#include <QClipboard>
#include <QApplication>
#include <QRegularExpression>
#include <QSettings>
#include <QVariantList>
#include <QUrl>
#include <QDir>
#include <QProcess>
#include <vector>
#include "../core/ClipboardMonitor.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

class StringUtils {
public:
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
    static bool isHtml(const QString& text) {
        return text.contains("<!DOCTYPE HTML") || text.contains("<html>") || text.contains("<style");
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
