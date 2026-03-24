#ifndef ADDRESSBAR_H
#define ADDRESSBAR_H

#include <QWidget>
#include <QLineEdit>
#include <QPushButton>

class AddressBar : public QWidget {
    Q_OBJECT
public:
    explicit AddressBar(QWidget* parent = nullptr);

    // 2026-03-24 [NEW] 设置当前显示的路径
    void setPath(const QString& path);
    QString path() const;

signals:
    void pathChanged(const QString& newPath);
    void backRequested();
    void forwardRequested();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void initUI();
    QLineEdit* m_pathEdit;
    QPushButton* m_btnBack;
    QPushButton* m_btnForward;
};

#endif // ADDRESSBAR_H
