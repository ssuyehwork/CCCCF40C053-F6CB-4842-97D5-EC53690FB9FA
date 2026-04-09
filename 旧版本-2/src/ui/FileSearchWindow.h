#ifndef FILESEARCHWINDOW_H
#define FILESEARCHWINDOW_H

#include "FramelessDialog.h"
#include <QListWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QThread>
#include <QPair>
#include <QSplitter>
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
 * @brief 隐形调整大小手柄
 */
class ResizeHandle : public QWidget {
    Q_OBJECT
public:
    explicit ResizeHandle(QWidget* target, QWidget* parent = nullptr);
protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
private:
    QWidget* m_target;
    QPoint m_startPos;
    QSize m_startSize;
};

/**
 * @brief 文件查找窗口：新增侧边栏收藏与路径历史记录
 */
class FileSearchWindow : public FramelessDialog {
    Q_OBJECT
public:
    explicit FileSearchWindow(QWidget* parent = nullptr);
    ~FileSearchWindow();

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
    
    // 侧边栏相关
    void onSidebarItemClicked(QListWidgetItem* item);
    void showSidebarContextMenu(const QPoint& pos);
    void addFavorite(const QString& path);

protected:
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void initUI();
    void setupStyles();
    void loadFavorites();
    void saveFavorites();
    void onMergeFiles(const QStringList& filePaths, const QString& rootPath);

    QListWidget* m_sidebar;
    QLineEdit* m_pathInput;
    QLineEdit* m_searchInput;
    QLineEdit* m_extInput;
    QLabel* m_infoLabel;
    QCheckBox* m_showHiddenCheck;
    QListWidget* m_fileList;
    
    ResizeHandle* m_resizeHandle;
    ScannerThread* m_scanThread = nullptr;
    FileSearchHistoryPopup* m_historyPopup = nullptr;
    
    struct FileData {
        QString name;
        QString path;
        bool isHidden;
    };
    QList<FileData> m_filesData;
    int m_visibleCount = 0;
    int m_hiddenCount = 0;
};

#endif // FILESEARCHWINDOW_H
