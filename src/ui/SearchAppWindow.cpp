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

    m_resizeHandle = new ResizeHandle(this, this);
    m_resizeHandle->raise();
}

SearchAppWindow::~SearchAppWindow() {
}

void SearchAppWindow::setupStyles() {
    // 设置整体深色背景
    m_container->setStyleSheet(
        "#DialogContainer {"
        "  background-color: #181818;"
        "  border: 1px solid #333333;"
        "  border-radius: 12px;"
        "} " + StringUtils::getToolTipStyle()
    );

    // 调整内容区域背景
    m_contentArea->setStyleSheet("QWidget#DialogContentArea { background: transparent; border: none; }");
}

void SearchAppWindow::initUI() {
    auto* mainLayout = new QVBoxLayout(m_contentArea);
    mainLayout->setContentsMargins(10, 5, 10, 10);
    mainLayout->setSpacing(5);

    // --- 自定义标签栏 ---
    auto* tabHeaderLayout = new QHBoxLayout();
    tabHeaderLayout->setContentsMargins(0, 0, 0, 0);
    tabHeaderLayout->setSpacing(2);

    auto createTabBtn = [this](const QString& text, const QString& iconName) {
        auto* btn = new QPushButton(text);
        btn->setCheckable(true);
        btn->setAutoExclusive(true);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setIcon(IconHelper::getIcon(iconName, "#AAA", 16));
        btn->setFixedSize(110, 34);
        btn->setStyleSheet(R"(
            QPushButton {
                background-color: #2D2D30;
                color: #888;
                border: 1px solid #333;
                border-bottom: none;
                border-top-left-radius: 6px;
                border-top-right-radius: 6px;
                font-size: 13px;
                font-weight: bold;
                padding-left: 10px;
                text-align: left;
            }
            QPushButton:hover {
                background-color: #3E3E42;
                color: #CCC;
            }
            QPushButton:checked {
                background-color: #1E1E1E;
                color: #007ACC;
                border-color: #333;
                border-bottom: 2px solid #1E1E1E;
            }
        )");
        return btn;
    };

    m_btnFileSearch = createTabBtn("文件查找", "folder");
    m_btnKeywordSearch = createTabBtn("关键字查找", "find_keyword");

    tabHeaderLayout->addWidget(m_btnFileSearch);
    tabHeaderLayout->addWidget(m_btnKeywordSearch);
    tabHeaderLayout->addStretch();
    mainLayout->addLayout(tabHeaderLayout);

    // --- 内容堆栈 ---
    m_stack = new QStackedWidget();
    m_stack->setStyleSheet("QStackedWidget { background-color: #1E1E1E; border: 1px solid #333; border-top: none; }");

    m_fileSearchWidget = new FileSearchWidget();
    m_keywordSearchWidget = new KeywordSearchWidget();

    m_stack->addWidget(m_fileSearchWidget);
    m_stack->addWidget(m_keywordSearchWidget);

    mainLayout->addWidget(m_stack, 1);

    // 信号连接
    connect(m_btnFileSearch, &QPushButton::clicked, this, &SearchAppWindow::switchToFileSearch);
    connect(m_btnKeywordSearch, &QPushButton::clicked, this, &SearchAppWindow::switchToKeywordSearch);

    // 默认选中第一个
    m_btnFileSearch->setChecked(true);
    switchToFileSearch();
}

void SearchAppWindow::switchToFileSearch() {
    m_stack->setCurrentWidget(m_fileSearchWidget);
    m_fileSearchWidget->updateShortcuts();
}

void SearchAppWindow::switchToKeywordSearch() {
    m_stack->setCurrentWidget(m_keywordSearchWidget);
    m_keywordSearchWidget->updateShortcuts();
}

void SearchAppWindow::resizeEvent(QResizeEvent* event) {
    FramelessDialog::resizeEvent(event);
    if (m_resizeHandle) {
        m_resizeHandle->move(width() - 20, height() - 20);
    }
}

void SearchAppWindow::showEvent(QShowEvent* event) {
    FramelessDialog::showEvent(event);
}

#include "SearchAppWindow.moc"
