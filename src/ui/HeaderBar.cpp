#include "HeaderBar.h"
#include "StringUtils.h"
#include "MainWindow.h"
#include "IconHelper.h"
#include <QHBoxLayout>
#include <QSettings>
#include <QMouseEvent>
#include <QApplication>
#include <QWindow>
#include <QIntValidator>

// 2026-03-xx 按照用户要求：HeaderBar.cpp 深度重构。
// 彻底移除对已物理删除窗口（Toolbox 等）的逻辑引用。

HeaderBar::HeaderBar(QWidget* parent) : QWidget(parent) {
    setFixedHeight(36); 
    setStyleSheet("background-color: #252526; border: none;");

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0); mainLayout->setSpacing(0);

    auto* topContent = new QWidget();
    auto* layout = new QHBoxLayout(topContent);
    layout->setContentsMargins(10, 0, 10, 0); layout->setSpacing(0); layout->setAlignment(Qt::AlignVCenter);
    
    QLabel* appLogo = new QLabel();
    appLogo->setFixedSize(18, 18);
    appLogo->setPixmap(IconHelper::getIcon("zap", "#4a90e2", 18).pixmap(18, 18));
    layout->addWidget(appLogo); layout->addSpacing(6);

    QLabel* titleLabel = new QLabel("数据管理终端");
    titleLabel->setStyleSheet("font-size: 13px; font-weight: bold; color: #4a90e2; border: none; background: transparent;");
    layout->addWidget(titleLabel); layout->addSpacing(15);

    m_searchEdit = new SearchLineEdit();
    m_searchEdit->setPlaceholderText("搜索数据...");
    m_searchEdit->setFixedWidth(280); m_searchEdit->setFixedHeight(24);
    m_searchEdit->setStyleSheet("SearchLineEdit { background-color: #1e1e1e; border: 1px solid #444; border-radius: 6px; padding: 2px 12px; color: white; } SearchLineEdit:focus { border: 1px solid #4a90e2; }");
    connect(m_searchEdit, &QLineEdit::textChanged, this, &HeaderBar::searchChanged);
    layout->addWidget(m_searchEdit); layout->addSpacing(15);

    QString pageBtnStyle = "QPushButton { background-color: transparent; border: none; border-radius: 4px; width: 24px; height: 24px; } QPushButton:hover { background-color: rgba(255, 255, 255, 0.1); }";

    auto createPageBtn = [&](const QString& icon, const QString& tip) {
        QPushButton* btn = new QPushButton();
        btn->setIcon(IconHelper::getIcon(icon, "#aaaaaa", 16));
        btn->setProperty("tooltipText", tip); btn->installEventFilter(this);
        btn->setStyleSheet(pageBtnStyle); return btn;
    };

    layout->addWidget(createPageBtn("nav_first", "第一页")); layout->addSpacing(6);
    layout->addWidget(createPageBtn("nav_prev", "上一页")); layout->addSpacing(8);
    m_pageInput = new QLineEdit("1");
    m_pageInput->setFixedWidth(40); m_pageInput->setFixedHeight(24); m_pageInput->setAlignment(Qt::AlignCenter);
    m_pageInput->setStyleSheet("QLineEdit { background-color: #2D2D2D; border: 1px solid #555; border-radius: 4px; color: #eee; }");
    layout->addWidget(m_pageInput); layout->addSpacing(6);
    m_totalPageLabel = new QLabel("/ 1");
    m_totalPageLabel->setStyleSheet("color: #888; font-size: 12px;");
    layout->addWidget(m_totalPageLabel); layout->addSpacing(10);
    layout->addWidget(createPageBtn("nav_next", "下一页")); layout->addSpacing(6);
    layout->addWidget(createPageBtn("nav_last", "最后一页")); layout->addSpacing(10);

    QPushButton* btnRefresh = createPageBtn("refresh", "刷新 (F5)");
    connect(btnRefresh, &QPushButton::clicked, this, &HeaderBar::refreshRequested);
    layout->addWidget(btnRefresh); layout->addStretch();

    QString funcBtnStyle = "QPushButton { background-color: transparent; border: none; border-radius: 4px; width: 24px; height: 24px; } QPushButton:hover { background-color: rgba(255, 255, 255, 0.1); }";

    QPushButton* btnAddCenter = new QPushButton();
    btnAddCenter->setIcon(IconHelper::getIcon("add", "#aaaaaa", 18));
    btnAddCenter->setProperty("tooltipText", "新建记录"); btnAddCenter->installEventFilter(this);
    btnAddCenter->setStyleSheet(funcBtnStyle);
    connect(btnAddCenter, &QPushButton::clicked, this, &HeaderBar::newNoteRequested);

    m_btnFilter = new QPushButton();
    m_btnFilter->setIcon(IconHelper::getIcon("filter", "#aaaaaa", 18));
    m_btnFilter->setProperty("tooltipText", "高级筛选"); m_btnFilter->installEventFilter(this);
    m_btnFilter->setStyleSheet(funcBtnStyle + " QPushButton:checked { background-color: rgba(255, 255, 255, 0.1); }");
    m_btnFilter->setCheckable(true);
    connect(m_btnFilter, &QPushButton::clicked, this, &HeaderBar::filterRequested);

    m_btnMeta = new QPushButton();
    m_btnMeta->setIcon(IconHelper::getIcon("sidebar_right", "#aaaaaa", 18));
    m_btnMeta->setProperty("tooltipText", "元数据面板 (Ctrl+I)"); m_btnMeta->installEventFilter(this);
    m_btnMeta->setCheckable(true);
    m_btnMeta->setStyleSheet(funcBtnStyle + " QPushButton:checked { background-color: rgba(255, 255, 255, 0.1); }");
    connect(m_btnMeta, &QPushButton::toggled, this, &HeaderBar::metadataToggled);

    m_btnStayOnTop = new QPushButton();
    m_btnStayOnTop->setIcon(IconHelper::getIcon("pin_tilted", "#aaaaaa", 18));
    m_btnStayOnTop->setProperty("tooltipText", "始终最前"); m_btnStayOnTop->installEventFilter(this);
    m_btnStayOnTop->setCheckable(true); m_btnStayOnTop->setStyleSheet(funcBtnStyle);
    connect(m_btnStayOnTop, &QPushButton::toggled, this, [this](bool checked){
        m_btnStayOnTop->setIcon(IconHelper::getIcon(checked ? "pin_vertical" : "pin_tilted", checked ? "#FF551C" : "#aaaaaa", 18));
        emit stayOnTopRequested(checked);
    });

    layout->addWidget(m_btnFilter); layout->addSpacing(4);
    layout->addWidget(m_btnMeta); layout->addSpacing(4);
    layout->addWidget(btnAddCenter); layout->addSpacing(4);
    layout->addWidget(m_btnStayOnTop); layout->addSpacing(4);

    auto addWinBtn = [&](const QString& icon, const QString& hoverColor, auto signal) {
        QPushButton* btn = new QPushButton();
        btn->setIcon(IconHelper::getIcon(icon, "#aaaaaa", 18)); btn->setFixedSize(24, 24);
        btn->setStyleSheet(QString("QPushButton { background: transparent; border: none; border-radius: 4px; } QPushButton:hover { background: %1; }").arg(hoverColor));
        connect(btn, &QPushButton::clicked, this, signal); layout->addWidget(btn);
    };

    addWinBtn("minimize", "rgba(255,255,255,0.1)", &HeaderBar::windowMinimize); layout->addSpacing(4);
    addWinBtn("maximize", "rgba(255,255,255,0.1)", &HeaderBar::windowMaximize); layout->addSpacing(4);
    addWinBtn("close", "#e81123", &HeaderBar::windowClose);

    mainLayout->addWidget(topContent);
    auto* bottomLine = new QFrame(); bottomLine->setFrameShape(QFrame::HLine); bottomLine->setFixedHeight(1);
    bottomLine->setStyleSheet("background-color: #333333; border: none;");
    mainLayout->addWidget(bottomLine);
}

void HeaderBar::updatePagination(int current, int total) {
    m_currentPage = current; m_totalPages = total; m_pageInput->setText(QString::number(current)); m_totalPageLabel->setText(QString("/ %1").arg(total));
}
void HeaderBar::setFilterActive(bool active) { m_btnFilter->setChecked(active); }
void HeaderBar::setMetadataActive(bool active) { m_btnMeta->setChecked(active); }
void HeaderBar::updateToolboxStatus(bool active) {}
void HeaderBar::focusSearch() { m_searchEdit->setFocus(); m_searchEdit->selectAll(); }
void HeaderBar::mousePressEvent(QMouseEvent* event) { if (event->button() == Qt::LeftButton) { if (auto* win = window()) { if (auto* handle = win->windowHandle()) handle->startSystemMove(); } } }
bool HeaderBar::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::ToolTip) {
        QString text = watched->property("tooltipText").toString();
        if (!text.isEmpty()) ToolTipOverlay::instance()->showText(QCursor::pos(), text, 2000);
        return true; 
    }
    return QWidget::eventFilter(watched, event);
}
void HeaderBar::mouseMoveEvent(QMouseEvent* event) {}
void HeaderBar::mouseDoubleClickEvent(QMouseEvent* event) { if (event->button() == Qt::LeftButton) emit windowMaximize(); }
