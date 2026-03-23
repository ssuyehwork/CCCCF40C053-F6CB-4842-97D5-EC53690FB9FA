#ifndef DRAGGABLELISTVIEW_H
#define DRAGGABLELISTVIEW_H

#include <QListView>
#include <QDrag>
#include <QMimeData>
#include <QMouseEvent>

class DraggableListView : public QListView {
    Q_OBJECT
public:
    explicit DraggableListView(QWidget* parent = nullptr) : QListView(parent) {}

protected:
    void startDrag(Qt::DropActions supportedActions) override {
        QModelIndexList indices = selectionModel()->selectedIndexes();
        if (indices.isEmpty()) return;

        QMimeData* mimeData = new QMimeData();
        QStringList paths;
        for (const auto& idx : indices) {
            QString path = idx.data(Qt::UserRole + 1).toString();
            if (!path.isEmpty()) paths << path;
        }
        
        if (paths.isEmpty()) {
            delete mimeData;
            return;
        }

        mimeData->setData("application/x-file-paths", paths.join(";").toUtf8());

        QDrag* drag = new QDrag(this);
        drag->setMimeData(mimeData);
        drag->exec(supportedActions);
    }
};

#endif // DRAGGABLELISTVIEW_H
