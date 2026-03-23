#include "NoteDelegate.h"
#include <QApplication>
#include <QTreeView>

void NoteDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const {
    if (!index.isValid()) return;

    bool isCategory = index.data(NoteModel::IsCategoryRole).toBool();
    if (isCategory) {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);

        // 绘制分类背景条
        QRect rect = option.rect.adjusted(2, 2, -2, -2);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(45, 45, 48)); // 深灰背景
        painter->drawRoundedRect(rect, 4, 4);

        // 绘制标题
        QString name = index.data(Qt::DisplayRole).toString();
        painter->setPen(QColor(200, 200, 200));
        painter->setFont(QFont("Microsoft YaHei", 9, QFont::Bold));
        painter->drawText(rect.adjusted(10, 0, -10, 0), Qt::AlignLeft | Qt::AlignVCenter, name);

        painter->restore();
        return;
    }

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    QString title = index.data(NoteModel::TitleRole).toString();
    QString timeStr = index.data(NoteModel::TimeRole).toDateTime().toString("yyyy-MM-dd HH:mm:ss");
    bool isPinned = index.data(NoteModel::PinnedRole).toBool();
    bool isSelected = (option.state & QStyle::State_Selected);

    qreal penWidth = isSelected ? 2.0 : 1.0;
    QRectF rect = QRectF(option.rect).adjusted(penWidth/2.0, penWidth/2.0, -penWidth/2.0, -4.0 - penWidth/2.0);

    QString colorHex = index.data(NoteModel::ColorRole).toString();
    QColor noteColor = colorHex.isEmpty() ? QColor("#1a1a1b") : QColor(colorHex);

    QColor bgColor = isSelected ? noteColor.lighter(115) : noteColor;
    QColor borderColor = isSelected ? QColor("#ffffff") : QColor("#333333");

    QPainterPath path;
    path.addRoundedRect(rect, 8, 8);

    if (!isSelected) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(0, 0, 0, 40));
        painter->drawRoundedRect(rect.translated(0, 2), 8, 8);
    }

    painter->setPen(QPen(borderColor, penWidth));
    painter->setBrush(bgColor);
    painter->drawPath(path);

    painter->setPen(Qt::white);
    painter->setFont(QFont("Microsoft YaHei", 10, QFont::Bold));
    QRectF titleRect = rect.adjusted(12, 10, -35, -70);
    painter->drawText(titleRect, Qt::AlignLeft | Qt::AlignTop, painter->fontMetrics().elidedText(title, Qt::ElideRight, titleRect.width()));

    if (isPinned) {
        QPixmap pin = IconHelper::getIcon("pin_vertical", "#FF551C", 14).pixmap(14, 14);
        painter->drawPixmap(rect.right() - 25, rect.top() + 12, pin);
    }

    painter->setPen(Qt::white);
    painter->setFont(QFont("Microsoft YaHei", 9));
    QRectF contentRect = rect.adjusted(12, 34, -12, -32);
    QString cleanContent = index.data(NoteModel::PlainContentRole).toString();
    QString elidedContent = painter->fontMetrics().elidedText(cleanContent, Qt::ElideRight, contentRect.width() * 2);
    painter->drawText(contentRect, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, elidedContent);

    QRectF bottomRect = rect.adjusted(12, 78, -12, -8);
    painter->setPen(Qt::white);
    painter->setFont(QFont("Segoe UI", 8));
    QPixmap clock = IconHelper::getIcon("clock", "#ffffff", 12).pixmap(12, 12);
    painter->drawPixmap(bottomRect.left(), bottomRect.top() + (bottomRect.height() - 12) / 2, clock);
    painter->drawText(bottomRect.adjusted(16, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, timeStr);

    QIcon typeIcon = index.data(Qt::DecorationRole).value<QIcon>();
    if (!typeIcon.isNull()) {
        int iconSize = 18;
        QRectF iconRect(bottomRect.right() - iconSize - 4, bottomRect.top() + (bottomRect.height() - iconSize) / 2, iconSize, iconSize);
        typeIcon.paint(painter, iconRect.toRect(), Qt::AlignCenter);
    }

    painter->restore();
}

QSize NoteDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const {
    if (index.data(NoteModel::IsCategoryRole).toBool()) {
        return QSize(option.rect.width(), 30);
    }
    return QSize(option.rect.width(), 110);
}

bool NoteDelegate::helpEvent(QHelpEvent* event, QAbstractItemView* view, const QStyleOptionViewItem& option, const QModelIndex& index) {
    if (event && event->type() == QEvent::ToolTip && index.isValid()) {
        QString tip = index.data(Qt::ToolTipRole).toString();
        if (!tip.isEmpty()) {
            ToolTipOverlay::instance()->showText(event->globalPos(), tip, 2000);
            return true;
        }
    }
    return QStyledItemDelegate::helpEvent(event, view, option, index);
}
