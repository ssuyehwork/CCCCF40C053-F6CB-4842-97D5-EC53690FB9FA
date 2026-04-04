#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTreeView>
#include <QListView>
#include <QSplitter>
#include <QStandardItemModel>
#include <QTimer>
#include <QFileInfo>
#include "../models/CategoryModel.h"
#include "HeaderBar.h"
#include "MetadataPanel.h"
#include "QuickPreview.h"
#include "DropTreeView.h"
#include "FilterPanel.h"
#include "CategoryLockWidget.h"
#include "ContentPanel.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <windowsx.h>
#endif

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    int getCurrentCategoryId() const {
        if (m_currentFilterType == "category") return m_currentFilterValue.toInt();
        return -1;
    }

signals:
    void globalLockRequested();

private slots:
    void onSelectionChanged(const QItemSelection& selected, const QItemSelection& deselected);
    void onTagSelected(const QModelIndex& index);
    void showContextMenu(const QPoint& pos);
    
    // Layout persistence
    void saveLayout();
    void restoreLayout();
    void updateShortcuts();

    void refreshData();
    void scheduleRefresh();
    void doPreview();
    void updatePreviewContent();

    // 快捷键处理与操作逻辑 (同步 ArcMeta)
    void doDeleteSelected(bool physical = false);
    void doToggleFavorite();
    void doTogglePin();
public:
    void doCreateByLine(bool fromClipboard);
    // 2026-04-04 按照用户要求：参考参考版，将新建逻辑转向物理项（文件夹/文件）
    void doCreateNewItem(const QString& type);

private:
    void doExtractContent();
    void doSetRating(int rating);
    void doMoveToCategory(int catId);
    void doCopyTags();
    void doPasteTags();
    void doImportCategory(int catId);
    void doImportFolder(int catId);
    void doExportCategory(int catId, const QString& catName);
    bool verifyExportPermission(); // 2026-03-20 按照用户要求，增加导出前的统一身份验证逻辑

protected:
#ifdef Q_OS_WIN
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;
#endif
    bool event(QEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    void initUI();
    void setupShortcuts();
    void updateFocusLines();
    void safeExpandPartitionTree(); // 2026-03-xx 按照用户要求：物理级预防上锁分类展开 (同步 ArcMeta)
    
    ArcMeta::DropTreeView* m_systemTree;
    ArcMeta::CategoryModel* m_systemModel;
    ArcMeta::DropTreeView* m_partitionTree;
    ArcMeta::CategoryModel* m_partitionModel;
    QWidget* m_sidebarContainer;
    QWidget* m_listFocusLine;
    QWidget* m_sidebarFocusLine;
    
    ArcMeta::ContentPanel* m_contentPanel;

    HeaderBar* m_header;
    ArcMeta::MetadataPanel* m_metaPanel;
    ArcMeta::FilterPanel* m_filterPanel;
    QFrame* m_filterContainer;
    QWidget* m_filterWrapper;
    
    CategoryLockWidget* m_lockWidget;

    QString m_currentKeyword;
    QString m_currentFilterType = "all";
    QVariant m_currentFilterValue = -1;
    int m_currentPage = 1;
    int m_pageSize = 100;
    QTimer* m_searchTimer;
    QTimer* m_refreshTimer;
    QString m_lastRefreshDate; // 2026-04-xx 按照用户要求：记录上次刷新的日期，用于零点自动刷新
};

#endif // MAINWINDOW_H
