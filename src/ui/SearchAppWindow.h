#ifndef SEARCHAPPWINDOW_H
#define SEARCHAPPWINDOW_H

#include "FramelessDialog.h"
#include <QTabWidget>
#include <QListWidget>

class FileSearchWidget;
class KeywordSearchWidget;

class SearchAppWindow : public FramelessDialog {
    Q_OBJECT
public:
    explicit SearchAppWindow(QWidget* parent = nullptr);
    ~SearchAppWindow();

public slots:
    void addFolderFavorite(const QString& path, bool pinned = false);
    void addFileFavorite(const QStringList& paths);
    void switchToFileSearch();
    void switchToKeywordSearch();

protected:
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onSidebarItemClicked(QListWidgetItem* item);
    void showSidebarContextMenu(const QPoint& pos);
    void onFileFavoriteItemDoubleClicked(QListWidgetItem* item);
    void showFileFavoriteContextMenu(const QPoint& pos);
    void removeFileFavorite();

    // 新增同步功能槽函数 (用于右侧文件收藏列表)
    void onEditFile();
    void copySelectedFiles();
    void onCutFile();
    void onDeleteFile();
    void onMergeSelectedFiles();

private:
    void initUI();
    void setupStyles();
    void loadFolderFavorites();
    void saveFolderFavorites();
    void loadFileFavorites();
    void saveFileFavorites();

    // 合并功能辅助
    void onMergeFiles(const QStringList& filePaths, const QString& rootPath);

    QTabWidget* m_tabWidget;
    FileSearchWidget* m_fileSearchWidget;
    KeywordSearchWidget* m_keywordSearchWidget;

    QListWidget* m_folderSidebar;
    QListWidget* m_fileFavoritesList;
};

#endif // SEARCHAPPWINDOW_H
