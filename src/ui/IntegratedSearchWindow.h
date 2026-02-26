#ifndef INTEGRATEDSEARCHWINDOW_H
#define INTEGRATEDSEARCHWINDOW_H

#include "FramelessDialog.h"
#include "ResizeHandle.h"
#include <QTabWidget>

class FileSearchWidget;
class KeywordSearchWidget;

/**
 * @brief 集成搜索窗口：合并文件查找与关键字查找
 */
class IntegratedSearchWindow : public FramelessDialog {
    Q_OBJECT
public:
    enum SearchType {
        FileSearch = 0,
        KeywordSearch = 1
    };

    explicit IntegratedSearchWindow(QWidget* parent = nullptr);
    ~IntegratedSearchWindow();

    void setCurrentTab(SearchType type);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void initUI();
    void setupStyles();

    QTabWidget* m_tabWidget;
    FileSearchWidget* m_fileSearchWidget;
    KeywordSearchWidget* m_keywordSearchWidget;
    ResizeHandle* m_resizeHandle;
};

#endif // INTEGRATEDSEARCHWINDOW_H
