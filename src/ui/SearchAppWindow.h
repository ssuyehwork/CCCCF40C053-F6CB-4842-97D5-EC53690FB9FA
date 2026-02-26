#ifndef SEARCHAPPWINDOW_H
#define SEARCHAPPWINDOW_H

#include "FramelessDialog.h"
#include "ResizeHandle.h"
#include <QStackedWidget>
#include <QPushButton>

class FileSearchWidget;
class KeywordSearchWidget;

/**
 * @brief 合并后的搜索主窗口，支持文件查找和关键字查找切换
 */
class SearchAppWindow : public FramelessDialog {
    Q_OBJECT
public:
    explicit SearchAppWindow(QWidget* parent = nullptr);
    ~SearchAppWindow();

    void switchToFileSearch();
    void switchToKeywordSearch();

protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void initUI();
    void setupStyles();

    QPushButton* m_btnFileSearch;
    QPushButton* m_btnKeywordSearch;
    QStackedWidget* m_stack;
    FileSearchWidget* m_fileSearchWidget;
    KeywordSearchWidget* m_keywordSearchWidget;
    ResizeHandle* m_resizeHandle;
};

#endif // SEARCHAPPWINDOW_H
