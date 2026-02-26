#ifndef SEARCH_APP_WINDOW_RAPID_NOTES_H
#define SEARCH_APP_WINDOW_RAPID_NOTES_H

#include "FramelessDialog.h"
#include <QTabWidget>
#include <QtGlobal>

class FileSearchWidget;
class KeywordSearchWidget;

class SearchAppWindow : public FramelessDialog {
    Q_OBJECT
public:
    explicit SearchAppWindow(QWidget* parent = nullptr);
    virtual ~SearchAppWindow();

    void switchToFileSearch();
    void switchToKeywordSearch();

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void initUI();
    void setupStyles();

    QTabWidget* m_tabWidget;
    FileSearchWidget* m_fileSearchWidget;
    KeywordSearchWidget* m_keywordSearchWidget;
};

#endif // SEARCH_APP_WINDOW_RAPID_NOTES_H
