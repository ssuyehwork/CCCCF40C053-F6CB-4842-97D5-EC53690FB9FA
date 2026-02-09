#ifndef FRAMELESSDIALOG_H
#define FRAMELESSDIALOG_H

#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QFrame>

/**
 * @brief 无边框对话框基类，自带标题栏、关闭按钮、阴影、置顶
 */
class FramelessDialog : public QDialog {
    Q_OBJECT
public:
    explicit FramelessDialog(const QString& title, QWidget* parent = nullptr);
    virtual ~FramelessDialog() = default;

    void setStayOnTop(bool stay);

private slots:
    void toggleStayOnTop(bool checked);

protected:
    void showEvent(QShowEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

    QWidget* m_contentArea;
    QVBoxLayout* m_mainLayout;
    QLabel* m_titleLabel;
    QPushButton* m_btnPin;
    QPushButton* m_minBtn;
    QPushButton* m_closeBtn;

    virtual void loadWindowSettings();
    virtual void saveWindowSettings();

private:
    QPoint m_dragPos;
    bool m_isStayOnTop = false; // 默认改为 false，支持记忆功能
    bool m_firstShow = true;
};

/**
 * @brief 无边框文本输入对话框
 */
class FramelessInputDialog : public FramelessDialog {
    Q_OBJECT
public:
    explicit FramelessInputDialog(const QString& title, const QString& label, 
                                  const QString& initial = "", QWidget* parent = nullptr);
    QString text() const { return m_edit->text().trimmed(); }
    void setEchoMode(QLineEdit::EchoMode mode) { m_edit->setEchoMode(mode); }

protected:
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QLineEdit* m_edit;
};

/**
 * @brief 无边框确认提示框
 */
class FramelessMessageBox : public FramelessDialog {
    Q_OBJECT
public:
    explicit FramelessMessageBox(const QString& title, const QString& text, QWidget* parent = nullptr);

signals:
    void confirmed();
    void cancelled();
};

#endif // FRAMELESSDIALOG_H
