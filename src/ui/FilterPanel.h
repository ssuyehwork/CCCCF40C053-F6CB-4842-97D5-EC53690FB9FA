#ifndef FILTERPANEL_H
#define FILTERPANEL_H

#include <QWidget>
#include <QVariantMap>
#include <QVBoxLayout>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QPushButton>
#include <QLabel>
#include <QMouseEvent>
#include <QGraphicsDropShadowEffect>

class FilterPanel : public QWidget {
    Q_OBJECT
public:
    explicit FilterPanel(QWidget* parent = nullptr);
    void updateStats(const QString& keyword, const QString& type, const QVariant& value);
    QVariantMap getCheckedCriteria() const;
    void resetFilters();

signals:
    void filterChanged();

private:
    void initUI();
    void setupTree();
    void onItemChanged(QTreeWidgetItem* item, int column);
    void onItemClicked(QTreeWidgetItem* item, int column);
    void refreshNode(const QString& key, const QList<QVariantMap>& items, bool isCol = false);

    QWidget* m_container;
    QTreeWidget* m_tree;
    QPushButton* m_btnReset;

    QMap<QString, QTreeWidgetItem*> m_roots;
    bool m_blockItemClick = false;
    QTreeWidgetItem* m_lastChangedItem = nullptr;
};

#endif // FILTERPANEL_H
