#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTreeView>
#include <QListView>
#include <QSplitter>
#include <QStandardItemModel>
#include <QTimer>
#include <QFileInfo>
#include "HeaderBar.h"
#include "../db/Database.h"
#include "../mft/MftReader.h"
#include "../mft/UsnWatcher.h"
#include "../meta/AmMetaJson.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <windowsx.h>
#endif

namespace ui {

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

signals:
    void toolboxRequested();
    void globalLockRequested();

public:
    void updateToolboxStatus(bool active);

private slots:
    void onFileSelected(const QModelIndex& index);
    void onDirectoryClicked(const QModelIndex& index);
    void refreshContent();
    void applyFilter();
    void onMetadataChanged();

protected:
#ifdef Q_OS_WIN
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;
#endif
    bool event(QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    void initUI();
    void setupDatabase();
    void startMftScanning();

    // 五大核心容器
    QWidget* m_container1_Category; // ① 分类
    QWidget* m_container2_Tree;     // ② 树状导航
    QWidget* m_container3_Content;  // ③ 内容
    QWidget* m_container4_Meta;     // ④ 元数据
    QWidget* m_container5_Filter;   // ⑤ 筛选

    HeaderBar* m_header;
    
    // UI 组件
    QTreeView* m_treeView;      // 树状导航
    QListView* m_contentView;   // 内容展示
    QStandardItemModel* m_treeModel;
    QStandardItemModel* m_contentModel;

    // 状态
    QString m_currentPath;
    QTimer* m_refreshTimer;
};

} // namespace ui

#endif // MAINWINDOW_H
