#ifndef INTEGRATEDSEARCHWINDOW_H
#define INTEGRATEDSEARCHWINDOW_H

#include "FramelessDialog.h"
#include "ResizeHandle.h"
#include <QTabWidget>
#include <QListWidget>
#include <QSplitter>

class FileSearchWidget;
class KeywordSearchWidget;

/**
 * @brief 集成搜索窗口：合并文件查找与关键字查找
 */
class IntegratedSearchWindow : public FramelessDialog {
    Q_OBJECT
public:
    enum SearchType {
        FileSearch = 0,
        KeywordSearch = 1
    };

    explicit IntegratedSearchWindow(QWidget* parent = nullptr);
    ~IntegratedSearchWindow();

    void setCurrentTab(SearchType type);

protected:
    void resizeEvent(QResizeEvent* event) override;

private slots:
    // 侧边栏相关
    void onSidebarItemClicked(QListWidgetItem* item);
    void showSidebarContextMenu(const QPoint& pos);
    void addFavorite(const QString& path);

    // 收藏相关
    void onCollectionItemClicked(QListWidgetItem* item);
    void showCollectionContextMenu(const QPoint& pos);
    void addCollectionItem(const QString& path);
    void onMergeCollectionFiles();

private:
    void initUI();
    void setupStyles();
    void loadFavorites();
    void saveFavorites();
    void loadCollection();
    void saveCollection();
    void onMergeFiles(const QStringList& filePaths, const QString& rootPath, bool useCombineDir = false);

    QTabWidget* m_tabWidget;
    FileSearchWidget* m_fileSearchWidget;
    KeywordSearchWidget* m_keywordSearchWidget;

    QListWidget* m_sidebar;
    QListWidget* m_collectionSidebar;

    ResizeHandle* m_resizeHandle;
};

#endif // INTEGRATEDSEARCHWINDOW_H
