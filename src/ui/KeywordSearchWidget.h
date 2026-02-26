#ifndef KEYWORDSEARCHWIDGET_H
#define KEYWORDSEARCHWIDGET_H

#include <QWidget>
#include "ClickableLineEdit.h"
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QTextBrowser>
#include <QProgressBar>
#include <QLabel>
#include <QListWidget>

/**
 * @brief 关键字搜索核心组件，UI 仅保留搜索参数与结果列表
 */
class KeywordSearchWidget : public QWidget {
    Q_OBJECT
public:
    explicit KeywordSearchWidget(QWidget* parent = nullptr);
    ~KeywordSearchWidget();

    void updateShortcuts();
    void setPath(const QString& path);
    QString getCurrentPath() const;

    // 暴露合并接口给主窗口
    void onMergeFiles(const QStringList& filePaths, const QString& rootPath, bool useCombineDir = false);

private slots:
    void onBrowseFolder();
    void onSearch();
    void onReplace();
    void onUndo();
    void onClearLog();
    void onShowHistory();
    void onSwapSearchReplace();

    // 结果列表相关 slots
    void showResultContextMenu(const QPoint& pos);
    void onEditFile();
    void onMergeSelectedFiles();
    void copySelectedPaths();
    void copySelectedFiles();

private:
    void initUI();
    void setupStyles();
    
    // 历史记录管理
    enum HistoryType { Path, Keyword, Replace };
    void addHistoryEntry(HistoryType type, const QString& text);
    bool isTextFile(const QString& filePath);

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

#endif // KEYWORDSEARCHWIDGET_H
