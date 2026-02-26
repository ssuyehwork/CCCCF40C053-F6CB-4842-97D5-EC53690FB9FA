#ifndef QUICKNOTEDELEGATE_H
#define QUICKNOTEDELEGATE_H

#include <QStyledItemDelegate>
#include <QPainter>
#include <QDateTime>
#include "../models/NoteModel.h"
#include "IconHelper.h"
#include "QuickWindow.h"

class QuickNoteDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit QuickNoteDelegate(QObject* parent = nullptr) : QStyledItemDelegate(parent) {}

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        return QSize(option.rect.width(), 45); // 紧凑型高度
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        if (!index.isValid()) return;

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);

        QRect rect = option.rect;
        bool isSelected = (option.state & QStyle::State_Selected);
        bool isHovered = (option.state & QStyle::State_MouseOver);

        // 1. 绘制基础背景 (斑马纹对比度微调)
        QColor bgColor = (index.row() % 2 == 0) ? QColor("#1E1E1E") : QColor("#181818");
        if (isHovered && !isSelected) {
            bgColor = QColor(255, 255, 255, 20);
        }
        painter->fillRect(rect, bgColor);

        // 2. 绘制指示条 (根据置顶状态与选中状态动态调整)
        bool isPinned = index.data(NoteModel::PinnedRole).toBool();
        if (isPinned) {
            // 置顶项：在最左侧固定绘制 1px 红色条
            painter->fillRect(QRect(rect.left(), rect.top(), 1, rect.height()), QColor("#FF0000"));
        }

        if (isSelected) {
            // 只有在选中状态下才计算分类颜色
            QColor highlightColor("#4a90e2"); // 默认蓝
            QuickWindow* win = qobject_cast<QuickWindow*>(parent());
            if (win) {
                QString c = win->currentCategoryColor();
                if (!c.isEmpty() && QColor::isValidColorName(c)) {
                    highlightColor = QColor(c);
                }
            }

            if (isPinned) {
                // 置顶项被选中：在红条右侧绘制 4px 分类指示色
                painter->fillRect(QRect(rect.left() + 1, rect.top(), 4, rect.height()), highlightColor);
            } else {
                // 未置顶但已选中：绘制完整的 5px 分类指示色
                painter->fillRect(QRect(rect.left(), rect.top(), 5, rect.height()), highlightColor);
            }

            // 3. 选中项背景叠加层 (约 6% 不透明度)
            QColor overlay = highlightColor;
            overlay.setAlpha(15); 
            painter->fillRect(rect, overlay);
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

        // 标题文本 (根据用户要求，QuickWindow 仅显示笔记标题)
        QString text = index.data(NoteModel::TitleRole).toString();
        painter->setPen(isSelected ? Qt::white : QColor("#CCCCCC"));
        painter->setFont(QFont("Microsoft YaHei", 9));
        
        // 调整右侧边距 (-70) 以避开右侧的时间戳和星级
        QRect textRect = rect.adjusted(40, 0, -70, 0);
        painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, 
                         painter->fontMetrics().elidedText(text, Qt::ElideRight, textRect.width()));

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
