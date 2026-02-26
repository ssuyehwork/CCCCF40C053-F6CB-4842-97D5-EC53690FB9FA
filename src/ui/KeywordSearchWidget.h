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
#include <QSplitter>

class KeywordSidebarListWidget;

/**
 * @brief 关键字搜索核心组件
 */
class KeywordSearchWidget : public QWidget {
    Q_OBJECT
public:
    explicit KeywordSearchWidget(QWidget* parent = nullptr);
    ~KeywordSearchWidget();

    void updateShortcuts();

private slots:
    void onBrowseFolder();
    void onSidebarItemClicked(QListWidgetItem* item);
    void showSidebarContextMenu(const QPoint& pos);
    void addFavorite(const QString& path, bool pinned = false);
    void onSearch();
    void onReplace();
    void onUndo();
    void onClearLog();
    void onResultDoubleClicked(const QModelIndex& index);
    void onShowHistory();
    void onSwapSearchReplace();

private:
    void initUI();
    void setupStyles();
    void loadFavorites();
    void saveFavorites();
    
    // 历史记录管理
    enum HistoryType { Path, Keyword, Replace };
    void addHistoryEntry(HistoryType type, const QString& text);
    bool isTextFile(const QString& filePath);
    void log(const QString& msg, const QString& type = "info");
    void highlightResult(const QString& keyword);

    QListWidget* m_sidebar;
    ClickableLineEdit* m_pathEdit;
    QLineEdit* m_filterEdit;
    ClickableLineEdit* m_searchEdit;
    ClickableLineEdit* m_replaceEdit;
    QCheckBox* m_caseCheck;
    QTextBrowser* m_logDisplay;
    QProgressBar* m_progressBar;
    QLabel* m_statusLabel;

    QString m_lastBackupPath;
    QStringList m_ignoreDirs;
};

#endif // KEYWORDSEARCHWIDGET_H
