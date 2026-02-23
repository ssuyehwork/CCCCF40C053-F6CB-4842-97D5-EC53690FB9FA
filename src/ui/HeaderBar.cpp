#include "HeaderBar.h"
#include "StringUtils.h"

#include "IconHelper.h"
#include <QHBoxLayout>
#include <QSettings>
#include <QMouseEvent>
#include <QApplication>
#include <QWindow>
#include <QIntValidator>

HeaderBar::HeaderBar(QWidget* parent) : QWidget(parent) {
    setFixedHeight(41); // 40 + 1px 线
    setStyleSheet("background-color: #252526; border: none;");

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // 顶部内容区
    auto* topContent = new QWidget();
    auto* layout = new QHBoxLayout(topContent);
    layout->setContentsMargins(10, 0, 10, 0);
    layout->setSpacing(0);
    
    // 1. Logo & Title
    QLabel* appLogo = new QLabel();
    appLogo->setFixedSize(18, 18);
    appLogo->setPixmap(IconHelper::getIcon("zap", "#4a90e2", 18).pixmap(18, 18));
    layout->addWidget(appLogo);
    layout->addSpacing(6);

    QLabel* titleLabel = new QLabel("快速笔记");
    titleLabel->setStyleSheet("font-size: 13px; font-weight: bold; color: #4a90e2; border: none; background: transparent;");
    layout->addWidget(titleLabel);
    layout->addSpacing(15);

    // 2. Search Box
    m_searchEdit = new SearchLineEdit();
    m_searchEdit->setPlaceholderText("搜索灵感 (双击查看历史)");
    m_searchEdit->setFixedWidth(280);
    m_searchEdit->setFixedHeight(28);
    m_searchEdit->setStyleSheet(
        "SearchLineEdit { "
        "  background-color: #1e1e1e; "
        "  border: 1px solid #444; "
        "  border-radius: 14px; "
        "  padding: 5px 15px; "
        "  color: white; "
        "  font-size: 13px; "
        "} "
        "SearchLineEdit:focus { border: 1px solid #4a90e2; background-color: #181818; }"
    );
    connect(m_searchEdit, &QLineEdit::textChanged, this, &HeaderBar::searchChanged);
    connect(m_searchEdit, &QLineEdit::returnPressed, [this](){
        m_searchEdit->addHistoryEntry(m_searchEdit->text().trimmed());
    });
    layout->addWidget(m_searchEdit);
    layout->addSpacing(15);

    // 3. Pagination Controls (保持原有逻辑)
    QString pageBtnStyle = 
        "QPushButton {"
        "    background-color: transparent;"
        "    border: 1px solid #555;"
        "    border-radius: 12px;"
        "    min-width: 24px;"
        "    max-width: 24px;"
        "    min-height: 24px;"
        "    max-height: 24px;"
        "    padding: 0px;"
        "}"
        "QPushButton:hover { background-color: #333; border-color: #777; }"
        "QPushButton:disabled { border-color: #333; }";

    auto createPageBtn = [&](const QString& icon, const QString& tip) {
        QPushButton* btn = new QPushButton();
        btn->setIcon(IconHelper::getIcon(icon, "#aaaaaa", 16));
        btn->setToolTip(tip);
        btn->setStyleSheet(pageBtnStyle);
        return btn;
    };

    QPushButton* btnFirst = createPageBtn("nav_first", "第一页");
    connect(btnFirst, &QPushButton::clicked, [this](){ emit pageChanged(1); });
    layout->addWidget(btnFirst);
    layout->addSpacing(6);

    QPushButton* btnPrev = createPageBtn("nav_prev", "上一页");
    connect(btnPrev, &QPushButton::clicked, [this](){ if(m_currentPage > 1) emit pageChanged(m_currentPage - 1); });
    layout->addWidget(btnPrev);
    layout->addSpacing(8);

    m_pageInput = new QLineEdit("1");
    m_pageInput->setFixedWidth(40);
    m_pageInput->setFixedHeight(24);
    m_pageInput->setAlignment(Qt::AlignCenter);
    m_pageInput->setValidator(new QIntValidator(1, 99999, this));
    m_pageInput->setStyleSheet(
        "QLineEdit {"
        "    background-color: #2D2D2D;"
        "    border: 1px solid #555;"
        "    border-radius: 12px;"
        "    color: #eee;"
        "    font-size: 11px;"
        "    padding: 0px;"
        "}"
        "QLineEdit:focus { border: 1px solid #4a90e2; }"
    );
    connect(m_pageInput, &QLineEdit::returnPressed, [this](){
        emit pageChanged(m_pageInput->text().toInt());
    });
    layout->addWidget(m_pageInput);
    layout->addSpacing(6);

    m_totalPageLabel = new QLabel("/ 1");
    m_totalPageLabel->setStyleSheet("color: #888; font-size: 12px; margin-left: 2px; margin-right: 5px; border: none; background: transparent;");
    layout->addWidget(m_totalPageLabel);
    layout->addSpacing(10);

    QPushButton* btnNext = createPageBtn("nav_next", "下一页");
    connect(btnNext, &QPushButton::clicked, [this](){ if(m_currentPage < m_totalPages) emit pageChanged(m_currentPage + 1); });
    layout->addWidget(btnNext);
    layout->addSpacing(6);

    QPushButton* btnLast = createPageBtn("nav_last", "最后一页");
    connect(btnLast, &QPushButton::clicked, [this](){ emit pageChanged(m_totalPages); });
    layout->addWidget(btnLast);
    layout->addSpacing(10);

    QPushButton* btnRefresh = createPageBtn("refresh", "刷新 (F5)");
    connect(btnRefresh, &QPushButton::clicked, this, &HeaderBar::refreshRequested);
    layout->addWidget(btnRefresh);
    layout->addSpacing(10);

    // 标准功能按钮样式 (32x32, 无边框)
    QString funcBtnStyle = 
        "QPushButton {"
        "    background-color: transparent;"
        "    border: none;"
        "    outline: none;"
        "    border-radius: 5px;"
        "    width: 32px;"
        "    height: 32px;"
        "    padding: 0px;"
        "}"
        "QPushButton:hover { background-color: rgba(255, 255, 255, 0.1); }"
        "QPushButton:pressed { background-color: rgba(255, 255, 255, 0.2); }";

    // 迁移：新建笔记 (+) 和 工具箱 按钮移至中间组，保留其原有 32x32 风格
    QPushButton* btnAddCenter = new QPushButton();
    btnAddCenter->setIcon(IconHelper::getIcon("add", "#ffffff", 20));
    btnAddCenter->setIconSize(QSize(20, 20));
    btnAddCenter->setToolTip("新建笔记 (Ctrl+N)");
    btnAddCenter->setStyleSheet(funcBtnStyle);
    connect(btnAddCenter, &QPushButton::clicked, this, &HeaderBar::newNoteRequested);
    layout->addWidget(btnAddCenter);
    layout->addSpacing(4);

    QPushButton* btnTool = new QPushButton();
    btnTool->setIcon(IconHelper::getIcon("toolbox", "#aaaaaa", 20));
    btnTool->setIconSize(QSize(20, 20));
    btnTool->setToolTip("工具箱 (右键快捷设置)");
    btnTool->setStyleSheet(funcBtnStyle);
    btnTool->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(btnTool, &QPushButton::clicked, this, &HeaderBar::toolboxRequested);
    connect(btnTool, &QPushButton::customContextMenuRequested, this, [this, btnTool](const QPoint& pos){
        emit toolboxContextMenuRequested(btnTool->mapToGlobal(pos));
    });
    layout->addWidget(btnTool);
    layout->addSpacing(4);

    QPushButton* btnLock = new QPushButton();
    btnLock->setIcon(IconHelper::getIcon("lock", "#aaaaaa", 20));
    btnLock->setIconSize(QSize(20, 20));
    btnLock->setToolTip("全局锁定");
    btnLock->setStyleSheet(funcBtnStyle);
    connect(btnLock, &QPushButton::clicked, this, &HeaderBar::globalLockRequested);
    layout->addWidget(btnLock);

    layout->addStretch();

    // 4. Functional Buttons
    m_btnStayOnTop = new QPushButton();
    m_btnStayOnTop->setObjectName("btnStayOnTop");
    m_btnStayOnTop->setIcon(IconHelper::getIcon("pin_tilted", "#aaaaaa", 20));
    m_btnStayOnTop->setIconSize(QSize(20, 20));
    m_btnStayOnTop->setToolTip("始终最前 (自动置顶)");
    m_btnStayOnTop->setCheckable(true);
    m_btnStayOnTop->setStyleSheet(funcBtnStyle + " QPushButton:checked { background-color: #FF551C; }");
    connect(m_btnStayOnTop, &QPushButton::toggled, this, [this](bool checked){
        m_btnStayOnTop->setIcon(IconHelper::getIcon(checked ? "pin_vertical" : "pin_tilted", checked ? "#ffffff" : "#aaaaaa", 20));
        emit stayOnTopRequested(checked);
    });
    layout->addWidget(m_btnStayOnTop);
    layout->addSpacing(4);

    m_btnMeta = new QPushButton();
    m_btnMeta->setIcon(IconHelper::getIcon("sidebar_right", "#aaaaaa", 20));
    m_btnMeta->setIconSize(QSize(20, 20));
    m_btnMeta->setToolTip("元数据面板 (Ctrl+I)");
    m_btnMeta->setCheckable(true);
    m_btnMeta->setStyleSheet(funcBtnStyle + " QPushButton:checked { background-color: #4a90e2; }");
    connect(m_btnMeta, &QPushButton::toggled, this, &HeaderBar::metadataToggled);
    layout->addWidget(m_btnMeta);
    layout->addSpacing(4);

    m_btnFilter = new QPushButton();
    m_btnFilter->setIcon(IconHelper::getIcon("filter", "#ffffff", 20));
    m_btnFilter->setIconSize(QSize(20, 20));
    m_btnFilter->setToolTip("高级筛选 (Ctrl+G)");
    m_btnFilter->setStyleSheet(funcBtnStyle + " QPushButton:checked { background-color: #4a90e2; }");
    m_btnFilter->setCheckable(true);
    connect(m_btnFilter, &QPushButton::clicked, this, &HeaderBar::filterRequested);
    layout->addWidget(m_btnFilter);

    // 5. Window Controls
    QWidget* winCtrlWidget = new QWidget();
    winCtrlWidget->setStyleSheet("background: transparent;");
    QHBoxLayout* winCtrlLayout = new QHBoxLayout(winCtrlWidget);
    winCtrlLayout->setContentsMargins(0, 0, 0, 0);
    winCtrlLayout->setSpacing(0);

    auto addWinBtn = [&](const QString& icon, const QString& hoverColor, auto signal) {
        QPushButton* btn = new QPushButton();
        btn->setIcon(IconHelper::getIcon(icon, "#aaaaaa", 20));
        btn->setIconSize(QSize(20, 20));
        btn->setFixedSize(32, 32);
        btn->setStyleSheet(QString("QPushButton { background: transparent; border: none; border-radius: 5px; } QPushButton:hover { background: %1; }").arg(hoverColor));
        connect(btn, &QPushButton::clicked, this, signal);
        winCtrlLayout->addWidget(btn);
    };

    addWinBtn("minimize", "rgba(255,255,255,0.1)", &HeaderBar::windowMinimize);
    addWinBtn("maximize", "rgba(255,255,255,0.1)", &HeaderBar::windowMaximize);
    addWinBtn("close", "#e81123", &HeaderBar::windowClose);
    layout->addWidget(winCtrlWidget);

    mainLayout->addWidget(topContent);

    // 【关键修复】实体的 1px 全宽分割线
    auto* bottomLine = new QFrame();
    bottomLine->setFrameShape(QFrame::HLine);
    bottomLine->setFixedHeight(1);
    bottomLine->setStyleSheet("background-color: #333333; border: none; margin: 0px;");
    mainLayout->addWidget(bottomLine);
}

void HeaderBar::updatePagination(int current, int total) {
    m_currentPage = current;
    m_totalPages = total;
    m_pageInput->setText(QString::number(current));
    m_totalPageLabel->setText(QString("/ %1").arg(total));
}

void HeaderBar::setFilterActive(bool active) {
    m_btnFilter->setChecked(active);
}

void HeaderBar::setMetadataActive(bool active) {
    m_btnMeta->setChecked(active);
}

void HeaderBar::focusSearch() {
    m_searchEdit->setFocus();
    m_searchEdit->selectAll();
}

void HeaderBar::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        if (auto* win = window()) {
            if (auto* handle = win->windowHandle()) {
                handle->startSystemMove();
            }
        }
        event->accept();
    }
}

void HeaderBar::mouseMoveEvent(QMouseEvent* event) {
    QWidget::mouseMoveEvent(event);
}

void HeaderBar::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        emit windowMaximize();
        event->accept();
    }
}
