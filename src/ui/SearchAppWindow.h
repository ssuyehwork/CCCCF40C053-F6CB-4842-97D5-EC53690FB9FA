#ifndef SEARCHAPPWINDOW_H
#define SEARCHAPPWINDOW_H

#include "FramelessDialog.h"
#include <QTabWidget>
#include <QListWidget>

class FileSearchWidget;
class KeywordSearchWidget;

/**
 * @brief 共享文件夹侧边栏 (接受目录拖入)
 */
class SharedSidebarListWidget : public QListWidget {
    Q_OBJECT
public:
    explicit SharedSidebarListWidget(QWidget* parent = nullptr);
signals:
    void folderDropped(const QString& path);
protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
};

/**
 * @brief 共享文件收藏侧边栏 (接受文件拖入)
 */
class SharedCollectionListWidget : public QListWidget {
    Q_OBJECT
public:
    explicit SharedCollectionListWidget(QWidget* parent = nullptr);
signals:
    void filesDropped(const QStringList& paths);
protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
};

/**
 * @brief 合并后的搜索主窗口，支持文件查找和关键字查找切换，并共享左右侧边栏
 */
class SearchAppWindow : public FramelessDialog {
    Q_OBJECT
public:
    explicit SearchAppWindow(QWidget* parent = nullptr);
    ~SearchAppWindow();

    void switchToFileSearch();
    void switchToKeywordSearch();

    // 暴露给子组件的接口
    void addCollectionItem(const QString& path);
    void addCollectionItems(const QStringList& paths);

private slots:
    void onSidebarItemClicked(class QListWidgetItem* item);
    void showSidebarContextMenu(const QPoint& pos);
    void onCollectionItemClicked(class QListWidgetItem* item);
    void showCollectionContextMenu(const QPoint& pos);
    void onMergeCollectionFiles();
    void onFavoriteCurrentPath();

protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void initUI();
    void setupStyles();
    void loadFavorites();
    void saveFavorites();
    void loadCollection();
    void saveCollection();
    void addFavorite(const QString& path);

    QTabWidget* m_tabWidget;
    FileSearchWidget* m_fileSearchWidget;
    KeywordSearchWidget* m_keywordSearchWidget;

    SharedSidebarListWidget* m_sidebar;
    SharedCollectionListWidget* m_collectionSidebar;
};

#endif // SEARCHAPPWINDOW_H
