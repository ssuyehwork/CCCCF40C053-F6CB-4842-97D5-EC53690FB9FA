#include "IntegratedSearchWindow.h"
#include "FileSearchWindow.h"
#include "KeywordSearchWindow.h"
#include "IconHelper.h"
#include <QVBoxLayout>
#include <QTabBar>

IntegratedSearchWindow::IntegratedSearchWindow(QWidget* parent)
    : FramelessDialog("搜索中心", parent)
{
    setObjectName("IntegratedSearchWindow");
    loadWindowSettings();
    if (width() < 1200 || height() < 750) {
        resize(1200, 750);
    }
    setupStyles();
    initUI();
    m_resizeHandle = new ResizeHandle(this, this);
    m_resizeHandle->raise();
}

IntegratedSearchWindow::~IntegratedSearchWindow()
{
}

void IntegratedSearchWindow::setupStyles()
{
    setStyleSheet(R"(
        #IntegratedSearchWindow {
            background-color: #1E1E1E;
        }
        QTabWidget::pane {
            border: none;
            background-color: #1E1E1E;
        }
        QTabBar::tab {
            background-color: transparent;
            color: #888888;
            padding: 8px 16px;
            margin-right: 4px;
            font-size: 13px;
            border-bottom: 2px solid transparent;
        }
        QTabBar::tab:hover {
            background-color: #2D2D2D;
            color: #CCCCCC;
        }
        QTabBar::tab:selected {
            color: #007ACC;
            border-bottom: 2px solid #007ACC;
            font-weight: bold;
        }
    )");
}

void IntegratedSearchWindow::initUI()
{
    auto* layout = new QVBoxLayout(m_contentArea);
    layout->setContentsMargins(0, 5, 0, 0);
    layout->setSpacing(0);

    m_tabWidget = new QTabWidget(m_contentArea);
    m_tabWidget->setDocumentMode(true);

    m_fileSearchWidget = new FileSearchWidget(m_tabWidget);
    m_keywordSearchWidget = new KeywordSearchWidget(m_tabWidget);

    m_tabWidget->addTab(m_fileSearchWidget, IconHelper::getIcon("folder", "#007ACC", 16), "文件查找");
    m_tabWidget->addTab(m_keywordSearchWidget, IconHelper::getIcon("find_keyword", "#007ACC", 16), "关键字查找");

    layout->addWidget(m_tabWidget);
}

void IntegratedSearchWindow::setCurrentTab(SearchType type)
{
    m_tabWidget->setCurrentIndex(static_cast<int>(type));
}

void IntegratedSearchWindow::resizeEvent(QResizeEvent* event)
{
    FramelessDialog::resizeEvent(event);
    if (m_resizeHandle) {
        m_resizeHandle->move(width() - 20, height() - 20);
    }
}

#include "IntegratedSearchWindow.moc"
