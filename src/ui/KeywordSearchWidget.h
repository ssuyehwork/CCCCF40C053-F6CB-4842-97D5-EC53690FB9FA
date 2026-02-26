#ifndef KEYWORDSEARCHWIDGET_H
#define KEYWORDSEARCHWIDGET_H

#include <QWidget>
#include "ClickableLineEdit.h"
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QProgressBar>
#include <QLabel>
#include <QListWidget>

/**
 * @brief 关键字搜索核心组件
 */
class KeywordSearchWidget : public QWidget {
    Q_OBJECT
public:
    explicit KeywordSearchWidget(QWidget* parent = nullptr);
    ~KeywordSearchWidget();

    void setSearchPath(const QString& path);
    QString currentPath() const;

signals:
    void requestAddFileFavorite(const QStringList& paths);
    void requestAddFolderFavorite(const QString& path);

private slots:
    void onBrowseFolder();
    void onSearch();
    void onReplace();
    void onUndo();
    void onClearLog();
    void onResultDoubleClicked(const QModelIndex& index);
    void onShowHistory();
    void onSwapSearchReplace();

    // 同步自 FileSearchWidget 的功能槽函数
    void onEditFile();
    void copySelectedFiles();
    void onCutFile();
    void onDeleteFile();
    void onMergeSelectedFiles();

private:
    void initUI();
    void setupStyles();
    
    // 历史记录管理
    enum HistoryType { Path, Keyword, Replace };
    void addHistoryEntry(HistoryType type, const QString& text);
    bool isTextFile(const QString& filePath);
    void log(const QString& msg, const QString& type = "info", int count = 0);
    void showResultContextMenu(const QPoint& pos);

    // 合并功能辅助
    void onMergeFiles(const QStringList& filePaths, const QString& rootPath);

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
};

#endif // KEYWORDSEARCHWIDGET_H
