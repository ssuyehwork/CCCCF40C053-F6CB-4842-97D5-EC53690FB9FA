#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTreeView>
#include <QListView>
#include <QSplitter>
#include <QStandardItemModel>
#include <QTimer>
#include <QFileInfo>
#include "../models/NoteModel.h"
#include "../models/CategoryModel.h"
#include "Editor.h"
#include "NoteEditWindow.h"
#include "HeaderBar.h"
#include "MetadataPanel.h"
#include "QuickPreview.h"
#include "DropTreeView.h"
#include "FilterPanel.h"
#include "CategoryLockWidget.h"

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
    void toolboxRequested();
    void globalLockRequested();

private slots:
    void onSelectionChanged(const QItemSelection& selected, const QItemSelection& deselected);
    void onTagSelected(const QModelIndex& index);
    void showContextMenu(const QPoint& pos);
    
    // Layout persistence
    void saveLayout();
    void restoreLayout();
    void updateShortcuts();

    // 【新增】处理单条笔记添加，不刷新全表
    void onNoteAdded(const QVariantMap& note);
    
    void refreshData();
    void scheduleRefresh();
    void doPreview();
    void updatePreviewContent();
    void showToolboxMenu(const QPoint& pos);

    // 快捷键处理与操作逻辑 (同步 QuickWindow)
    void doDeleteSelected(bool physical = false);
    void doToggleFavorite();
    void doTogglePin();
    void doLockSelected();
    void doNewIdea();
    void doExtractContent();
    void doOCR();
    void doEditSelected();
    void doSetRating(int rating);
    void doMoveToCategory(int catId);
    void doCopyTags();
    void doPasteTags();
    void doImportCategory(int catId);
    void doImportFolder(int catId);
    void doExportCategory(int catId, const QString& catName);

protected:
#ifdef Q_OS_WIN
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;
#endif
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
    
    DropTreeView* m_systemTree;
    CategoryModel* m_systemModel;
    DropTreeView* m_partitionTree;
    CategoryModel* m_partitionModel;
    QWidget* m_sidebarContainer;
    
    QListView* m_noteList;
    NoteModel* m_noteModel;

    HeaderBar* m_header;
    MetadataPanel* m_metaPanel;
    FilterPanel* m_filterPanel;
    QWidget* m_filterWrapper;
    
    Editor* m_editor;
    CategoryLockWidget* m_lockWidget;
    QPushButton* m_editBtn;

    QString m_currentKeyword;
    QString m_currentFilterType = "all";
    QVariant m_currentFilterValue = -1;
    int m_currentPage = 1;
    int m_pageSize = 100;
    QTimer* m_searchTimer;
    QTimer* m_refreshTimer;
};

#endif // MAINWINDOW_H