#include "SecurityLockDialog.h"
#include "ToolTipOverlay.h"
#include "../core/DatabaseManager.h"
#include <QKeyEvent>
#include <QApplication>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>

SecurityLockDialog::SecurityLockDialog(const QString& message, QWidget* parent)
    : FramelessDialog("安全锁定", parent)
{
    setFixedSize(480, 240);

    auto* layout = new QVBoxLayout(m_contentArea);
    layout->setContentsMargins(30, 20, 30, 20);
    layout->setSpacing(20);

    auto* row = new QHBoxLayout();
    auto* iconLabel = new QLabel();
    // 使用红色警告图标
    QIcon warnIcon = QApplication::style()->standardIcon(QStyle::SP_MessageBoxCritical);
    iconLabel->setPixmap(warnIcon.pixmap(48, 48));
    row->addWidget(iconLabel);

    m_lblMessage = new QLabel(message);
    m_lblMessage->setWordWrap(true);
    m_lblMessage->setStyleSheet("color: #ecf0f1; font-size: 14px; line-height: 1.4;");
    row->addWidget(m_lblMessage, 1);
    layout->addLayout(row);

    m_lblAttempts = new QLabel();
    m_lblAttempts->setAlignment(Qt::AlignRight);
    updateAttemptsDisplay();
    layout->addWidget(m_lblAttempts);

    layout->addStretch();

    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    auto* btnOk = new QPushButton("确 定");
    btnOk->setFixedSize(100, 36);
    btnOk->setStyleSheet("QPushButton { background-color: #3A90FF; color: white; border-radius: 4px; font-weight: bold; } QPushButton:hover { background-color: #2b7ae6; }");
    connect(btnOk, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(btnOk);
    layout->addLayout(btnLayout);

    // 强制置顶并聚焦
    setWindowFlag(Qt::WindowStaysOnTopHint, true);
}

void SecurityLockDialog::updateAttemptsDisplay() {
    QVariantMap status = DatabaseManager::instance().getTrialStatus(false);
    int rescueFailed = status["rescue_failed_attempts"].toInt();
    int rem = 4 - rescueFailed;
    if (rem < 0) rem = 0;

    m_lblAttempts->setText(QString("今日抢救尝试机会: <b style='%1'>%2</b> / 4")
        .arg(rem > 1 ? "color: #f39c12;" : "color: #e74c3c;")
        .arg(rem));
    m_lblAttempts->setStyleSheet("color: #777; font-size: 11px;");
}

void SecurityLockDialog::keyPressEvent(QKeyEvent* event) {
    // 监听超级快捷键: Ctrl + Shift + Alt + F10
    if (event->key() == Qt::Key_F10 &&
        (event->modifiers() & Qt::ControlModifier) &&
        (event->modifiers() & Qt::ShiftModifier) &&
        (event->modifiers() & Qt::AltModifier))
    {
        showRescueInput();
        event->accept();
        return;
    }

    FramelessDialog::keyPressEvent(event);
}

void SecurityLockDialog::showRescueInput() {
    QVariantMap status = DatabaseManager::instance().getTrialStatus(false);
    if (status["rescue_failed_attempts"].toInt() >= 4) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #e74c3c;'>✖ 今日抢救次数已耗尽</b>");
        return;
    }

    FramelessInputDialog dlg("超级抢救模式", "请输入高级密钥以强制同步数据状态：", "", this);
    dlg.setEchoMode(QLineEdit::Password);

    if (dlg.exec() == QDialog::Accepted) {
        QString key = dlg.text();
        // 此处逻辑交由 DatabaseManager 处理，它内部会更新 rescue_failed_attempts
        if (DatabaseManager::instance().verifyRescueKey(key)) {
            ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #2ecc71;'>✅ 抢救成功，数据已完成自愈同步</b>");
            accept(); // 关闭锁定窗口，允许程序继续
        } else {
            updateAttemptsDisplay();
            ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #e74c3c;'>❌ 密钥验证失败</b>");
        }
    }
}
