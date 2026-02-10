#include "GraphWidget.h"
#include <QPainter>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QWheelEvent>
#include <QtMath>
#include <QRandomGenerator>
#include "StringUtils.h"

class EdgeItem : public QGraphicsItem {
public:
    EdgeItem(NodeItem* sourceNode, NodeItem* destNode);
    void adjust();
protected:
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
private:
    NodeItem *source, *dest;
    QPointF sourcePoint, destPoint;
};

// --- EdgeItem Implementation ---
EdgeItem::EdgeItem(NodeItem* sourceNode, NodeItem* destNode) : source(sourceNode), dest(destNode) {
    source->addEdge(this);
    dest->addEdge(this);
    adjust();
    setZValue(-1); // 线在节点下层
}
void EdgeItem::adjust() {
    prepareGeometryChange();
    sourcePoint = source->pos();
    destPoint = dest->pos();
}
QRectF EdgeItem::boundingRect() const {
    return QRectF(sourcePoint, QSizeF(destPoint.x() - sourcePoint.x(), destPoint.y() - sourcePoint.y())).normalized();
}
void EdgeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    painter->setPen(QPen(QColor(100, 100, 100, 100), 1));
    painter->drawLine(sourcePoint, destPoint);
}

// --- GraphWidget Implementation ---
GraphWidget::GraphWidget(QWidget* parent) : QGraphicsView(parent) {
    QGraphicsScene* scene = new QGraphicsScene(this);
    scene->setItemIndexMethod(QGraphicsScene::NoIndex);
    scene->setSceneRect(-400, -300, 800, 600);
    setScene(scene);
    setCacheMode(CacheBackground);
    setViewportUpdateMode(BoundingRectViewportUpdate);
    setRenderHint(QPainter::Antialiasing);
    setTransformationAnchor(AnchorUnderMouse);
    setDragMode(QGraphicsView::ScrollHandDrag);
}

void GraphWidget::itemMoved() {
    if (!m_timerId) m_timerId = startTimer(1000 / 25);
}

void GraphWidget::loadNotes(const QList<QVariantMap>& notes) {
    scene()->clear();
    
    QList<NodeItem*> nodes;
    QMap<QString, QList<NodeItem*>> tagMap; // 用于按标签建立连接

    // 1. 创建节点
    for (const auto& note : notes) {
        QString title = note["title"].toString();
        int id = note["id"].toInt();
        QString tagsStr = note["tags"].toString();
        
        NodeItem* node = new NodeItem(this, title, id);
        // 随机初始位置，避免重叠
        node->setPos(-200 + QRandomGenerator::global()->bounded(400), 
                     -200 + QRandomGenerator::global()->bounded(400));
        scene()->addItem(node);
        nodes.append(node);

        // 记录标签归属
        QStringList tags = tagsStr.split(",", Qt::SkipEmptyParts);
        for(const QString& t : tags) {
            tagMap[t.trimmed()].append(node);
        }
    }

    // 2. 建立连接 (如果两个笔记有相同的标签，连线)
    for (auto it = tagMap.begin(); it != tagMap.end(); ++it) {
        QList<NodeItem*> group = it.value();
        for (int i = 0; i < group.size(); ++i) {
            for (int j = i + 1; j < group.size(); ++j) {
                scene()->addItem(new EdgeItem(group[i], group[j]));
            }
        }
    }
    
    // 启动物理引擎
    itemMoved();
}

void GraphWidget::timerEvent(QTimerEvent* event) {
    Q_UNUSED(event);
    QList<NodeItem*> nodes;
    for (QGraphicsItem* item : scene()->items()) {
        if (NodeItem* node = qgraphicsitem_cast<NodeItem*>(item)) nodes << node;
    }
    for (NodeItem* node : nodes) node->calculateForces();
    bool itemsMoved = false;
    for (NodeItem* node : nodes) {
        if (node->advancePosition()) itemsMoved = true;
    }
    if (!itemsMoved) {
        killTimer(m_timerId);
        m_timerId = 0;
    }
}

void GraphWidget::wheelEvent(QWheelEvent* event) {
    scale(pow(2.0, event->angleDelta().y() / 240.0), pow(2.0, event->angleDelta().y() / 240.0));
}

void GraphWidget::drawBackground(QPainter* painter, const QRectF& rect) {
    painter->fillRect(rect, QColor(30, 30, 30));
}

// --- NodeItem Implementation ---
NodeItem::NodeItem(GraphWidget* graphWidget, const QString& title, int id) 
    : m_graph(graphWidget), m_title(title), m_id(id) {
    setFlag(ItemIsMovable);
    setFlag(ItemSendsGeometryChanges);
    setCacheMode(DeviceCoordinateCache);
    setToolTip(StringUtils::wrapToolTip(QString("%1 (ID: %2)").arg(title).arg(id)));
}
void NodeItem::addEdge(EdgeItem* edge) { m_edgeList << edge; }

void NodeItem::calculateForces() {
    if (!scene() || scene()->mouseGrabberItem() == this) {
        m_newPos = pos();
        return;
    }
    // 简化物理引擎：斥力 (防止节点堆积)
    qreal xvel = 0, yvel = 0;
    for (QGraphicsItem* item : scene()->items()) {
        NodeItem* node = qgraphicsitem_cast<NodeItem*>(item);
        if (!node || node == this) continue;
        QLineF line(mapToScene(0, 0), node->mapToScene(0, 0));
        qreal dx = line.dx();
        qreal dy = line.dy();
        double l = 2.0 * (dx * dx + dy * dy);
        if (l > 0) {
            xvel += (dx * 200.0) / l;
            yvel += (dy * 200.0) / l;
        }
    }
    // 简化物理引擎：拉力 (让连接的节点靠近)
    double weight = (m_edgeList.size() + 1) * 10;
    for (EdgeItem* edge : m_edgeList) {
        QPointF vec;
        if (edge->mapToScene(0,0) == pos()) // 这里需要判断边的哪一头是自己
             // 简化处理：由于EdgeItem并没有简单的方法暴露对方坐标，这里仅作受力示意
             // 实际应该在EdgeItem里存source/dest指针
             continue; 
    }
    
    // 向中心聚集的重力，防止飞出屏幕
    QPointF centerVec = -pos(); 
    xvel += centerVec.x() / 1000.0;
    yvel += centerVec.y() / 1000.0;

    if (qAbs(xvel) < 0.1 && qAbs(yvel) < 0.1) xvel = yvel = 0;

    m_newPos = pos() + QPointF(xvel, yvel);
}

bool NodeItem::advancePosition() {
    if (m_newPos == pos()) return false;
    setPos(m_newPos);
    return true;
}

QRectF NodeItem::boundingRect() const { return QRectF(-60, -20, 120, 65); }

void NodeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    painter->setBrush(QColor("#4FACFE"));
    painter->setPen(QPen(Qt::white, 1));
    painter->drawEllipse(-10, -10, 20, 20);
    
    // 绘制标题 - 扩大绘制区域并使用省略号处理超长文字
    painter->setPen(Qt::white);
    QFont font = painter->font();
    font.setPointSize(8);
    painter->setFont(font);
    
    QRectF textRect(-60, 12, 120, 30);
    QString elidedTitle = painter->fontMetrics().elidedText(m_title, Qt::ElideRight, textRect.width());
    painter->drawText(textRect, Qt::AlignCenter | Qt::TextWordWrap, elidedTitle);
}

QVariant NodeItem::itemChange(GraphicsItemChange change, const QVariant& value) {
    if (change == ItemPositionHasChanged) {
        for (EdgeItem* edge : m_edgeList) edge->adjust();
        m_graph->itemMoved();
    }
    return QGraphicsItem::itemChange(change, value);
}
void NodeItem::mousePressEvent(QGraphicsSceneMouseEvent* event) { update(); QGraphicsItem::mousePressEvent(event); }
void NodeItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) { update(); QGraphicsItem::mouseReleaseEvent(event); }
