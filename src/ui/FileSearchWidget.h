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
 * @brief 文件查找部件：核心搜索逻辑，UI 仅保留搜索参数与结果列表
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

    void updateShortcuts();
    void setPath(const QString& path);
    QString getCurrentPath() const;

    // 暴露合并接口给主窗口
    void onMergeFiles(const QStringList& filePaths, const QString& rootPath, bool useCombineDir = false);

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

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void initUI();
    void setupStyles();

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
