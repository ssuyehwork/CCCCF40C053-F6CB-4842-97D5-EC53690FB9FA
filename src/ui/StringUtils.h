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
#include <vector>
#include "../core/ClipboardMonitor.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

class StringUtils {
public:
    /**
     * @brief 智能语言拆分：中文作为标题，非中文作为内容
     */
    static void smartSplitLanguage(const QString& text, QString& title, QString& content) {
        QString trimmedText = text.trimmed();
        if (trimmedText.isEmpty()) {
            title = "新笔记";
            content = "";
            return;
        }

        // 匹配中文字符范围
        static QRegularExpression chineseRegex("[\\x{4e00}-\\x{9fa5}]+");
        // 匹配非中文且非空白非标点的字符（识别泰文、英文等）
        static QRegularExpression otherRegex("[^\\x{4e00}-\\x{9fa5}\\s\\p{P}]+");

        bool hasChinese = trimmedText.contains(chineseRegex);
        bool hasOther = trimmedText.contains(otherRegex);

        if (hasChinese && hasOther) {
            // 提取所有中文块作为标题
            QStringList chineseBlocks;
            QRegularExpressionMatchIterator i = chineseRegex.globalMatch(trimmedText);
            while (i.hasNext()) {
                chineseBlocks << i.next().captured();
            }
            title = chineseBlocks.join(" ").simplified();
            if (title.isEmpty()) title = "未命名";

            // 移除中文块后的剩余部分作为内容
            QString remaining = trimmedText;
            remaining.replace(chineseRegex, " ");
            content = remaining.simplified();
            
            // 如果拆分后内容为空（例如全是标点），则保留全文
            if (content.isEmpty()) content = trimmedText;
        } else {
            // 单一语种或无法识别：首行作为标题，全文作为内容
            QStringList lines = trimmedText.split('\n', Qt::SkipEmptyParts);
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
     * @brief 智能识别语言：判断文本是否包含中文
     */
    static bool containsChinese(const QString& text) {
        static QRegularExpression chineseRegex("[\\x{4e00}-\\x{9fa5}]+");
        return text.contains(chineseRegex);
    }

    /**
     * @brief 偶数行配对拆分：每两行为一组
     * 规则：含中文的行为标题，若同语种则第一行为标题。
     */
    static QList<QPair<QString, QString>> smartSplitPairs(const QString& text) {
        QList<QPair<QString, QString>> results;
        QStringList lines = text.split('\n', Qt::SkipEmptyParts);
        
        if (lines.isEmpty()) return results;

        // 如果是偶数行，执行配对逻辑
        if (lines.size() > 0 && lines.size() % 2 == 0) {
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
                    // 同语种，第一行为标题
                    results.append({line1, line2});
                }
            }
        } else {
            // 奇数行或单行，沿用之前的单条逻辑
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
     * @brief 获取全局统一的 ToolTip QSS 样式字符串
     */
    static QString getToolTipStyle() {
        // [CRITICAL] 核心修复：将 QToolTip 背景设为透明并移除边框。
        // 这是为了解决 Qt 原生 ToolTip 窗口在应用 border-radius 时出现的“直角背景溢出”问题。
        // 实际的圆角背景和边框将通过 wrapToolTip 中的 HTML 容器实现。
        return "QToolTip { color: #ffffff; background-color: transparent; border: none; padding: 0px; }";
    }

    /**
     * @brief 包装 ToolTip 为富文本格式，强制触发 QSS 样式渲染并实现圆角背景
     */
    static QString wrapToolTip(const QString& text) {
        if (text.isEmpty()) return text;
        // [CRITICAL] 深度修复：检查是否已经包装过。使用特征标记防止重复包装。
        if (text.contains("id='qtooltip_container'")) return text;

        QString content = text;
        // 如果输入已包含 <html>，提取其内部内容以便重新在 div 中包装
        if (content.startsWith("<html>")) {
            content.remove("<html>");
            content.remove("</html>");
        }

        // [CRITICAL] 使用带有特定 id 的 div 容器实现圆角、背景和边框。
        // 通过 QSS 将原生 QToolTip 背景设为透明，再由此容器承载视觉效果，彻底解决直角溢出。
        return QString("<html><div id='qtooltip_container' style='background-color: #2D2D2D; border: 1px solid #555555; border-radius: 6px; padding: 5px 10px; color: #ffffff; white-space:nowrap;'>%1</div></html>").arg(content);
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
};

#endif // STRINGUTILS_H
