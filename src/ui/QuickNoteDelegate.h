#ifndef QUICKNOTEDELEGATE_H
#define QUICKNOTEDELEGATE_H

#include <QStyledItemDelegate>
#include <QPainter>
#include <QDateTime>
#include "../models/NoteModel.h"
#include "IconHelper.h"
#include "QuickWindow.h"
#include "ToolTipOverlay.h"
#include <QHelpEvent>

class QuickNoteDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit QuickNoteDelegate(QObject* parent = nullptr) : QStyledItemDelegate(parent) {}

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        return QSize(option.rect.width(), 45); // 紧凑型高度
    }

    bool helpEvent(QHelpEvent* event, QAbstractItemView* view, const QStyleOptionViewItem& option, const QModelIndex& index) override {
        if (event && event->type() == QEvent::ToolTip && index.isValid()) {
            QString tip = index.data(Qt::ToolTipRole).toString();
            if (!tip.isEmpty()) {
                // 2026-03-xx 按照用户要求，列表数据 ToolTip 持续时间设为 2 秒 (2000ms)
                ToolTipOverlay::instance()->showText(event->globalPos(), tip, 2000);
                return true;
            }
        }
        return QStyledItemDelegate::helpEvent(event, view, option, index);
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        if (!index.isValid()) return;

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);

        QRect rect = option.rect;
        bool isSelected = (option.state & QStyle::State_Selected);
        bool isHovered = (option.state & QStyle::State_MouseOver);

        // 1. 绘制基础背景 (斑马纹)
        QColor bgColor = (index.row() % 2 == 0) ? QColor("#1E1E1E") : QColor("#181818");
        painter->fillRect(rect, bgColor);

        // 2. 绘制高亮背景 (选中或悬停) - 按照用户要求及宪法规范：4px 圆角
        if (isSelected || isHovered) {
            QColor highlightColor("#4a90e2"); // 默认蓝
            QuickWindow* win = qobject_cast<QuickWindow*>(parent());
            if (win) {
                QString c = win->currentCategoryColor();
                if (!c.isEmpty() && QColor::isValidColorName(c)) highlightColor = QColor(c);
            }

            QColor bg = isSelected ? highlightColor : QColor(255, 255, 255);
            bg.setAlpha(isSelected ? 30 : 20); // 选中态稍深，悬停态稍浅

            // 宪法规范：padding 2px 4px, margin 1px 2px
            QRect highlightRect = rect.adjusted(2, 1, -2, -1);
            painter->setBrush(bg);
            painter->setPen(Qt::NoPen);
            painter->drawRoundedRect(highlightRect, 4, 4);
        }

        // 3. 绘制指示条 (仅置顶状态)
        bool isPinned = index.data(NoteModel::PinnedRole).toBool();
        if (isPinned) {
            // 2026-03-12 按照用户要求，统一置顶指示条颜色为橙色 (#FF551C)
            painter->fillRect(QRect(rect.left(), rect.top(), 2, rect.height()), QColor("#FF551C"));
        }

        // 2. 分隔线 (对齐 Python 版，使用极浅的黑色半透明)
        painter->setPen(QColor(0, 0, 0, 25));
        painter->drawLine(rect.bottomLeft(), rect.bottomRight());

        // 图标 (DecorationRole) - 中心点对齐在 23px
        QIcon icon = index.data(Qt::DecorationRole).value<QIcon>();
        if (!icon.isNull()) {
            QString type = index.data(NoteModel::TypeRole).toString();
            if (type == "image") {
                // 缩略图 (32x32): 起始 7px + 32/2 = 23px 中心
                icon.paint(painter, rect.left() + 7, rect.top() + (rect.height() - 32) / 2, 32, 32);
            } else {
                // SVG图标 (20x20): 起始 13px + 20/2 = 23px 中心
                icon.paint(painter, rect.left() + 13, rect.top() + (rect.height() - 20) / 2, 20, 20);
            }
        }

        // 标题文本与后缀名处理
        QString text = index.data(NoteModel::TitleRole).toString();
        QString type = index.data(NoteModel::TypeRole).toString();
        QString content = index.data(NoteModel::ContentRole).toString();
        
        if (type == "file" || type == "files") {
            QStringList paths = content.split(';', Qt::SkipEmptyParts);
            if (!paths.isEmpty()) {
                QString firstExt = QFileInfo(paths.first().trimmed()).suffix().toUpper();
                bool sameExt = true;
                for (const QString& p : paths) {
                    if (QFileInfo(p.trimmed()).suffix().toUpper() != firstExt) {
                        sameExt = false;
                        break;
                    }
                }
                if (sameExt && !firstExt.isEmpty()) {
                    text = QString("[%1] %2").arg(firstExt, text);
                }
            }
        }

        painter->setPen(isSelected ? Qt::white : QColor("#CCCCCC"));
        painter->setFont(QFont("Microsoft YaHei", 9));
        
        // 调整右侧边距 (-70) 以避开右侧的时间戳和星级
        QRect textRect = rect.adjusted(40, 0, -70, 0);
        painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, 
                         painter->fontMetrics().elidedText(text, Qt::ElideRight, textRect.width()));

        // 文件数量数字显示 (#FF4858)
        if (type == "file" || type == "files") {
            QStringList paths = content.split(';', Qt::SkipEmptyParts);
            if (paths.size() > 1) {
                painter->save();
                painter->setPen(QColor("#FF4858"));
                // 对齐时间戳字体：Segoe UI, 7pt, 常规粗细
                painter->setFont(QFont("Segoe UI", 7));
                // 在时间戳下方 (y=15 左右) 绘制
                painter->drawText(rect.adjusted(0, 15, -10, 0), Qt::AlignRight | Qt::AlignTop, QString::number(paths.size()));
                painter->restore();
            }
        }

        // 时间 (极简展示) - 显示在右上方
        QString timeStr = index.data(NoteModel::TimeRole).toDateTime().toString("MM-dd HH:mm");
        painter->setPen(QColor("#666666"));
        painter->setFont(QFont("Segoe UI", 7));
        painter->drawText(rect.adjusted(0, 3, -10, 0), Qt::AlignRight | Qt::AlignTop, timeStr);

        // 星级 (Rating) - 显示在右下方 (仅显示实心星)
        int rating = index.data(NoteModel::RatingRole).toInt();
        if (rating > 0) {
            int starSize = 9;
            int spacing = 1;
            // 限制最大星级为 5
            int displayRating = qMin(rating, 5);
            int totalWidth = displayRating * starSize + (displayRating - 1) * spacing;
            int startX = rect.right() - 9 - totalWidth;
            int startY = rect.bottom() - starSize - 5;

            QIcon starFilled = IconHelper::getIcon("star_filled", "#F1C40F", starSize);

            for (int i = 0; i < displayRating; ++i) {
                QRect starRect(startX + i * (starSize + spacing), startY, starSize, starSize);
                starFilled.paint(painter, starRect);
            }
        }

        painter->restore();
    }
};

#endif // QUICKNOTEDELEGATE_H
