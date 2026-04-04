#pragma once

#include <QStyledItemDelegate>
#include <QPainter>
#include <QApplication>
#include <QLineEdit>
#include "CategoryModel.h"

namespace ArcMeta {

class CategoryDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        if (!index.isValid()) return;

        if (option.state & QStyle::State_Editing) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }

        bool selected = option.state & QStyle::State_Selected;
        bool hover = option.state & QStyle::State_MouseOver;
        bool isSelectable = index.flags() & Qt::ItemIsSelectable;

        if (isSelectable && (selected || hover)) {
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);

            QString colorHex = index.data(CategoryModel::ColorRole).toString();
            QColor baseColor = colorHex.isEmpty() ? QColor("#3498db") : QColor(colorHex);
            QColor bg = selected ? baseColor : QColor("#2a2d2e");
            if (selected) bg.setAlphaF(0.2f); 

            QStyle* style = option.widget ? option.widget->style() : QApplication::style();
            QRect decoRect = style->subElementRect(QStyle::SE_ItemViewItemDecoration, &option, option.widget);
            QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &option, option.widget);
            
            QRect contentRect = decoRect.united(textRect);
            contentRect = contentRect.intersected(option.rect);
            
            // 物理还原：选中高亮应用 4px 圆角，与列表模式保持一致
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
            opt.palette.setColor(QPalette::HighlightedText, Qt::white);
        }
        
        QStyledItemDelegate::paint(painter, opt, index);
    }

    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        Q_UNUSED(option);
        Q_UNUSED(index);
        QLineEdit* editor = new QLineEdit(parent);
        editor->setStyleSheet(
            "QLineEdit {"
            "  background-color: #2D2D2D;"
            "  color: white;"
            "  border: 1px solid #4a90e2;"
            "  border-radius: 6px;"
            "  padding: 0px 4px;"
            "  margin: 0px;"
            "}"
        );
        return editor;
    }

    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        Q_UNUSED(index);
        QStyle* style = option.widget ? option.widget->style() : QApplication::style();
        QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &option, option.widget);
        textRect.adjust(0, -1, 0, 1);
        editor->setGeometry(textRect);
    }
};

} // namespace ArcMeta
