#ifndef TODOCALENDARWINDOW_H
#define TODOCALENDARWINDOW_H

#include "FramelessDialog.h"
#include "../core/DatabaseManager.h"
#include <QCalendarWidget>
#include <QListWidget>

class TodoCalendarWindow : public FramelessDialog {
    Q_OBJECT
public:
    explicit TodoCalendarWindow(QWidget* parent = nullptr);
    ~TodoCalendarWindow() = default;

protected:
    void showEvent(QShowEvent* event) override;

private slots:
    void onDateSelected();
    void onAddTodo();
    void onEditTodo(QListWidgetItem* item);
    void refreshTodos();

private:
    void initUI();

    QCalendarWidget* m_calendar;
    QListWidget* m_todoList;
    QPushButton* m_btnAdd;
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
