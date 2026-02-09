#include "DatabaseLockDialog.h"
#include "StringUtils.h"

#include "IconHelper.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolTip>

DatabaseLockDialog::DatabaseLockDialog(Mode mode, QWidget* parent) 
    : FramelessDialog(mode == Login ? "数据库已锁定" : "设置数据库主密码", parent), m_mode(mode) 
{
    setFixedSize(350, mode == Login ? 220 : 280);

    auto* mainLayout = new QVBoxLayout(m_contentArea);
    mainLayout->setContentsMargins(30, 20, 30, 20);
    mainLayout->setSpacing(15);

    auto* iconLabel = new QLabel();
    iconLabel->setPixmap(IconHelper::getIcon("lock_secure", "#aaaaaa").pixmap(48, 48));
    iconLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(iconLabel);

    m_tipLabel = new QLabel(mode == Login ? "请输入主密码以解密并加载数据：" : "请设置一个主密码，该密码将用于加密整个数据库文件：");
    m_tipLabel->setWordWrap(true);
    m_tipLabel->setStyleSheet("color: #bbb; font-size: 12px;");
    m_tipLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(m_tipLabel);

    m_pwdEdit = new QLineEdit();
    m_pwdEdit->setEchoMode(QLineEdit::Password);
    m_pwdEdit->setPlaceholderText(mode == Login ? "主密码" : "新密码");
    m_pwdEdit->setFixedHeight(35);
    m_pwdEdit->setStyleSheet("QLineEdit { background: #252526; border: 1px solid #444; border-radius: 4px; padding: 0 10px; color: white; } QLineEdit:focus { border-color: #4a90e2; }");
    mainLayout->addWidget(m_pwdEdit);

    if (mode == SetPassword) {
        m_confirmEdit = new QLineEdit();
        m_confirmEdit->setEchoMode(QLineEdit::Password);
        m_confirmEdit->setPlaceholderText("确认密码");
        m_confirmEdit->setFixedHeight(35);
        m_confirmEdit->setStyleSheet(m_pwdEdit->styleSheet());
        mainLayout->addWidget(m_confirmEdit);
    } else {
        m_confirmEdit = nullptr;
    }

    auto* btnLayout = new QHBoxLayout();
    auto* btnConfirm = new QPushButton("确认");
    btnConfirm->setAutoDefault(false);
    btnConfirm->setFixedHeight(35);
    btnConfirm->setStyleSheet("QPushButton { background: #007acc; color: white; border: none; border-radius: 4px; font-weight: bold; } QPushButton:hover { background: #0062a3; }");
    connect(btnConfirm, &QPushButton::clicked, this, &DatabaseLockDialog::onConfirm);
    btnLayout->addWidget(btnConfirm);

    auto* btnCancel = new QPushButton("退出");
    btnCancel->setAutoDefault(false);
    btnCancel->setFixedHeight(35);
    btnCancel->setStyleSheet("QPushButton { background: #3e3e42; color: #ccc; border: none; border-radius: 4px; } QPushButton:hover { background: #4e4e52; }");
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(btnCancel);

    mainLayout->addLayout(btnLayout);

    m_pwdEdit->setFocus();
}

void DatabaseLockDialog::onConfirm() {
    QString pwd = m_pwdEdit->text();
    if (pwd.isEmpty()) {
        QToolTip::showText(QCursor::pos(), StringUtils::wrapToolTip("<span style='color: #e74c3c; font-weight: bold;'>✖ 密码不能为空</span>"), this);
        return;
    }

    if (m_mode == SetPassword) {
        if (pwd != m_confirmEdit->text()) {
            QToolTip::showText(QCursor::pos(), StringUtils::wrapToolTip("<span style='color: #e74c3c; font-weight: bold;'>✖ 两次输入的密码不一致</span>"), this);
            return;
        }
    }

    accept();
}
