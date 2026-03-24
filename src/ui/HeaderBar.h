#ifndef HEADERBAR_H
#define HEADERBAR_H

#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <QMenu>
#include "SearchLineEdit.h"

class HeaderBar : public QWidget {
    Q_OBJECT
public:
    explicit HeaderBar(QWidget* parent = nullptr);

signals:
    void newNoteRequested();
    void toggleSidebar();
    void toolboxRequested();
    void globalLockRequested();
    void metadataToggled(bool checked);
    void refreshRequested();
    void filterRequested();
    void stayOnTopRequested(bool checked);
    void windowClose();
    void windowMinimize();
    void windowMaximize();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

public:
    void setFilterActive(bool active);
    void setMetadataActive(bool active);
    void updateToolboxStatus(bool active); // 2026-03-22 [NEW] 同步工具箱按钮颜色状态

private:
    QPushButton* m_btnFilter;
    QPushButton* m_btnMeta;
    QPushButton* m_btnStayOnTop;
    QPushButton* m_btnToolbox; // 2026-03-22 [NEW] 提升为成员变量

    int m_currentPage = 1;
    int m_totalPages = 1;
    QPoint m_dragPos;
};

#endif // HEADERBAR_H
