#include "HeaderBar.h"
#include "StringUtils.h"
#include "MainWindow.h"
#include "IconHelper.h"
#include "ToolTipOverlay.h"
#include <QHBoxLayout>
#include <QSettings>
#include <QMouseEvent>
#include <QApplication>
#include <QWindow>
#include <QIntValidator>
#include <QMenu>
#include <QLabel>
#include <QPushButton>

HeaderBar::HeaderBar(QWidget* parent) : QWidget(parent) {
    setFixedHeight(36); 
    setStyleSheet("background-color: #252526; border: none;");

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // 顶部内容区
    auto* topContent = new QWidget();
    auto* layout = new QHBoxLayout(topContent);
    layout->setContentsMargins(10, 0, 10, 0);
    layout->setSpacing(0);
    layout->setAlignment(Qt::AlignVCenter);
    
    // 1. Logo & Title
    QLabel* appLogo = new QLabel();
    appLogo->setFixedSize(18, 18);
    appLogo->setPixmap(IconHelper::getIcon("zap", "#4a90e2", 18).pixmap(18, 18));
    layout->addWidget(appLogo);
    layout->addSpacing(6);

    QLabel* titleLabel = new QLabel("RapidNotes");
    titleLabel->setStyleSheet("font-size: 13px; font-weight: bold; color: #4a90e2; border: none; background: transparent;");
    layout->addWidget(titleLabel);
    layout->addSpacing(15);

    // 2. Search Box
    m_searchEdit = new SearchLineEdit();
    m_searchEdit->setPlaceholderText("搜索文件...");
    m_searchEdit->setFixedWidth(280);
    m_searchEdit->setFixedHeight(24);
    m_searchEdit->setStyleSheet(
        "SearchLineEdit { "
        "  background-color: #1e1e1e; "
        "  border: 1px solid #444; "
        "  border-radius: 6px; "
        "  padding: 2px 12px; "
        "  color: white; "
        "  font-size: 12px; "
        "} "
        "SearchLineEdit:focus { border: 1px solid #4a90e2; background-color: #181818; }"
    );
    connect(m_searchEdit, &QLineEdit::textChanged, this, &HeaderBar::searchChanged);
    layout->addWidget(m_searchEdit);
    layout->addSpacing(15);

    // 3. Pagination Controls
    QString pageBtnStyle = 
        "QPushButton {"
        "    background-color: transparent;"
        "    border: none;"
        "    border-radius: 4px;"
        "    width: 24px;"
        "    height: 24px;"
        "    padding: 0px;"
        "}"
        "QPushButton:hover { background-color: rgba(255, 255, 255, 0.1); }"
        "QPushButton:disabled { background-color: transparent; }";

    auto createPageBtn = [&](const QString& icon, const QString& tip) {
        QPushButton* btn = new QPushButton();
        btn->setIcon(IconHelper::getIcon(icon, "#aaaaaa", 16));
        btn->setProperty("tooltipText", tip); btn->installEventFilter(this);
        btn->setStyleSheet(pageBtnStyle);
        return btn;
    };

    QPushButton* btnPrev = createPageBtn("nav_prev", "上一页");
    layout->addWidget(btnPrev);
    layout->addSpacing(8);

    m_pageInput = new QLineEdit("1");
    m_pageInput->setFixedWidth(40);
    m_pageInput->setFixedHeight(24);
    m_pageInput->setAlignment(Qt::AlignCenter);
    m_pageInput->setStyleSheet("QLineEdit { background-color: #2D2D2D; border: 1px solid #555; border-radius: 4px; color: #eee; font-size: 11px; }");
    layout->addWidget(m_pageInput);
    layout->addSpacing(6);

    m_totalPageLabel = new QLabel("/ 1");
    m_totalPageLabel->setStyleSheet("color: #888; font-size: 12px; border: none; background: transparent;");
    layout->addWidget(m_totalPageLabel);
    layout->addSpacing(10);

    QPushButton* btnNext = createPageBtn("nav_next", "下一页");
    layout->addWidget(btnNext);
    layout->addSpacing(10);

    // 标准功能按钮样式
    QString funcBtnStyle = 
        "QPushButton {"
        "    background-color: transparent;"
        "    border: none;"
        "    outline: none;"
        "    border-radius: 4px;"
        "    width: 24px;"
        "    height: 24px;"
        "    padding: 0px;"
        "}"
        "QPushButton:hover { background-color: rgba(255, 255, 255, 0.1); }";

    m_btnToolbox = new QPushButton();
    m_btnToolbox->setIcon(IconHelper::getIcon("toolbox", "#aaaaaa", 18));
    m_btnToolbox->setProperty("tooltipText", "工具箱"); m_btnToolbox->installEventFilter(this);
    m_btnToolbox->setStyleSheet(funcBtnStyle);
    connect(m_btnToolbox, &QPushButton::clicked, this, &HeaderBar::toolboxRequested);
    layout->addWidget(m_btnToolbox);
    layout->addSpacing(4);

    layout->addStretch();

    // 4. 右侧窗口控制
    auto addWinBtn = [&](const QString& icon, const QString& hoverColor, auto signal) {
        QPushButton* btn = new QPushButton();
        btn->setIcon(IconHelper::getIcon(icon, "#aaaaaa", 18));
        btn->setFixedSize(24, 24); 
        btn->setStyleSheet(QString("QPushButton { background: transparent; border: none; border-radius: 4px; } QPushButton:hover { background: %1; }").arg(hoverColor));
        connect(btn, &QPushButton::clicked, this, signal);
        layout->addWidget(btn);
    };

    addWinBtn("minimize", "rgba(255,255,255,0.1)", &HeaderBar::windowMinimize);
    layout->addSpacing(4);
    addWinBtn("maximize", "rgba(255,255,255,0.1)", &HeaderBar::windowMaximize);
    layout->addSpacing(4);
    addWinBtn("close", "#e81123", &HeaderBar::windowClose);

    mainLayout->addWidget(topContent);

    auto* bottomLine = new QFrame();
    bottomLine->setFrameShape(QFrame::HLine);
    bottomLine->setFixedHeight(1);
    bottomLine->setStyleSheet("background-color: #333333; border: none; margin: 0px;");
    mainLayout->addWidget(bottomLine);
}

void HeaderBar::updatePagination(int current, int total) {
    m_pageInput->setText(QString::number(current));
    m_totalPageLabel->setText(QString("/ %1").arg(total));
}

void HeaderBar::setFilterActive(bool active) {}
void HeaderBar::setMetadataActive(bool active) {}

void HeaderBar::updateToolboxStatus(bool active) {
    if (m_btnToolbox) {
        m_btnToolbox->setIcon(IconHelper::getIcon("toolbox", active ? "#00A650" : "#aaaaaa", 18));
    }
}

void HeaderBar::focusSearch() {
    m_searchEdit->setFocus();
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

bool HeaderBar::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::ToolTip || event->type() == QEvent::HoverEnter) {
        QString text = watched->property("tooltipText").toString();
        if (!text.isEmpty()) {
            // 2026-03-22 🟢 [编译修复]：物理修正 ui::MainWindow 作用域并正确包含头文件
            ToolTipOverlay::instance()->showText(QCursor::pos(), text, 2000);
        }
        return true; 
    } else if (event->type() == QEvent::HoverLeave) {
        ToolTipOverlay::hideTip();
    }
    return QWidget::eventFilter(watched, event);
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
