#ifndef CATEGORYDELEGATE_H
#define CATEGORYDELEGATE_H

#include <QStyledItemDelegate>
#include <QPainter>
#include <QApplication>
#include <QLineEdit>
#include <QAbstractProxyModel>
#include "../models/CategoryModel.h"

class CategoryDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        if (!index.isValid()) return;

        // [CRITICAL] 编辑状态下跳过自定义绘制，避免背景颜色与编辑器冲突
        if (option.state & QStyle::State_Editing) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }

        bool selected = option.state & QStyle::State_Selected;
        bool hover = option.state & QStyle::State_MouseOver;
        /* [MODIFIED] 2026-03-11 增加拖拽视觉反馈：识别放落目标 (State_On) 与 拖拽源 (isDraggingSource) */
        bool isDropTarget = option.state & QStyle::State_On; 
        bool isDraggingSource = false;

        const CategoryModel* catModel = qobject_cast<const CategoryModel*>(index.model());
        if (!catModel) {
            if (auto* proxy = qobject_cast<const QAbstractProxyModel*>(index.model())) {
                catModel = qobject_cast<const CategoryModel*>(proxy->sourceModel());
            }
        }
        if (catModel && catModel->draggingId() != -1) {
            isDraggingSource = (index.data(CategoryModel::IdRole).toInt() == catModel->draggingId());
        }

        bool isSelectable = index.flags() & Qt::ItemIsSelectable;
        bool isDropEnabled = index.flags() & Qt::ItemIsDropEnabled;

        if ((isSelectable && (selected || hover || isDraggingSource)) || (isDropEnabled && isDropTarget)) {
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);

            QString colorHex = index.data(CategoryModel::ColorRole).toString();
            QColor baseColor = colorHex.isEmpty() ? QColor("#4a90e2") : QColor(colorHex);
            
            QColor bg;
            bool isActive = (option.state & QStyle::State_Active);

            if (isDraggingSource) {
                bg = QColor("#3e3e42"); // 正在被拖拽的源项：深灰色弱化
                bg.setAlphaF(0.6);
            } else if (isDropTarget) {
                bg = baseColor; // 拖拽经过的目标项：分类色高亮提醒
                bg.setAlphaF(0.4);
            } else if (selected || hover) {
                // 2026-03-xx 按照用户最新要求：选中或悬停项均使用较暗的分类原色，且透明度保持在 0.7 左右
                // 如果窗口不活跃，进一步弱化高亮为深灰色
                bg = isActive ? baseColor.darker(160) : QColor("#3e3e42");
                bg.setAlphaF(isActive ? 0.7 : 0.5);
            }

            // 2026-03-xx 恢复为紧凑包裹模式：背景仅包裹图标与文字内容，保持原有的精致感
            QStyle* style = option.widget ? option.widget->style() : QApplication::style();
            QRect iconRect = style->subElementRect(QStyle::SE_ItemViewItemDecoration, &option, option.widget);
            QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &option, option.widget);

            // 取图标和文字区域的并集，并与当前项矩形取交集以防越界
            QRect contentRect = (iconRect | textRect).intersected(option.rect);
            // 应用 8px 级别的精致 Padding，形成胶囊状背景
            contentRect.adjust(-6, 1, 6, -1);
            
            painter->setBrush(bg);
            painter->setPen(Qt::NoPen);
            painter->drawRoundedRect(contentRect, 6, 6);
            painter->restore();
        }

        // 绘制原内容 (图标、文字)
        QStyleOptionViewItem opt = option;
        // 关键：移除 Selected 状态，由我们自己控制背景，防止 QStyle 绘制默认的蓝色/灰色整行高亮
        opt.state &= ~QStyle::State_Selected;
        opt.state &= ~QStyle::State_MouseOver;
        
        // 2026-03-xx 核心修复：彻底屏蔽 Inactive 状态下的原生高亮背景干扰
        // 显式将 HighlightedText 和 Highlight 背景设为透明/白色，确保文字颜色稳定
        if (selected) {
            opt.palette.setColor(QPalette::Text, Qt::white);
            opt.palette.setColor(QPalette::HighlightedText, Qt::white);
            opt.palette.setColor(QPalette::Highlight, Qt::transparent);
            opt.palette.setColor(QPalette::Inactive, QPalette::Highlight, Qt::transparent);
            opt.palette.setColor(QPalette::Inactive, QPalette::HighlightedText, Qt::white);
        }
        
        QStyledItemDelegate::paint(painter, opt, index);
    }

    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        QLineEdit* editor = new QLineEdit(parent);
        // [CRITICAL] 优化编辑器样式，增加 padding 解决“挤压”感，并统一配色
        editor->setStyleSheet(
            "QLineEdit {"
            "  background-color: #2D2D2D;"
            "  color: white;"
            "  border: 1px solid #4a90e2;"
            "  border-radius: 4px;"
            "  padding: 0px 4px;"
            "  margin: 0px;"
            "}"
        );
        return editor;
    }

    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        // [CRITICAL] 精准定位编辑器区域：仅覆盖文本部分，不遮挡左侧图标与箭头空间，解决“挤压”感
        QStyle* style = option.widget ? option.widget->style() : QApplication::style();
        QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &option, option.widget);
        
        // 稍微上下扩展，使得输入框在 22px 行高中不显得过于局促
        textRect.adjust(0, -1, 0, 1);
        editor->setGeometry(textRect);
    }
};

#endif // CATEGORYDELEGATE_H
