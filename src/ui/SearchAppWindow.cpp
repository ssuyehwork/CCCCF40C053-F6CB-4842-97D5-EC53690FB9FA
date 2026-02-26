#include "SearchAppWindow.h"
#include "FileSearchWidget.h"
#include "KeywordSearchWidget.h"
#include "IconHelper.h"
#include "StringUtils.h"
#include <QVBoxLayout>
#include <QHBoxLayout>

SearchAppWindow::SearchAppWindow(QWidget* parent)
    : FramelessDialog("综合搜索工具", parent)
{
    setObjectName("SearchAppWindow");
    loadWindowSettings();
    resize(1200, 800);
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
    auto* mainLayout = new QVBoxLayout(m_contentArea);
    mainLayout->setContentsMargins(10, 5, 10, 10);
    mainLayout->setSpacing(0);
    mainLayout->addWidget(m_tabWidget);

    m_fileSearchWidget = new FileSearchWidget();
    m_keywordSearchWidget = new KeywordSearchWidget();

    m_tabWidget->addTab(m_fileSearchWidget, IconHelper::getIcon("folder", "#AAA"), "文件查找");
    m_tabWidget->addTab(m_keywordSearchWidget, IconHelper::getIcon("find_keyword", "#AAA"), "关键字查找");

    connect(m_tabWidget, &QTabWidget::currentChanged, this, [this](int index){
        if (auto* w = qobject_cast<FileSearchWidget*>(m_tabWidget->widget(index))) w->updateShortcuts();
        else if (auto* w = qobject_cast<KeywordSearchWidget*>(m_tabWidget->widget(index))) w->updateShortcuts();
    });
}

void SearchAppWindow::switchToFileSearch() {
    m_tabWidget->setCurrentWidget(m_fileSearchWidget);
    m_fileSearchWidget->updateShortcuts();
}

void SearchAppWindow::switchToKeywordSearch() {
    m_tabWidget->setCurrentWidget(m_keywordSearchWidget);
    m_keywordSearchWidget->updateShortcuts();
}

void SearchAppWindow::resizeEvent(QResizeEvent* event) {
    FramelessDialog::resizeEvent(event);
}

void SearchAppWindow::showEvent(QShowEvent* event) {
    FramelessDialog::showEvent(event);
}
