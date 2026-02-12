#ifndef NOTEDELEGATE_H
#define NOTEDELEGATE_H

#include <QStyledItemDelegate>
#include <QPainter>
#include <QPainterPath>
#include <QDateTime>
#include <QRegularExpression>
#include "../models/NoteModel.h"
#include "IconHelper.h"
#include "StringUtils.h"

class NoteDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit NoteDelegate(QObject* parent = nullptr) : QStyledItemDelegate(parent) {}

    // 定义卡片高度
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        Q_UNUSED(index);
        return QSize(option.rect.width(), 110); // 每个卡片高度 110px
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        if (!index.isValid()) return;

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);

        // 1. 获取数据
        QString title = index.data(NoteModel::TitleRole).toString();
        QString content = index.data(NoteModel::ContentRole).toString();
        QString timeStr = index.data(NoteModel::TimeRole).toDateTime().toString("yyyy-MM-dd HH:mm:ss");
        bool isPinned = index.data(NoteModel::PinnedRole).toBool();
        
        // 2. 处理选中状态和背景 (更精致的配色与阴影感)
        bool isSelected = (option.state & QStyle::State_Selected);
        
        // 【关键修复】使用 QRectF 并根据笔宽调整，确保 2px 边框完全在 option.rect 内部绘制，消除选中残留伪影
        qreal penWidth = isSelected ? 2.0 : 1.0;
        QRectF rect = QRectF(option.rect).adjusted(penWidth/2.0, penWidth/2.0, -penWidth/2.0, -4.0 - penWidth/2.0);
        
        // 获取笔记自身的颜色标记作为背景
        QString colorHex = index.data(NoteModel::ColorRole).toString();
        QColor noteColor = colorHex.isEmpty() ? QColor("#1a1a1b") : QColor(colorHex);
        
        QColor bgColor = isSelected ? noteColor.lighter(115) : noteColor; 
        QColor borderColor = isSelected ? QColor("#ffffff") : QColor("#333333");
        
        // 绘制卡片背景
        QPainterPath path;
        path.addRoundedRect(rect, 8, 8);
        
        // 模拟阴影
        if (!isSelected) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(0, 0, 0, 40));
            painter->drawRoundedRect(rect.translated(0, 2), 8, 8);
        }

        painter->setPen(QPen(borderColor, penWidth));
        painter->setBrush(bgColor);
        painter->drawPath(path);

        // 3. 绘制标题 (加粗，主文本色: 统一设为白色以应对多样背景卡片)
        painter->setPen(Qt::white);
        QFont titleFont("Microsoft YaHei", 10, QFont::Bold);
        painter->setFont(titleFont);
        QRectF titleRect = rect.adjusted(12, 10, -35, -70);
        painter->drawText(titleRect, Qt::AlignLeft | Qt::AlignTop, painter->fontMetrics().elidedText(title, Qt::ElideRight, titleRect.width()));

        // 4. 绘制置顶/星级标识
        if (isPinned) {
            QPixmap pin = IconHelper::getIcon("pin", "#f1c40f", 14).pixmap(14, 14);
            painter->drawPixmap(rect.right() - 25, rect.top() + 12, pin);
        }

        // 5. 绘制内容预览 (强制纯白：确保在任何背景下都有最高清晰度)
        painter->setPen(Qt::white);
        painter->setFont(QFont("Microsoft YaHei", 9));
        QRectF contentRect = rect.adjusted(12, 34, -12, -32);
        
        // 【统一优化】调用 StringUtils 剥离 HTML 标签，确保预览纯净
        QString cleanContent = StringUtils::htmlToPlainText(content).simplified();
        QString elidedContent = painter->fontMetrics().elidedText(cleanContent, Qt::ElideRight, contentRect.width() * 2);
        painter->drawText(contentRect, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, elidedContent);

        // 6. 绘制底部元数据栏 (时间图标 + 时间 + 类型标签)
        QRectF bottomRect = rect.adjusted(12, 78, -12, -8);
        
        // 时间 (强制纯白)
        painter->setPen(Qt::white);
        painter->setFont(QFont("Segoe UI", 8));
        QPixmap clock = IconHelper::getIcon("clock", "#ffffff", 12).pixmap(12, 12);
        painter->drawPixmap(bottomRect.left(), bottomRect.top() + (bottomRect.height() - 12) / 2, clock);
        painter->drawText(bottomRect.adjusted(16, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, timeStr);

        // 处理类型标签显示 (对齐智能标签逻辑)
        QString itemType = index.data(NoteModel::TypeRole).toString();
        if (itemType == "text") itemType = "文本";
        else if (itemType == "image") itemType = "图片";
        else if (itemType == "file" || itemType == "local_file") itemType = "文件";
        else if (itemType == "folder" || itemType == "local_folder") itemType = "文件夹";
        else if (itemType == "local_batch") itemType = "批量";
        else if (itemType.isEmpty()) itemType = "笔记";
        
        QString tagText = itemType;
        int tagWidth = painter->fontMetrics().horizontalAdvance(tagText) + 16;
        QRectF tagRect(bottomRect.right() - tagWidth, bottomRect.top() + 2, tagWidth, 18);
        
        painter->setBrush(QColor("#1e1e1e"));
        painter->setPen(QPen(QColor("#444"), 1));
        painter->drawRoundedRect(tagRect, 4, 4);
        
        painter->setPen(Qt::white); // 类型标签文字也改为纯白
        painter->setFont(QFont("Microsoft YaHei", 7, QFont::Bold));
        painter->drawText(tagRect, Qt::AlignCenter, tagText);

        painter->restore();
    }
};

#endif // NOTEDELEGATE_H