#ifndef KEYWORDSEARCHWINDOW_H
#define KEYWORDSEARCHWINDOW_H

#include "FramelessDialog.h"
#include "ClickableLineEdit.h"
#include "ResizeHandle.h"
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QTextBrowser>
#include <QProgressBar>
#include <QLabel>
#include <QListWidget>
#include <QSplitter>

/**
 * @brief 关键字搜索核心组件
 */
class KeywordSearchWidget : public QWidget {
    Q_OBJECT
public:
    explicit KeywordSearchWidget(QWidget* parent = nullptr);
    ~KeywordSearchWidget();

    // 暴露路径编辑框以便 UnifiedSearchWindow 同步
    ClickableLineEdit* m_pathEdit;

private slots:
    void onBrowseFolder();
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

    friend class UnifiedSearchWindow;
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
    ResizeHandle* m_resizeHandle;
};

#endif // KEYWORDSEARCHWINDOW_H
