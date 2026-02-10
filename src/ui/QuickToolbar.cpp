#include "QuickToolbar.h"
#include "IconHelper.h"
#include <QIntValidator>
#include <type_traits>
#include "StringUtils.h"

QuickToolbar::QuickToolbar(QWidget* parent) : QWidget(parent) {
    setFixedWidth(40);
    setStyleSheet("background-color: #252526; border-left: 1px solid #333; border-top-right-radius: 10px; border-bottom-right-radius: 10px;");

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 10, 0, 10);
    layout->setSpacing(5);
    layout->setAlignment(Qt::AlignHCenter);

    auto addBtn = [&](const QString& icon, const QString& tip, auto signal, bool checkable = false) {
        QPushButton* btn = new QPushButton();
        btn->setFixedSize(30, 30);
        btn->setIcon(IconHelper::getIcon(icon, "#aaaaaa"));
        btn->setToolTip(StringUtils::wrapToolTip(tip));
        btn->setCheckable(checkable);
        btn->setStyleSheet("QPushButton { background: transparent; border: none; border-radius: 4px; } "
                           "QPushButton:hover { background: #3e3e42; } "
                           "QPushButton:checked { background: #0E639C; }");
        
        if constexpr (!std::is_same_v<decltype(signal), std::nullptr_t>) {
            connect(btn, &QPushButton::clicked, this, signal);
        }

        layout->addWidget(btn);
        return btn;
    };

    addBtn("close", "关闭", &QuickToolbar::closeRequested);
    addBtn("maximize", "切换主界面", &QuickToolbar::openFullRequested);
    addBtn("minimize", "最小化", &QuickToolbar::minimizeRequested);
    
    auto addSeparator = [&]() {
        QFrame* line = new QFrame();
        line->setFixedSize(20, 1);
        line->setStyleSheet("background-color: #333; margin: 5px 0;");
        layout->addWidget(line, 0, Qt::AlignHCenter);
    };

    addSeparator();
    
    m_btnStayTop = addBtn("pin_tilted", "保持置顶", nullptr, true);
    connect(m_btnStayTop, &QPushButton::toggled, this, [this](bool checked){
        m_btnStayTop->setIcon(IconHelper::getIcon(checked ? "pin_vertical" : "pin_tilted", checked ? "#ffffff" : "#aaaaaa"));
        emit toggleStayOnTop(checked);
    });
    
    addBtn("eye", "显示/隐藏侧边栏", &QuickToolbar::toggleSidebar);
    addBtn("refresh", "刷新", &QuickToolbar::refreshRequested);
    addBtn("toolbox", "工具箱", &QuickToolbar::toolboxRequested);

    addSeparator();

    // 分页模块：对齐 Python 版样式
    QWidget* pageBox = new QWidget();
    pageBox->setFixedWidth(32);
    QVBoxLayout* pageLayout = new QVBoxLayout(pageBox);
    pageLayout->setContentsMargins(0, 5, 0, 5);
    pageLayout->setSpacing(2);
    pageLayout->setAlignment(Qt::AlignHCenter);
    pageBox->setStyleSheet("QWidget { background: #1e1e1e; border: 1px solid #333; border-radius: 4px; } QPushButton { border: none; background: transparent; }");

    m_btnPrev = new QPushButton();
    m_btnPrev->setFixedSize(24, 20);
    m_btnPrev->setIcon(IconHelper::getIcon("nav_prev", "#888", 12));
    connect(m_btnPrev, &QPushButton::clicked, this, &QuickToolbar::prevPage);
    pageLayout->addWidget(m_btnPrev, 0, Qt::AlignHCenter);

    m_pageEdit = new QLineEdit("1");
    m_pageEdit->setFixedWidth(24);
    m_pageEdit->setFixedHeight(18);
    m_pageEdit->setAlignment(Qt::AlignCenter);
    m_pageEdit->setValidator(new QIntValidator(1, 999, this));
    m_pageEdit->setStyleSheet("background: #252526; color: white; border: 1px solid #444; border-radius: 2px; font-size: 10px; padding: 0;");
    connect(m_pageEdit, &QLineEdit::returnPressed, [this](){
        emit jumpToPage(m_pageEdit->text().toInt());
    });
    pageLayout->addWidget(m_pageEdit, 0, Qt::AlignHCenter);

    m_totalPageLabel = new QLabel("1");
    m_totalPageLabel->setAlignment(Qt::AlignCenter);
    m_totalPageLabel->setStyleSheet("color: #666; font-size: 9px; border: none; background: transparent;");
    pageLayout->addWidget(m_totalPageLabel, 0, Qt::AlignHCenter);

    m_btnNext = new QPushButton();
    m_btnNext->setFixedSize(24, 20);
    m_btnNext->setIcon(IconHelper::getIcon("nav_next", "#888", 12));
    connect(m_btnNext, &QPushButton::clicked, this, &QuickToolbar::nextPage);
    pageLayout->addWidget(m_btnNext, 0, Qt::AlignHCenter);

    layout->addWidget(pageBox, 0, Qt::AlignHCenter);

    layout->addStretch();

    // 垂直文字
    QLabel* lblTitle = new QLabel("快\n速\n笔\n记");
    lblTitle->setStyleSheet("color: #444; font-weight: bold; font-size: 11px; line-height: 1.2;");
    lblTitle->setAlignment(Qt::AlignCenter);
    layout->addWidget(lblTitle);

    layout->addSpacing(10);
    
    QLabel* logo = new QLabel();
    logo->setPixmap(IconHelper::getIcon("zap", "#0E639C", 20).pixmap(20, 20));
    layout->addWidget(logo, 0, Qt::AlignHCenter);
}

void QuickToolbar::setPageInfo(int current, int total) {
    m_pageEdit->setText(QString::number(current));
    m_totalPageLabel->setText(QString::number(total));
    m_btnPrev->setEnabled(current > 1);
    m_btnNext->setEnabled(current < total);
}

void QuickToolbar::setStayOnTop(bool onTop) {
    m_btnStayTop->setChecked(onTop);
}

bool QuickToolbar::isStayOnTop() const {
    return m_btnStayTop->isChecked();
}
