#ifndef GRAPHWIDGET_H
#define GRAPHWIDGET_H

#include <QGraphicsView>
#include <QGraphicsItem>
#include <QTimer>
#include <QVariantMap>

class NodeItem;
class EdgeItem;

class GraphWidget : public QGraphicsView {
    Q_OBJECT
public:
    explicit GraphWidget(QWidget* parent = nullptr);

    void itemMoved();
    // 新增：加载笔记生成图谱
    void loadNotes(const QList<QVariantMap>& notes);

protected:
    void timerEvent(QTimerEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void drawBackground(QPainter* painter, const QRectF& rect) override;

private:
    int m_timerId = 0;
};

class NodeItem : public QGraphicsItem {
public:
    NodeItem(GraphWidget* graphWidget, const QString& title, int id);
    void addEdge(EdgeItem* edge);
    
    enum { Type = UserType + 1 };
    int type() const override { return Type; }

    void calculateForces();
    bool advancePosition();

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    
    int noteId() const { return m_id; }
    QString title() const { return m_title; }

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;

private:
    QList<EdgeItem*> m_edgeList;
    QPointF m_newPos;
    GraphWidget* m_graph;
    QString m_title;
    int m_id;
};

#endif // GRAPHWIDGET_H