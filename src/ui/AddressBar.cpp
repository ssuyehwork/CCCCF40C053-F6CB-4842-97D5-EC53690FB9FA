#include "AddressBar.h"
#include "IconHelper.h"
#include <QHBoxLayout>
#include <QKeyEvent>

AddressBar::AddressBar(QWidget* parent) : QWidget(parent) {
    setFixedHeight(32);
    initUI();
}

void AddressBar::initUI() {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(10, 2, 10, 2);
    layout->setSpacing(6);

    // 1. 后退/前进按钮
    m_btnBack = new QPushButton();
    m_btnBack->setIcon(IconHelper::getIcon("nav_prev", "#aaaaaa"));
    m_btnBack->setFixedSize(24, 24);
    m_btnBack->setStyleSheet("QPushButton { border: none; background: transparent; } QPushButton:hover { background: rgba(255,255,255,0.1); border-radius: 4px; }");
    connect(m_btnBack, &QPushButton::clicked, this, &AddressBar::backRequested);

    m_btnForward = new QPushButton();
    m_btnForward->setIcon(IconHelper::getIcon("nav_next", "#aaaaaa"));
    m_btnForward->setFixedSize(24, 24);
    m_btnForward->setStyleSheet("QPushButton { border: none; background: transparent; } QPushButton:hover { background: rgba(255,255,255,0.1); border-radius: 4px; }");
    connect(m_btnForward, &QPushButton::clicked, this, &AddressBar::forwardRequested);

    layout->addWidget(m_btnBack);
    layout->addWidget(m_btnForward);

    // 2. 地址输入框 (4px 圆角)
    m_pathEdit = new QLineEdit();
    m_pathEdit->setFixedHeight(24);
    // 2026-03-24 按照用户要求：地址栏背景深灰色，4像素圆角
    m_pathEdit->setStyleSheet(
        "QLineEdit {"
        "  background-color: #2D2D2D;"
        "  border: 1px solid #444;"
        "  border-radius: 4px;"
        "  color: #EEE;"
        "  padding: 0 10px;"
        "  font-size: 12px;"
        "}"
        "QLineEdit:focus { border: 1px solid #4a90e2; }"
    );
    connect(m_pathEdit, &QLineEdit::returnPressed, [this](){
        emit pathChanged(m_pathEdit->text().trimmed());
    });

    layout->addWidget(m_pathEdit, 1);
}

void AddressBar::setPath(const QString& path) {
    m_pathEdit->setText(path);
}

QString AddressBar::path() const {
    return m_pathEdit->text();
}

bool AddressBar::eventFilter(QObject* watched, QEvent* event) {
    return QWidget::eventFilter(watched, event);
}
