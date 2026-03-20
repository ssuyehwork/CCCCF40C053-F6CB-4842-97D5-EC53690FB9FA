#include "PasswordVerifyDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QApplication>

PasswordVerifyDialog::PasswordVerifyDialog(const QString& title, const QString& message, QWidget* parent)
    : FramelessDialog(title, parent)
{
    setFixedSize(400, 220);

    auto* layout = new QVBoxLayout(m_contentArea);
    layout->setContentsMargins(30, 20, 30, 20);
    layout->setSpacing(15);

    // 1. 提示信息
    auto* lblMsg = new QLabel(message);
    lblMsg->setStyleSheet("color: #ccc; font-size: 13px;");
    lblMsg->setWordWrap(true);
    layout->addWidget(lblMsg);

    // 2. 密码输入框
    m_pwdEdit = new QLineEdit();
    m_pwdEdit->setEchoMode(QLineEdit::Password);
    m_pwdEdit->setPlaceholderText("请输入应用锁定密码");
    m_pwdEdit->setFixedHeight(36);
    m_pwdEdit->setStyleSheet(
        "QLineEdit {"
        "  background-color: #1a1a1a; border: 1px solid #333; border-radius: 4px;"
        "  padding: 0 10px; color: white; font-size: 14px;"
        "}"
        "QLineEdit:focus { border: 1px solid #3a90ff; }"
    );
    layout->addWidget(m_pwdEdit);

    layout->addStretch();

    // 3. 按钮栏
    auto* btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(12);
    
    auto* btnCancel = new QPushButton("取 消");
    btnCancel->setFixedSize(90, 32);
    btnCancel->setStyleSheet("QPushButton { background: #444; color: #ccc; border-radius: 4px; } QPushButton:hover { background: #555; }");
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    
    auto* btnOk = new QPushButton("确 定");
    btnOk->setFixedSize(90, 32);
    btnOk->setStyleSheet("QPushButton { background: #3a90ff; color: white; border-radius: 4px; font-weight: bold; } QPushButton:hover { background: #2b7ae6; }");
    connect(btnOk, &QPushButton::clicked, this, &QDialog::accept);
    // 默认点击确定
    btnOk->setDefault(true);
    
    btnLayout->addStretch();
    btnLayout->addWidget(btnCancel);
    btnLayout->addWidget(btnOk);
    layout->addLayout(btnLayout);

    // 绑定回车键
    connect(m_pwdEdit, &QLineEdit::returnPressed, this, &QDialog::accept);
}

void PasswordVerifyDialog::showEvent(QShowEvent* event) {
    FramelessDialog::showEvent(event);
    m_pwdEdit->setFocus();
}
