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

        // 2. 绘制选中高亮 (仅左侧 5 像素指示条，颜色联动当前分类)
        if (isSelected) {
            QColor highlightColor("#4a90e2"); // 默认蓝
            QuickWindow* win = qobject_cast<QuickWindow*>(parent());
            if (win) {
                QString c = win->currentCategoryColor();
                if (!c.isEmpty() && QColor::isValidColorName(c)) {
                    highlightColor = QColor(c);
                }
            }

            // 绘制左侧 5px 指示条
            painter->fillRect(QRect(rect.left(), rect.top(), 5, rect.height()), highlightColor);
            
            // 选中背景增加更克制的叠加层 (约 6% 不透明度)，避免遮挡内容
            QColor overlay = highlightColor;
            overlay.setAlpha(15); 
            painter->fillRect(rect, overlay);
        }

        // 2. 分隔线 (对齐 Python 版，使用极浅的黑色半透明)
        painter->setPen(QColor(0, 0, 0, 25));
        painter->drawLine(rect.bottomLeft(), rect.bottomRight());

        // 图标 (DecorationRole)
        QIcon icon = index.data(Qt::DecorationRole).value<QIcon>();
        if (!icon.isNull()) {
            QString type = index.data(NoteModel::TypeRole).toString();
            if (type == "image") {
                // 如果是图片，绘制更大的缩略图 (32x32)
                int size = 32;
                icon.paint(painter, rect.left() + 10, rect.top() + (rect.height() - size) / 2, size, size);
            } else {
                icon.paint(painter, rect.left() + 10, rect.top() + (rect.height() - 20) / 2, 20, 20);
            }
        }

        // 标题文本 (根据用户要求，QuickWindow 仅显示笔记标题)
        QString text = index.data(NoteModel::TitleRole).toString();
        painter->setPen(isSelected ? Qt::white : QColor("#CCCCCC"));
        painter->setFont(QFont("Microsoft YaHei", 9));
        
        QRect textRect = rect.adjusted(40, 0, -50, 0);
        painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, 
                         painter->fontMetrics().elidedText(text, Qt::ElideRight, textRect.width()));

        // 时间 (极简展示) - 显示在右上方
        QString timeStr = index.data(NoteModel::TimeRole).toDateTime().toString("MM-dd HH:mm");
        painter->setPen(QColor("#666666"));
        painter->setFont(QFont("Segoe UI", 7));
        painter->drawText(rect.adjusted(0, 3, -10, 0), Qt::AlignRight | Qt::AlignTop, timeStr);

        painter->restore();
    }
};

#endif // QUICKNOTEDELEGATE_H
