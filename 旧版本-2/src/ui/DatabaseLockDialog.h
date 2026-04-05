#ifndef DATABASELOCKDIALOG_H
#define DATABASELOCKDIALOG_H

#include "FramelessDialog.h"
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>

class DatabaseLockDialog : public FramelessDialog {
    Q_OBJECT
public:
    enum Mode { Login, SetPassword };

    explicit DatabaseLockDialog(Mode mode, QWidget* parent = nullptr);

    QString password() const { return m_pwdEdit->text(); }

private slots:
    void onConfirm();

private:
    Mode m_mode;
    QLineEdit* m_pwdEdit;
    QLineEdit* m_confirmEdit;
    QLabel* m_tipLabel;
};

#endif // DATABASELOCKDIALOG_H
