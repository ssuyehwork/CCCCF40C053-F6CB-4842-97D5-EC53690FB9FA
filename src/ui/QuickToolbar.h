#ifndef QUICKTOOLBAR_H
#define QUICKTOOLBAR_H

#include <QWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QVBoxLayout>

class QuickToolbar : public QWidget {
    Q_OBJECT
public:
    explicit QuickToolbar(QWidget* parent = nullptr);

    void setPageInfo(int current, int total);
    void setStayOnTop(bool onTop);
    bool isStayOnTop() const;

signals:
    void closeRequested();
    void minimizeRequested();
    void openFullRequested();
    void toggleStayOnTop(bool checked);
    void toggleSidebar();
    void prevPage();
    void nextPage();
    void jumpToPage(int page);
    void refreshRequested();
    void toolboxRequested();

private:
    QPushButton* m_btnStayTop;
    QLineEdit* m_pageEdit;
    QLabel* m_totalPageLabel;
    QPushButton* m_btnPrev;
    QPushButton* m_btnNext;
};

#endif // QUICKTOOLBAR_H
