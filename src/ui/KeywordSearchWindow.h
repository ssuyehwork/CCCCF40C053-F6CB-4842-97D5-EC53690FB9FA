#ifndef KEYWORDSEARCHWINDOW_H
#define KEYWORDSEARCHWINDOW_H

#include "FramelessDialog.h"
#include "ClickableLineEdit.h"
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QTextBrowser>
#include <QProgressBar>
#include <QLabel>
#include <QListWidget>
#include <QSplitter>

class KeywordSidebarListWidget;

/**
 * @brief 收藏侧边栏列表 (支持拖拽和多选) - 复刻自 FileSearchWindow
 */
class KeywordCollectionListWidget : public QListWidget {
    Q_OBJECT
public:
    explicit KeywordCollectionListWidget(QWidget* parent = nullptr);
signals:
    void filesDropped(const QStringList& paths);
protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
};

/**
 * @brief 关键字搜索核心组件
 */
class KeywordSearchWidget : public QWidget {
    Q_OBJECT
public:
    explicit KeywordSearchWidget(QWidget* parent = nullptr);
    ~KeywordSearchWidget();

private slots:
    void onBrowseFolder();
    void onSidebarItemClicked(QListWidgetItem* item);
    void showSidebarContextMenu(const QPoint& pos);
    void addFavorite(const QString& path);
    void onSearch();
    void onReplace();
    void onUndo();
    void onClearLog();
    void onShowHistory();
    void onSwapSearchReplace();
    void updateShortcuts();

    // 结果列表相关 slots
    void showResultContextMenu(const QPoint& pos);
    void onEditFile();
    void onMergeSelectedFiles();
    void onMergeCollectionFiles();
    void copySelectedPaths();
    void copySelectedFiles();

    // 收藏相关
    void onCollectionItemClicked(QListWidgetItem* item);
    void showCollectionContextMenu(const QPoint& pos);
    void addCollectionItem(const QString& path);

private:
    void initUI();
    void setupStyles();
    void loadFavorites();
    void saveFavorites();
    void loadCollection();
    void saveCollection();
    void onMergeFiles(const QStringList& filePaths, const QString& rootPath, bool useCombineDir = false);
    
    // 历史记录管理
    enum HistoryType { Path, Keyword, Replace };
    void addHistoryEntry(HistoryType type, const QString& text);
    bool isTextFile(const QString& filePath);

    QListWidget* m_sidebar;
    KeywordCollectionListWidget* m_collectionSidebar;
    QAction* m_actionSearch = nullptr;
    QAction* m_actionReplace = nullptr;
    QAction* m_actionUndo = nullptr;
    QAction* m_actionSwap = nullptr;
    QAction* m_actionCopyPaths = nullptr;
    QAction* m_actionCopyFiles = nullptr;
    QAction* m_actionSelectAll = nullptr;

    ClickableLineEdit* m_pathEdit;
    QLineEdit* m_filterEdit;
    ClickableLineEdit* m_searchEdit;
    ClickableLineEdit* m_replaceEdit;
    QCheckBox* m_caseCheck;
    QListWidget* m_resultList;
    QProgressBar* m_progressBar;
    QLabel* m_statusLabel;

    QString m_lastBackupPath;
    QStringList m_ignoreDirs;

    struct MatchData {
        QString path;
        int count;
    };
    QList<MatchData> m_resultsData;
};

/**
 * @brief 关键字搜索窗口：封装了 KeywordSearchWidget
 */
class KeywordSearchWindow : public FramelessDialog {
    Q_OBJECT
public:
    explicit KeywordSearchWindow(QWidget* parent = nullptr);
    ~KeywordSearchWindow();

protected:
    void hideEvent(QHideEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    KeywordSearchWidget* m_searchWidget;
    class ResizeHandle* m_resizeHandle;
};

#endif // KEYWORDSEARCHWINDOW_H
