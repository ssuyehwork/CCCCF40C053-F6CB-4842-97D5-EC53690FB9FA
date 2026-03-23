#ifndef FILEMANAGERWINDOW_H
#define FILEMANAGERWINDOW_H

#include <QMainWindow>
#include <QSplitter>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTreeView>
#include <QListView>
#include <QLineEdit>
#include <QPushButton>
#include "HeaderBar.h"

namespace ui {

class FileManagerWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit FileManagerWindow(QWidget* parent = nullptr);

private:
    void initUI();

    // 五大核心容器
    QWidget* m_categoryContainer; // ① 分类
    QWidget* m_treeContainer;     // ② 树状导航
    QWidget* m_contentContainer;  // ③ 内容
    QWidget* m_metaContainer;     // ④ 元数据
    QWidget* m_filterContainer;   // ⑤ 筛选

    HeaderBar* m_header;
};

} // namespace ui

#endif // FILEMANAGERWINDOW_H
