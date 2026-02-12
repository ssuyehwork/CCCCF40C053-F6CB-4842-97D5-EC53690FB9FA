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
#include <QPainter>
#include <QFont>
#include <QPen>
#include <QBrush>
#include <QRect>
#include <QPoint>
#include <QColor>
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
    static inline void smartSplitLanguage(const QString& text, QString& title, QString& content) {
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
    static inline bool containsChinese(const QString& text) {
        static QRegularExpression chineseRegex("[\\x{4e00}-\\x{9fa5}]+");
        return text.contains(chineseRegex);
    }

    /**
     * @brief 偶数行配对拆分：每两行为一组
     * 规则：含中文的行为标题，若同语种则第一行为标题。
     */
    static inline QList<QPair<QString, QString>> smartSplitPairs(const QString& text) {
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
    static inline bool isHtml(const QString& text) {
        return text.contains("<!DOCTYPE HTML") || text.contains("<html>") || text.contains("<style");
    }

    static inline QString htmlToPlainText(const QString& html) {
        if (!isHtml(html)) return html;
        QTextDocument doc;
        doc.setHtml(html);
        return doc.toPlainText();
    }

    static inline void copyNoteToClipboard(const QString& content) {
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
    static inline QString convertChineseVariant(const QString& text, bool toSimplified) {
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
    static inline QString getToolTipStyle() {
        // [MASTER] 极致一致性：QToolTip 与 drawTipBox 必须完全对等
        return "QToolTip { background: #2B2B2B; color: white; border: 1px solid #B0B0B0; border-radius: 4px; padding: 6px 10px; font-family: 'Segoe UI', 'Microsoft YaHei'; font-size: 12px; } "
               "QToolTip QLabel { background: transparent; border: none; }";
    }

    /**
     * @brief 强制回归纯文本，严禁 HTML 标签干扰渲染
     */
    static inline QString wrapToolTip(const QString& text) {
        QString t = text;
        t.remove(QRegularExpression("<[^>]*>"));
        return t.trimmed();
    }

    /**
     * @brief 统一绘图层 Tip 渲染函数，作为全项目的唯一标准
     */
    static inline void drawTipBox(QPainter& p, const QRect& winRect, const QPoint& pos, const QString& text, bool centered = false) {
        p.save();
        p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);

        QFont font("Microsoft YaHei");
        font.setPixelSize(12);
        font.setBold(false);
        p.setFont(font);

        // 计算文本区域
        QRect boundingRect = p.fontMetrics().boundingRect(0, 0, 400, 200, Qt::AlignCenter | Qt::TextWordWrap, text);
        int w = boundingRect.width() + 20; // 10px * 2
        int h = boundingRect.height() + 12; // 6px * 2

        // 计算位置
        QRect r;
        if (centered) {
            r = QRect(pos.x() - w/2, pos.y() - h/2, w, h);
        } else {
            // 默认显示在鼠标上方偏左，避开指针
            r = QRect(pos.x() - w/2, pos.y() - h - 25, w, h);
        }

        // 边界限制
        if (r.right() > winRect.width()) r.moveRight(winRect.width() - 10);
        if (r.left() < 0) r.moveLeft(10);
        if (r.bottom() > winRect.height()) r.moveBottom(winRect.height() - 10);
        if (r.top() < 0) r.moveTop(10);

        // [极致一致性] 完全对齐 image.png：纯黑底、细灰边框、微弱阴影
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 120));
        p.drawRoundedRect(r.adjusted(1, 1, 1, 1), 4, 4); // 绘制阴影层

        p.setPen(QPen(QColor(100, 100, 100), 1)); // 细灰边框
        p.setBrush(Qt::black); // 纯黑背景
        p.drawRoundedRect(r, 4, 4);

        // 绘制文字 (强制使用 Segoe UI/微软雅黑)
        p.setPen(Qt::white);
        p.drawText(r, Qt::AlignCenter | Qt::TextWordWrap, text);
        p.restore();
    }

    /**
     * @brief 记录最近访问或使用的分类
     */
    static inline void recordRecentCategory(int catId) {
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
    static inline QVariantList getRecentCategories() {
        QSettings settings("RapidNotes", "QuickWindow");
        return settings.value("recentCategories").toList();
    }
};

#endif // STRINGUTILS_H
