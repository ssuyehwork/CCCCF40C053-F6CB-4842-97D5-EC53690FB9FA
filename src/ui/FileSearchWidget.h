#ifndef FILESEARCHWIDGET_H
#define FILESEARCHWIDGET_H

#include <QWidget>
#include <QListWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QThread>
#include <QLabel>
#include <atomic>

class FileSearchHistoryPopup;

/**
 * @brief 扫描线程：实现增量扫描与目录剪枝
 */
class ScannerThread : public QThread {
    Q_OBJECT
public:
    explicit ScannerThread(const QString& folderPath, QObject* parent = nullptr);
    void stop();

signals:
    void fileFound(const QString& name, const QString& path, bool isHidden);
    void finished(int count);

protected:
    void run() override;

private:
    QString m_folderPath;
    std::atomic<bool> m_isRunning{true};
};

/**
 * @brief 收藏侧边栏列表 (支持拖拽和多选)
 */
class FileCollectionListWidget : public QListWidget {
    Q_OBJECT
public:
    explicit FileCollectionListWidget(QWidget* parent = nullptr);
signals:
    void filesDropped(const QStringList& paths);
protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
};

/**
 * @brief 文件查找部件：包含侧边栏收藏与路径历史记录
 */
class FileSearchWidget : public QWidget {
    Q_OBJECT
public:
    explicit FileSearchWidget(QWidget* parent = nullptr);
    ~FileSearchWidget();

    // 历史记录操作接口 (供 Popup 调用)
    void addHistoryEntry(const QString& path);
    QStringList getHistory() const;
    void clearHistory();
    void removeHistoryEntry(const QString& path);
    void useHistoryPath(const QString& path);

    // 文件名搜索历史相关
    void addSearchHistoryEntry(const QString& text);
    QStringList getSearchHistory() const;
    void removeSearchHistoryEntry(const QString& text);
    void clearSearchHistory();

private slots:
    void selectFolder();
    void onPathReturnPressed();
    void startScan(const QString& path);
    void onFileFound(const QString& name, const QString& path, bool isHidden);
    void onScanFinished(int count);
    void refreshList();
    void showFileContextMenu(const QPoint& pos);
    void copySelectedFiles();
    void onEditFile();
    void onCutFile();
    void onDeleteFile();
    void onMergeSelectedFiles();
    void onMergeFolderContent();
    void onMergeCollectionFiles();
    
    // 侧边栏相关
    void onSidebarItemClicked(QListWidgetItem* item);
    void showSidebarContextMenu(const QPoint& pos);
    void addFavorite(const QString& path);

    // 收藏侧边栏 (右侧)
    void onCollectionItemClicked(QListWidgetItem* item);
    void showCollectionContextMenu(const QPoint& pos);
    void addCollectionItem(const QString& path);

public:
    void updateShortcuts();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void initUI();
    void setupStyles();
    void loadFavorites();
    void saveFavorites();
    void loadCollection();
    void saveCollection();
    void onMergeFiles(const QStringList& filePaths, const QString& rootPath, bool useCombineDir = false);

    QListWidget* m_sidebar;
    FileCollectionListWidget* m_collectionSidebar;
    QAction* m_actionSelectAll = nullptr;
    QAction* m_actionCopy = nullptr;
    QAction* m_actionDelete = nullptr;
    QAction* m_actionScan = nullptr;
    QLineEdit* m_pathInput;
    QLineEdit* m_searchInput;
    QLineEdit* m_extInput;
    QLabel* m_infoLabel;
    QCheckBox* m_showHiddenCheck;
    QListWidget* m_fileList;
    
    ScannerThread* m_scanThread = nullptr;
    FileSearchHistoryPopup* m_pathPopup = nullptr;
    FileSearchHistoryPopup* m_searchPopup = nullptr;
    
    struct FileData {
        QString name;
        QString path;
        bool isHidden;
    };
    QList<FileData> m_filesData;
    int m_visibleCount = 0;
    int m_hiddenCount = 0;
};

#endif // FILESEARCHWIDGET_H
