#include "DropTreeView.h"
#include "../models/CategoryModel.h"
#include <QDrag>
#include <QPixmap>

DropTreeView::DropTreeView(QWidget* parent) : QTreeView(parent) {
    setAcceptDrops(true);
    setDropIndicatorShown(true);
}

void DropTreeView::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasFormat("application/x-note-ids") ||
        event->mimeData()->hasFormat("application/x-qabstractitemmodeldatalist")) {
        event->acceptProposedAction();
    } else {
        // [MODIFIED] 非内部数据显式 ignore，允许冒泡到 QuickWindow 处理外部拖入
        event->ignore();
    }
}

void DropTreeView::dragMoveEvent(QDragMoveEvent* event) {
    if (event->mimeData()->hasFormat("application/x-note-ids") ||
        event->mimeData()->hasFormat("application/x-qabstractitemmodeldatalist")) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void DropTreeView::dropEvent(QDropEvent* event) {
    if (event->mimeData()->hasFormat("application/x-note-ids")) {
        QByteArray data = event->mimeData()->data("application/x-note-ids");
        QStringList idStrs = QString::fromUtf8(data).split(",", Qt::SkipEmptyParts);
        QList<int> ids;
        for (const QString& s : idStrs) ids << s.toInt();

        // [COMPAT] 适配 Qt6：使用 event->position().toPoint() 替换已废弃的 event->pos()
        QModelIndex index = indexAt(event->position().toPoint());
        emit notesDropped(ids, index);
        event->acceptProposedAction();
    } else if (event->mimeData()->hasFormat("application/x-qabstractitemmodeldatalist")) {
        // 允许 QStandardItemModel 的默认拖拽行为（用于分类排序）
        QTreeView::dropEvent(event);
    } else {
        event->ignore();
    }
}

void DropTreeView::startDrag(Qt::DropActions supportedActions) {
    // 追踪拖拽 ID
    auto* catModel = qobject_cast<CategoryModel*>(model());
    if (catModel && !selectedIndexes().isEmpty()) {
        catModel->setDraggingId(selectedIndexes().first().data(CategoryModel::IdRole).toInt());
    }

    // 禁用默认的快照卡片预览，改用 1x1 透明占位符
    QDrag* drag = new QDrag(this);
    drag->setMimeData(model()->mimeData(selectedIndexes()));
    
    QPixmap pix(1, 1);
    pix.fill(Qt::transparent);
    drag->setPixmap(pix);
    drag->setHotSpot(QPoint(0, 0));
    
    drag->exec(supportedActions, Qt::MoveAction);
}
