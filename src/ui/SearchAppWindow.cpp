#include "SearchAppWindow.h"
#include "FileSearchWidget.h"
#include "KeywordSearchWidget.h"
#include "IconHelper.h"
#include <QVBoxLayout>
#include <QTabWidget>

SearchAppWindow::SearchAppWindow(QWidget* parent) 
    : FramelessDialog("查找文件", parent) 
{
    setObjectName("SearchTool_SearchAppWindow_Standalone");
    resize(1100, 750);
    setupStyles();
    initUI();
}

SearchAppWindow::~SearchAppWindow() {
}

void SearchAppWindow::setupStyles() {
    m_tabWidget = new QTabWidget();
    m_tabWidget->setStyleSheet(R"(
        QTabWidget::pane {
            border: 1px solid #333;
            background: #1e1e1e;
            top: -1px;
            border-radius: 4px;
        }
        QTabBar::tab {
            background: #2D2D30;
            color: #AAA;
            padding: 10px 20px;
            border: 1px solid #333;
            border-bottom: none;
            border-top-left-radius: 6px;
            border-top-right-radius: 6px;
            margin-right: 2px;
        }
        QTabBar::tab:hover {
            background: #3E3E42;
            color: #EEE;
        }
        QTabBar::tab:selected {
            background: #1e1e1e;
            color: #007ACC;
            border-bottom: 1px solid #1e1e1e;
            font-weight: bold;
        }
    )");
}

void SearchAppWindow::initUI() {
    auto* layout = new QVBoxLayout(m_contentArea);
    layout->setContentsMargins(10, 5, 10, 10);
    layout->addWidget(m_tabWidget);

    m_fileSearchWidget = new FileSearchWidget();
    m_keywordSearchWidget = new KeywordSearchWidget();

    m_tabWidget->addTab(m_fileSearchWidget, IconHelper::getIcon("folder", "#AAA"), "文件查找");
    m_tabWidget->addTab(m_keywordSearchWidget, IconHelper::getIcon("find_keyword", "#AAA"), "关键字查找");
}

void SearchAppWindow::switchToFileSearch() {
    if (m_tabWidget && m_fileSearchWidget)
        m_tabWidget->setCurrentWidget(m_fileSearchWidget);
}

void SearchAppWindow::switchToKeywordSearch() {
    if (m_tabWidget && m_keywordSearchWidget)
        m_tabWidget->setCurrentWidget(m_keywordSearchWidget);
}

void SearchAppWindow::resizeEvent(QResizeEvent* event) {
    FramelessDialog::resizeEvent(event);
}
