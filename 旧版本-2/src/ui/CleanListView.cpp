#include "CleanListView.h"
#include <QDrag>
#include <QPixmap>
#include <QMimeData>

CleanListView::CleanListView(QWidget* parent) : QListView(parent) {}

void CleanListView::startDrag(Qt::DropActions supportedActions) {
    QModelIndexList indexes = selectedIndexes();
    if (indexes.isEmpty()) return;

    QMimeData* data = model()->mimeData(indexes);
    if (!data) return;

    QDrag* drag = new QDrag(this);
    drag->setMimeData(data);
    
    // 关键：在这里设置一个 1x1 像素的透明 Pixmap 用以消除臃肿的截图
    QPixmap transparentPixmap(1, 1);
    transparentPixmap.fill(Qt::transparent);
    drag->setPixmap(transparentPixmap);
    drag->setHotSpot(QPoint(0, 0));

    drag->exec(supportedActions, Qt::MoveAction);
}
