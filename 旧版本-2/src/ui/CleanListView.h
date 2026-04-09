#ifndef CLEANLISTVIEW_H
#define CLEANLISTVIEW_H

#include <QListView>

class CleanListView : public QListView {
    Q_OBJECT
public:
    explicit CleanListView(QWidget* parent = nullptr);

protected:
    void startDrag(Qt::DropActions supportedActions) override;
};

#endif // CLEANLISTVIEW_H
