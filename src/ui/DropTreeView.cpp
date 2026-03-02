#include "DropTreeView.h"
#include "../models/CategoryModel.h"
#include <QDrag>
#include <QPixmap>

DropTreeView::DropTreeView(QWidget* parent) : QTreeView(parent) {
    setAcceptDrops(true);
    setDropIndicatorShown(true);
}

void DropTreeView::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasFormat("application/x-note-ids")) {
        event->acceptProposedAction();
    } else {
        QTreeView::dragEnterEvent(event);
    }
}

void DropTreeView::dragMoveEvent(QDragMoveEvent* event) {
    if (event->mimeData()->hasFormat("application/x-note-ids")) {
        event->acceptProposedAction();
    } else {
        QTreeView::dragMoveEvent(event);
    }
}

void DropTreeView::dropEvent(QDropEvent* event) {
    if (event->mimeData()->hasFormat("application/x-note-ids")) {
        QByteArray data = event->mimeData()->data("application/x-note-ids");
        QStringList idStrs = QString::fromUtf8(data).split(",", Qt::SkipEmptyParts);
        QList<int> ids;
        for (const QString& s : idStrs) ids << s.toInt();

        QModelIndex index = indexAt(event->position().toPoint());
        emit notesDropped(ids, index);
        event->acceptProposedAction();
    } else {
        QTreeView::dropEvent(event);
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
