#ifndef CATEGORYDELEGATE_H
#define CATEGORYDELEGATE_H

#include <QStyledItemDelegate>
#include <QPainter>
#include <QApplication>
#include "../models/CategoryModel.h"

class CategoryDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        if (!index.isValid()) return;

        bool selected = option.state & QStyle::State_Selected;
        bool hover = option.state & QStyle::State_MouseOver;
        bool isSelectable = index.flags() & Qt::ItemIsSelectable;

        if (isSelectable && (selected || hover)) {
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);

            QString colorHex = index.data(CategoryModel::ColorRole).toString();
            QColor baseColor = colorHex.isEmpty() ? QColor("#4a90e2") : QColor(colorHex);
            QColor bg = selected ? baseColor : QColor("#2a2d2e");
            if (selected) bg.setAlphaF(0.2); // 选中时应用 20% 透明度联动分类颜色

            // 精准计算高亮区域：联合图标与文字区域，避开左侧缩进/箭头区域
            QStyle* style = option.widget ? option.widget->style() : QApplication::style();
            QRect decoRect = style->subElementRect(QStyle::SE_ItemViewItemDecoration, &option, option.widget);
            QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &option, option.widget);
            
            // 联合区域并与当前行 rect 取交集，防止溢出
            QRect contentRect = decoRect.united(textRect);
            contentRect = contentRect.intersected(option.rect);
            
            // 向左右微调 (padding)，并保持上下略有间隙以体现圆角效果
            contentRect.adjust(-6, 1, 6, -1);
            
            painter->setBrush(bg);
            painter->setPen(Qt::NoPen);
            painter->drawRoundedRect(contentRect, 5, 5);
            painter->restore();
        }

        // 绘制原内容 (图标、文字)
        QStyleOptionViewItem opt = option;
        // 关键：移除 Selected 状态，由我们自己控制背景，防止 QStyle 绘制默认的蓝色/灰色整行高亮
        opt.state &= ~QStyle::State_Selected;
        opt.state &= ~QStyle::State_MouseOver;
        
        // 选中时文字强制设为白色以确保清晰度
        if (selected) {
            opt.palette.setColor(QPalette::Text, Qt::white);
            opt.palette.setColor(QPalette::HighlightedText, Qt::white);
        }
        
        QStyledItemDelegate::paint(painter, opt, index);
    }
};

#endif // CATEGORYDELEGATE_H
