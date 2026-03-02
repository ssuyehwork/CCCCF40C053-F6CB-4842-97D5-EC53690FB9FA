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
#include <QStackedWidget>
#include <QSortFilterProxyModel>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QHideEvent>
#include "../models/NoteModel.h"
#include "../models/CategoryModel.h"
#include "QuickPreview.h"
#include "DropTreeView.h"
#include "CategoryLockWidget.h"
#include "ClickableLineEdit.h"
#include "CleanListView.h"
#include <QShortcut>

#ifdef Q_OS_WIN
#include <windows.h>
#include <windowsx.h>
#endif

class QuickWindow : public QWidget {
    Q_OBJECT
public:
    explicit QuickWindow(QWidget* parent = nullptr);
    void showAuto();
    void focusLockInput();
    void saveState();
    void restoreState();
    int getCurrentCategoryId() const {
        if (m_currentFilterType == "category") return m_currentFilterValue.toInt();
        return -1;
    }

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
    void updateShortcuts();
public:
    QString currentCategoryColor() const { return m_currentCategoryColor; }
    bool isLocked() const { return m_appLockWidget != nullptr; }

    // 快捷键处理函数
    void doDeleteSelected(bool physical = false);
    void doToggleFavorite();
    void doTogglePin();
    void doLockSelected();
    void doNewIdea();
    void doExtractContent();
    void doOCR();
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
    void doImportCategory(int catId);
    void doImportFolder(int catId);
    void doExportCategory(int catId, const QString& catName);
    
    SearchLineEdit* m_searchEdit;
    QStackedWidget* m_listStack;
    CleanListView* m_listView;
    CategoryLockWidget* m_lockWidget;
    QWidget* m_appLockWidget = nullptr;
    NoteModel* m_model;
    
    DropTreeView* m_systemTree;
    DropTreeView* m_partitionTree;
    CategoryModel* m_systemModel;
    CategoryModel* m_partitionModel;
    QSortFilterProxyModel* m_systemProxyModel;
    QSortFilterProxyModel* m_partitionProxyModel;
    
    QTimer* m_searchTimer;
    QTimer* m_monitorTimer;
    QTimer* m_refreshTimer;
    QSplitter* m_splitter;
    QLabel* m_statusLabel;
    QStackedWidget* m_bottomStackedWidget;
    ClickableLineEdit* m_tagEdit;
    SearchLineEdit* m_catSearchEdit;
    QLineEdit* m_pageInput;
    QList<QShortcut*> m_shortcuts;

    int m_currentPage = 1;
    int m_totalPages = 1;
    QString m_currentFilterType = "all";
    QVariant m_currentFilterValue = -1;
    QString m_currentCategoryColor = "#4a90e2"; // 默认蓝色
    bool m_isStayOnTop = false;

#ifdef Q_OS_WIN
    HWND m_lastActiveHwnd = nullptr;
    HWND m_lastFocusHwnd = nullptr;
    DWORD m_lastThreadId = 0;
#endif
};

#endif // QUICKWINDOW_H