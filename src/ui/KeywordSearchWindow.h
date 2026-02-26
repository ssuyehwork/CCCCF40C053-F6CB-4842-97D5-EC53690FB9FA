#ifndef KEYWORDSEARCHWINDOW_H
#define KEYWORDSEARCHWINDOW_H

#include "FramelessDialog.h"
#include "ClickableLineEdit.h"
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QProgressBar>
#include <QLabel>
#include <QListWidget>

class KeywordSearchWidget : public QWidget {
    Q_OBJECT
public:
    explicit KeywordSearchWidget(QWidget* parent = nullptr);
    ~KeywordSearchWidget();

    void setPath(const QString& path);

private slots:
    void onBrowseFolder();
    void onSearch();
    void onReplace();
    void onUndo();
    void onClearLog();
    void onShowHistory();
    void onSwapSearchReplace();
    void copySelectedPaths();
    void showResultContextMenu(const QPoint& pos);

private:
    void initUI();
    void setupStyles();
    void onMergeFiles(const QStringList& filePaths, const QString& rootPath, bool useCombineDir = false);
    
    enum HistoryType { Path, Keyword, Replace };
    void addHistoryEntry(HistoryType type, const QString& text);
    bool isTextFile(const QString& filePath);

    ClickableLineEdit* m_pathEdit;
    QLineEdit* m_filterEdit;
    ClickableLineEdit* m_searchEdit;
    ClickableLineEdit* m_replaceEdit;
    QCheckBox* m_caseCheck;
    QListWidget* m_resultList;
    QProgressBar* m_progressBar;
    QLabel* m_statusLabel;
    
    QString m_lastBackupPath;
    struct MatchData { QString path; int count; };
    QList<MatchData> m_resultsData;
};

class KeywordSearchWindow : public FramelessDialog {
    Q_OBJECT
public:
    explicit KeywordSearchWindow(QWidget* parent = nullptr);
protected:
    void resizeEvent(QResizeEvent* event) override;
private:
    KeywordSearchWidget* m_searchWidget;
    class ResizeHandle* m_resizeHandle;
};

#endif // KEYWORDSEARCHWINDOW_H
