#ifndef TODOCALENDARWINDOW_H
#define TODOCALENDARWINDOW_H

#include "FramelessDialog.h"
#include "../core/DatabaseManager.h"
#include <QCalendarWidget>
#include <QListWidget>
#include <QStackedWidget>
#include <QLineEdit>
#include <QTextEdit>
#include <QDateTimeEdit>
#include <QComboBox>
#include <QCheckBox>

class CustomCalendar : public QCalendarWidget {
    Q_OBJECT
public:
    explicit CustomCalendar(QWidget* parent = nullptr);
protected:
    void paintCell(QPainter* painter, const QRect& rect, QDate date) const override;
};

class TodoCalendarWindow : public FramelessDialog {
    Q_OBJECT
public:
    explicit TodoCalendarWindow(QWidget* parent = nullptr);
    ~TodoCalendarWindow() = default;

protected:
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onDateSelected();
    void onSwitchView();
    void onAddTodo();
    void onEditTodo(QListWidgetItem* item);
    void refreshTodos();

private:
    void initUI();
    void update24hList(const QDate& date);

    CustomCalendar* m_calendar;
    QStackedWidget* m_viewStack;
    QListWidget* m_detailed24hList;
    QListWidget* m_todoList;
    QListWidget* m_list24h;
    QPushButton* m_btnAdd;
    QPushButton* m_btnSwitch;
    QLabel* m_dateLabel;
};

class TodoEditDialog : public FramelessDialog {
    Q_OBJECT
public:
    explicit TodoEditDialog(const DatabaseManager::Todo& todo, QWidget* parent = nullptr);
    DatabaseManager::Todo getTodo() const;

private slots:
    void onSave();

private:
    void initUI();
    DatabaseManager::Todo m_todo;

    QLineEdit* m_editTitle;
    QTextEdit* m_editContent;
    QDateTimeEdit* m_editStart;
    QDateTimeEdit* m_editEnd;
    QDateTimeEdit* m_editReminder;
    QComboBox* m_comboPriority;
    QCheckBox* m_checkReminder;
};

#endif // TODOCALENDARWINDOW_H
