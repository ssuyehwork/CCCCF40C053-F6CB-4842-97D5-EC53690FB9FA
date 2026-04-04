#pragma once

#include <QStyledItemDelegate>
#include <QPainter>
#include <QApplication>

namespace ArcMeta {

/**
 * @brief 通用树形视图代理，提供圆角高亮效果
 */
class TreeItemDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        if (!index.isValid()) return;

        bool selected = option.state & QStyle::State_Selected;
        bool hover = option.state & QStyle::State_MouseOver;

        if (selected || hover) {
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);

            // 默认使用蓝调高亮，或者根据需要自定义
            QColor bg = selected ? QColor("#378ADD") : QColor("#2a2d2e");
            if (selected) bg.setAlphaF(0.2f);

            QStyle* style = option.widget ? option.widget->style() : QApplication::style();
            QRect decoRect = style->subElementRect(QStyle::SE_ItemViewItemDecoration, &option, option.widget);
            QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &option, option.widget);

            QRect contentRect = decoRect.united(textRect);
            contentRect = contentRect.intersected(option.rect);

            // 物理还原：选中高亮应用 4px 圆角 (符合 AGENTS.md 规范)，增加微量内缩
            contentRect.adjust(0, 1, 0, -1);

            painter->setBrush(bg);
            painter->setPen(Qt::NoPen);
            painter->drawRoundedRect(contentRect, 4, 4);
            painter->restore();
        }

        QStyleOptionViewItem opt = option;
        opt.state &= ~QStyle::State_Selected;
        opt.state &= ~QStyle::State_MouseOver;

        if (selected) {
            opt.palette.setColor(QPalette::Text, Qt::white);
        }

        QStyledItemDelegate::paint(painter, opt, index);
    }
};

} // namespace ArcMeta
