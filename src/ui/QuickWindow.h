#ifndef QUICKWINDOW_H
#define QUICKWINDOW_H

#include <QWidget>
#include "SearchLineEdit.h"
#include <QListView>
#include <QTreeView>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QLabel>
#include <QTimer>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QHideEvent>
#include "../models/NoteModel.h"
#include "../models/CategoryModel.h"
#include "QuickPreview.h"
#include "DropTreeView.h"
#include "CategoryLockWidget.h"
#include "ClickableLineEdit.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <windowsx.h>
#endif

// 自定义列表视图，实现 Ditto 风格的轻量化拖拽
class DittoListView : public QListView {
    Q_OBJECT
public:
    using QListView::QListView;
protected:
    void startDrag(Qt::DropActions supportedActions) override;
    void mousePressEvent(QMouseEvent* event) override;
};

class QuickWindow : public QWidget {
    Q_OBJECT
public:
    explicit QuickWindow(QWidget* parent = nullptr);
    void showAuto();
    void focusLockInput();
    void saveState();
    void restoreState();

public slots:
    void refreshData();
    void scheduleRefresh();
    void onNoteAdded(const QVariantMap& note);

signals:
    void toggleMainWindowRequested();
    void toolboxRequested();

protected:
#ifdef Q_OS_WIN
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;
#endif
    bool event(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void initUI();
    void setupAppLock();
    void activateNote(const QModelIndex& index);
    void setupShortcuts();
    void updatePartitionStatus(const QString& name);
    void refreshSidebar();
    void applyListTheme(const QString& colorHex);
public:
    QString currentCategoryColor() const { return m_currentCategoryColor; }
    bool isAutoCategorizeEnabled() const { return m_autoCategorizeClipboard; }
    bool isLocked() const { return m_appLockWidget != nullptr; }
    int getCurrentCategoryId() const { return (m_currentFilterType == "category") ? m_currentFilterValue.toInt() : -1; }

    // 快捷键处理函数
    void doDeleteSelected(bool physical = false);
    void doToggleFavorite();
    void doTogglePin();
    void doLockSelected();
    void doNewIdea();
    void doExtractContent();
    void doEditSelected();
    void doEditNote(int id);
    void doSetRating(int rating);
    void doMoveToCategory(int catId);
    void doRestoreTrash();
    void doPreview();
    void doGlobalLock();
    void toggleStayOnTop(bool checked);
    void toggleSidebar();
    void showListContextMenu(const QPoint& pos);
    void showSidebarMenu(const QPoint& pos);
    void showToolboxMenu(const QPoint& pos);
    void updatePreviewContent();
    void handleTagInput();
    void openTagSelector();
    void doCopyTags();
    void doPasteTags();
    
    SearchLineEdit* m_searchEdit;
    QListView* m_listView;
    CategoryLockWidget* m_lockWidget;
    QWidget* m_appLockWidget = nullptr;
    NoteModel* m_model;
    QuickPreview* m_quickPreview;
    
    DropTreeView* m_systemTree;
    DropTreeView* m_partitionTree;
    CategoryModel* m_systemModel;
    CategoryModel* m_partitionModel;
    
    QTimer* m_searchTimer;
    QTimer* m_monitorTimer;
    QTimer* m_refreshTimer;
    QSplitter* m_splitter;
    QLabel* m_statusLabel;
    ClickableLineEdit* m_tagEdit;

    int m_currentPage = 1;
    int m_totalPages = 1;
    QString m_currentFilterType = "all";
    QVariant m_currentFilterValue = -1;
    QString m_currentCategoryColor = "#4a90e2"; // 默认蓝色
    bool m_autoCategorizeClipboard = false;
    bool m_isStayOnTop = false;

#ifdef Q_OS_WIN
    HWND m_lastActiveHwnd = nullptr;
    HWND m_lastFocusHwnd = nullptr;
    DWORD m_lastThreadId = 0;
#endif
};

#endif // QUICKWINDOW_H