#include "AddressBar.h"
#include "IconHelper.h"
#include <QHBoxLayout>
#include <QKeyEvent>

AddressBar::AddressBar(QWidget* parent) : QWidget(parent) {
    // 2026-03-24 按照授权修改：地址栏高度提升至 40px
    setFixedHeight(40);
    initUI();
}

void AddressBar::initUI() {
    auto* layout = new QHBoxLayout(this);
    // 2026-03-24 按照授权修改：微调边距以配合 40px 高度，确保视觉居中
    layout->setContentsMargins(15, 0, 15, 0);
    layout->setSpacing(8);

    // 1. 后退/前进按钮 (规格统一为 28x28 以匹配 40px 条高度)
    m_btnBack = new QPushButton();
    m_btnBack->setIcon(IconHelper::getIcon("nav_prev", "#aaaaaa", 18));
    m_btnBack->setFixedSize(28, 28);
    m_btnBack->setStyleSheet("QPushButton { border: none; background: transparent; } QPushButton:hover { background: rgba(255,255,255,0.1); border-radius: 4px; }");
    connect(m_btnBack, &QPushButton::clicked, this, &AddressBar::backRequested);
    
    m_btnForward = new QPushButton();
    m_btnForward->setIcon(IconHelper::getIcon("nav_next", "#aaaaaa", 18));
    m_btnForward->setFixedSize(28, 28);
    m_btnForward->setStyleSheet("QPushButton { border: none; background: transparent; } QPushButton:hover { background: rgba(255,255,255,0.1); border-radius: 4px; }");
    connect(m_btnForward, &QPushButton::clicked, this, &AddressBar::forwardRequested);

    layout->addWidget(m_btnBack);
    layout->addWidget(m_btnForward);

    // 2. 地址输入框 (4px 圆角)
    m_pathEdit = new QLineEdit();
    // 2026-03-24 按照授权修改：输入框高度提升至 30px
    m_pathEdit->setFixedHeight(30);
    // 2026-03-24 按照用户要求：地址栏背景深灰色，4像素圆角
    m_pathEdit->setStyleSheet(
        "QLineEdit {"
        "  background-color: #2D2D2D;"
        "  border: 1px solid #333;"
        "  border-radius: 4px;"
        "  color: #EEE;"
        "  padding: 0 12px;"
        "  font-size: 13px;"
        "}"
        "QLineEdit:focus { border: 1px solid #4a90e2; background-color: #252526; }"
    );
    connect(m_pathEdit, &QLineEdit::returnPressed, [this](){
        emit pathChanged(m_pathEdit->text().trimmed());
        emit returnPressed();
    });

    layout->addWidget(m_pathEdit, 1);
}

void AddressBar::setPath(const QString& path) {
    if (m_pathEdit->text() != path) {
        m_pathEdit->setText(path);
    }
}

QString AddressBar::path() const {
    return m_pathEdit->text();
}

void AddressBar::focusAddress() {
    m_pathEdit->setFocus();
    m_pathEdit->selectAll();
}

bool AddressBar::eventFilter(QObject* watched, QEvent* event) {
    return QWidget::eventFilter(watched, event);
}
