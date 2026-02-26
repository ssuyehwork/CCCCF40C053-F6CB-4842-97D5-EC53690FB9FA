#include "ActivationDialog.h"
#include "ToolTipOverlay.h"
#include "../core/DatabaseManager.h"
#include <QDateTime>
#include <QMessageBox>
#include <QApplication>
#include <QCursor>
#include <QKeyEvent>

ActivationDialog::ActivationDialog(const QString& reason, QWidget* parent)
    : FramelessDialog("软件激活验证", parent)
{
    setFixedSize(420, 280);
    
    auto* layout = new QVBoxLayout(m_contentArea);
    layout->setContentsMargins(35, 25, 35, 20);
    layout->setSpacing(15);
    
    m_lblReason = new QLabel(reason);
    m_lblReason->setWordWrap(true);
    m_lblReason->setStyleSheet("color: #ecf0f1; font-size: 13px; font-weight: bold; line-height: 1.4;");
    layout->addWidget(m_lblReason);
    
    m_editKey = new QLineEdit();
    m_editKey->setEchoMode(QLineEdit::Password);
    m_editKey->setPlaceholderText("请输入激活码...");
    m_editKey->setStyleSheet("QLineEdit { height: 42px; border: 1px solid #3a90ff; border-radius: 6px; background: #1a1a1a; color: #fff; padding: 0 12px; font-family: 'Consolas'; }");
    layout->addWidget(m_editKey);
    
    m_lblAttempts = new QLabel();
    m_lblAttempts->setAlignment(Qt::AlignRight);
    updateRemainingAttempts();
    layout->addWidget(m_lblAttempts);
    
    auto* btnRow = new QHBoxLayout();
    auto* btnVerify = new QPushButton("确 认 激 活");
    btnVerify->setFixedHeight(42);
    btnVerify->setStyleSheet("QPushButton { background: #3a90ff; color: white; border-radius: 6px; font-weight: bold; font-size: 14px; }"
                             "QPushButton:hover { background: #2b7ae6; }");
    connect(btnVerify, &QPushButton::clicked, this, &ActivationDialog::onVerifyClicked);
    
    auto* btnExit = new QPushButton("退出程序");
    btnExit->setFixedHeight(42);
    btnExit->setStyleSheet("QPushButton { background: #2d2d2d; color: #999; border: 1px solid #444; border-radius: 6px; }"
                            "QPushButton:hover { background: #3d3d3d; color: #fff; }");
    connect(btnExit, &QPushButton::clicked, this, &ActivationDialog::reject);
    
    btnRow->addWidget(btnExit);
    btnRow->addWidget(btnVerify);
    layout->addLayout(btnRow);
    
    auto* lblTlg = new QLabel("联系获取助手：<b style='color: #3a90ff;'>Telegram：TLG_888</b>");
    lblTlg->setAlignment(Qt::AlignCenter);
    lblTlg->setStyleSheet("color: #777; font-size: 12px; margin-top: 8px;");
    layout->addWidget(lblTlg);
    
    layout->addStretch();
}

void ActivationDialog::updateRemainingAttempts() {
    int failed = DatabaseManager::instance().getTrialStatus()["failed_attempts"].toInt();
    int rem = 4 - failed;
    if (rem < 0) rem = 0;

    m_lblAttempts->setText(QString("今日剩余尝试机会: <b style='%1'>%2</b> / 4")
        .arg(rem > 1 ? "color: #f39c12;" : "color: #e74c3c;")
        .arg(rem));
    m_lblAttempts->setStyleSheet("color: #888; font-size: 11px;");
}

void ActivationDialog::onVerifyClicked() {
    QString key = m_editKey->text().trimmed();
    if (key.isEmpty()) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #f1c40f;'>请输入激活码</b>");
        return;
    }

    if (DatabaseManager::instance().verifyActivationCode(key)) {
        accept(); // 成功激活
    } else {
        updateRemainingAttempts();
        m_editKey->clear();
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #e74c3c;'>❌ 激活码错误</b>");
    }
}

void ActivationDialog::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_F1) {
        DatabaseManager::instance().resetFailedAttempts();
        updateRemainingAttempts();
        
        // 恢复 UI 状态（如果因为被锁定导致了禁用）
        m_editKey->setEnabled(true);
        m_editKey->setFocus();
        
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #2ecc71;'>✅ 尝试次数已重置，请重新输入</b>");
        event->accept();
        return;
    }
    
    // 对于其他键盘事件，交由基类处理
    FramelessDialog::keyPressEvent(event);
}
