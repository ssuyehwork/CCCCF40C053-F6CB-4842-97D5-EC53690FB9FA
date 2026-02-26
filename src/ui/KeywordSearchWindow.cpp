#include "KeywordSearchWindow.h"
#include "IconHelper.h"
#include "ToolTipOverlay.h"
#include "ResizeHandle.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFileDialog>
#include <QDirIterator>
#include <QTextStream>
#include <QRegularExpression>
#include <QDateTime>
#include <QtConcurrent>
#include <QSettings>
#include <QApplication>
#include <QClipboard>
#include <QToolButton>

// ----------------------------------------------------------------------------
// KeywordSearchWidget
// ----------------------------------------------------------------------------
KeywordSearchWidget::KeywordSearchWidget(QWidget* parent) : QWidget(parent) {
    setupStyles();
    initUI();
}
KeywordSearchWidget::~KeywordSearchWidget() {}

void KeywordSearchWidget::setupStyles() {
    setStyleSheet(R"(
        QLabel { color: #AAA; font-weight: bold; font-size: 13px; }
        QLineEdit { background: #252526; border: 1px solid #333; border-radius: 4px; padding: 6px; color: #EEE; }
        QLineEdit:focus { border-color: #007ACC; }
        QCheckBox { color: #AAA; font-size: 12px; }
        #SearchBtn { background: #007ACC; color: white; border: none; border-radius: 4px; padding: 8px 20px; font-weight: bold; }
        #ReplaceBtn { background: #D32F2F; color: white; border: none; border-radius: 4px; padding: 8px 20px; font-weight: bold; }
        #NormalBtn { background: #3E3E42; color: #EEE; border: none; border-radius: 4px; padding: 8px 20px; }
    )");
}

void KeywordSearchWidget::initUI() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(15);

    auto* configGrid = new QGridLayout();
    configGrid->setColumnStretch(1, 1);
    configGrid->setHorizontalSpacing(10);
    configGrid->setVerticalSpacing(10);

    configGrid->addWidget(new QLabel("搜索目录:"), 0, 0);
    m_pathEdit = new ClickableLineEdit();
    m_pathEdit->setPlaceholderText("选择搜索根目录...");
    configGrid->addWidget(m_pathEdit, 0, 1);
    auto* browseBtn = new QPushButton();
    browseBtn->setFixedSize(32, 32);
    browseBtn->setIcon(IconHelper::getIcon("folder", "#EEE", 18));
    browseBtn->setStyleSheet("background: #3E3E42; border-radius: 4px;");
    connect(browseBtn, &QPushButton::clicked, this, &KeywordSearchWidget::onBrowseFolder);
    configGrid->addWidget(browseBtn, 0, 2);

    configGrid->addWidget(new QLabel("文件过滤:"), 1, 0);
    m_filterEdit = new QLineEdit();
    m_filterEdit->setPlaceholderText("例如: *.py, *.txt");
    configGrid->addWidget(m_filterEdit, 1, 1, 1, 2);

    configGrid->addWidget(new QLabel("查找内容:"), 2, 0);
    m_searchEdit = new ClickableLineEdit();
    m_searchEdit->setPlaceholderText("输入要查找的内容...");
    configGrid->addWidget(m_searchEdit, 2, 1);

    configGrid->addWidget(new QLabel("替换内容:"), 3, 0);
    m_replaceEdit = new ClickableLineEdit();
    m_replaceEdit->setPlaceholderText("替换为...");
    configGrid->addWidget(m_replaceEdit, 3, 1);

    auto* swapBtn = new QPushButton();
    swapBtn->setFixedSize(32, 74);
    swapBtn->setIcon(IconHelper::getIcon("swap", "#AAA", 20));
    swapBtn->setStyleSheet("background: #3E3E42; border-radius: 4px;");
    connect(swapBtn, &QPushButton::clicked, this, &KeywordSearchWidget::onSwapSearchReplace);
    configGrid->addWidget(swapBtn, 2, 2, 2, 1);

    m_caseCheck = new QCheckBox("区分大小写");
    configGrid->addWidget(m_caseCheck, 4, 1);
    mainLayout->addLayout(configGrid);

    auto* btnLayout = new QHBoxLayout();
    auto* searchBtn = new QPushButton(" 智能搜索");
    searchBtn->setObjectName("SearchBtn");
    searchBtn->setIcon(IconHelper::getIcon("find_keyword", "#FFF", 16));
    connect(searchBtn, &QPushButton::clicked, this, &KeywordSearchWidget::onSearch);

    auto* replaceBtn = new QPushButton(" 执行替换");
    replaceBtn->setObjectName("ReplaceBtn");
    replaceBtn->setIcon(IconHelper::getIcon("edit", "#FFF", 16));
    connect(replaceBtn, &QPushButton::clicked, this, &KeywordSearchWidget::onReplace);

    auto* undoBtn = new QPushButton(" 撤销替换");
    undoBtn->setObjectName("NormalBtn");
    undoBtn->setIcon(IconHelper::getIcon("undo", "#EEE", 16));
    connect(undoBtn, &QPushButton::clicked, this, &KeywordSearchWidget::onUndo);

    auto* clearBtn = new QPushButton(" 清空日志");
    clearBtn->setObjectName("NormalBtn");
    clearBtn->setIcon(IconHelper::getIcon("trash", "#EEE", 16));
    connect(clearBtn, &QPushButton::clicked, this, &KeywordSearchWidget::onClearLog);

    btnLayout->addWidget(searchBtn);
    btnLayout->addWidget(replaceBtn);
    btnLayout->addWidget(undoBtn);
    btnLayout->addWidget(clearBtn);
    btnLayout->addStretch();
    mainLayout->addLayout(btnLayout);

    m_resultList = new QListWidget();
    m_resultList->setStyleSheet("background: #1E1E1E; border: 1px solid #333; border-radius: 4px; color: #CCC;");
    mainLayout->addWidget(m_resultList, 1);

    m_statusLabel = new QLabel("就绪");
    m_statusLabel->setStyleSheet("color: #666; font-size: 11px;");
    mainLayout->addWidget(m_statusLabel);
}

void KeywordSearchWidget::setPath(const QString& path) { m_pathEdit->setText(path); }
void KeywordSearchWidget::onBrowseFolder() {
    QString f = QFileDialog::getExistingDirectory(this, "选择目录");
    if (!f.isEmpty()) setPath(f);
}
void KeywordSearchWidget::onSwapSearchReplace() {
    QString t = m_searchEdit->text(); m_searchEdit->setText(m_replaceEdit->text()); m_replaceEdit->setText(t);
}
void KeywordSearchWidget::onSearch() {}
void KeywordSearchWidget::onReplace() {}
void KeywordSearchWidget::onUndo() {}
void KeywordSearchWidget::onClearLog() { m_resultList->clear(); m_statusLabel->setText("就绪"); }
void KeywordSearchWidget::onShowHistory() {}
void KeywordSearchWidget::copySelectedPaths() {}
void KeywordSearchWidget::showResultContextMenu(const QPoint&) {}
void KeywordSearchWidget::onMergeFiles(const QStringList&, const QString&, bool) {}
void KeywordSearchWidget::addHistoryEntry(HistoryType, const QString&) {}
bool KeywordSearchWidget::isTextFile(const QString&) { return true; }

// ----------------------------------------------------------------------------
// KeywordSearchWindow
// ----------------------------------------------------------------------------
KeywordSearchWindow::KeywordSearchWindow(QWidget* parent) : FramelessDialog("查找关键字", parent) {
    resize(1000, 700);
    m_searchWidget = new KeywordSearchWidget(m_contentArea);
    auto* layout = new QVBoxLayout(m_contentArea);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_searchWidget);
    m_resizeHandle = new ResizeHandle(this, this);
}
void KeywordSearchWindow::resizeEvent(QResizeEvent* event) {
    FramelessDialog::resizeEvent(event);
    if (m_resizeHandle) m_resizeHandle->move(width() - 20, height() - 20);
}
