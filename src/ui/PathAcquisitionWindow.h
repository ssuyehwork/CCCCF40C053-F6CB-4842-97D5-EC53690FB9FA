#ifndef PATHACQUISITIONWINDOW_H
#define PATHACQUISITIONWINDOW_H

#include "FramelessDialog.h"
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QUrl>
#include <QCheckBox>
#include <QToolButton>

class PathAcquisitionWindow : public FramelessDialog {
    Q_OBJECT
public:
    explicit PathAcquisitionWindow(QWidget* parent = nullptr);
    ~PathAcquisitionWindow();

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void initUI();
    void updateDropHintStyle(bool dragging);

    QListWidget* m_pathList;
    QToolButton* m_dropHint;
    QToolButton* m_copyAllBtn;
    QLabel* m_dropIconLabel;
    QLabel* m_dropTextLabel;
    QCheckBox* m_recursiveCheck;
    QPoint m_dragPos;
    
    QList<QUrl> m_currentUrls;  // 缓存当前拖入的 URL，用于自动刷新
    void processStoredUrls();   // 处理缓存的 URL

private slots:
    void onShowContextMenu(const QPoint& pos); // 右键菜单
    void onBrowse();                           // 浏览文件或文件夹
    void onCopyAll();                          // 复制全部路径
};

#endif // PATHACQUISITIONWINDOW_H
