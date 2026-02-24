#include "TodoCalendarWindow.h"
#include "IconHelper.h"
#include "StringUtils.h"
#include "ToolTipOverlay.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDateTimeEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QPainter>

TodoCalendarWindow::TodoCalendarWindow(QWidget* parent) : FramelessDialog("待办日历", parent) {
    initUI();
    setMinimumSize(800, 600);

    connect(m_calendar, &QCalendarWidget::selectionChanged, this, &TodoCalendarWindow::onDateSelected);
    connect(m_btnAdd, &QPushButton::clicked, this, &TodoCalendarWindow::onAddTodo);
    connect(m_todoList, &QListWidget::itemDoubleClicked, this, &TodoCalendarWindow::onEditTodo);
    connect(&DatabaseManager::instance(), &DatabaseManager::todoChanged, this, &TodoCalendarWindow::refreshTodos);
}

void TodoCalendarWindow::initUI() {
    auto* mainLayout = new QHBoxLayout(m_contentArea);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(15);

    // 左侧：日历
    m_calendar = new QCalendarWidget(this);
    m_calendar->setGridVisible(true);
    m_calendar->setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader);
    m_calendar->setStyleSheet(
        "QCalendarWidget QAbstractItemView { background-color: #2d2d2d; color: #ccc; selection-background-color: #007acc; selection-color: white; }"
        "QCalendarWidget QWidget#qt_calendar_navigationbar { background-color: #333; }"
        "QCalendarWidget QToolButton { color: #ccc; font-weight: bold; background-color: transparent; border: none; }"
        "QCalendarWidget QToolButton:hover { background-color: #444; }"
        "QCalendarWidget QMenu { background-color: #333; color: #ccc; }"
        "QCalendarWidget QSpinBox { background-color: #333; color: #ccc; selection-background-color: #007acc; }"
    );
    mainLayout->addWidget(m_calendar, 1);

    // 右侧：列表面板
    auto* listPanel = new QWidget(this);
    auto* listLayout = new QVBoxLayout(listPanel);
    listLayout->setContentsMargins(0, 0, 0, 0);

    m_dateLabel = new QLabel(this);
    m_dateLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: #007acc; margin-bottom: 5px;");
    listLayout->addWidget(m_dateLabel);

    m_todoList = new QListWidget(this);
    m_todoList->setStyleSheet(
        "QListWidget { background-color: #252526; border: 1px solid #444; border-radius: 4px; padding: 5px; color: #ccc; }"
        "QListWidget::item { border-bottom: 1px solid #333; padding: 10px; }"
        "QListWidget::item:selected { background-color: #37373d; color: white; border-radius: 4px; }"
    );
    listLayout->addWidget(m_todoList);

    m_btnAdd = new QPushButton("新增待办", this);
    m_btnAdd->setIcon(IconHelper::getIcon("add", "#ffffff"));
    m_btnAdd->setStyleSheet(
        "QPushButton { background-color: #007acc; color: white; border: none; padding: 8px 15px; border-radius: 4px; font-weight: bold; }"
        "QPushButton:hover { background-color: #0098ff; }"
    );
    listLayout->addWidget(m_btnAdd);

    mainLayout->addWidget(listPanel, 0);

    onDateSelected();
}

void TodoCalendarWindow::showEvent(QShowEvent* event) {
    FramelessDialog::showEvent(event);
    refreshTodos();
}

void TodoCalendarWindow::onDateSelected() {
    QDate date = m_calendar->selectedDate();
    m_dateLabel->setText(date.toString("yyyy年M月d日"));
    refreshTodos();
}

void TodoCalendarWindow::refreshTodos() {
    m_todoList->clear();
    QDate date = m_calendar->selectedDate();
    QList<DatabaseManager::Todo> todos = DatabaseManager::instance().getTodosByDate(date);

    for (const auto& t : todos) {
        auto* item = new QListWidgetItem(m_todoList);
        QString timeStr = t.startTime.isValid() ? t.startTime.toString("HH:mm") : "--:--";
        if (t.endTime.isValid()) timeStr += " - " + t.endTime.toString("HH:mm");

        item->setText(QString("%1 %2").arg(timeStr).arg(t.title));
        item->setData(Qt::UserRole, t.id);

        if (t.status == 1) {
            item->setIcon(IconHelper::getIcon("select", "#666", 16));
            item->setForeground(QColor("#666"));
            auto font = item->font();
            font.setStrikeOut(true);
            item->setFont(font);
        } else if (t.status == 2) {
            item->setIcon(IconHelper::getIcon("close", "#e74c3c", 16));
            item->setForeground(QColor("#e74c3c"));
        } else {
            item->setIcon(IconHelper::getIcon("circle_filled", "#007acc", 8));
        }

        if (t.priority > 0) {
            item->setBackground(QColor(0, 122, 204, 30));
        }

        m_todoList->addItem(item);
    }
}

void TodoCalendarWindow::onAddTodo() {
    DatabaseManager::Todo t;
    t.startTime = QDateTime(m_calendar->selectedDate(), QTime::currentTime());
    t.endTime = t.startTime.addSecs(3600);

    TodoEditDialog dlg(t, this);
    if (dlg.exec() == QDialog::Accepted) {
        DatabaseManager::instance().addTodo(dlg.getTodo());
    }
}

void TodoCalendarWindow::onEditTodo(QListWidgetItem* item) {
    int id = item->data(Qt::UserRole).toInt();
    // 简单起见，从数据库重新获取（为了演示全流程，此处直接查）
    QList<DatabaseManager::Todo> todos = DatabaseManager::instance().getTodosByDate(m_calendar->selectedDate());
    for (const auto& t : todos) {
        if (t.id == id) {
            TodoEditDialog dlg(t, this);
            if (dlg.exec() == QDialog::Accepted) {
                DatabaseManager::instance().updateTodo(dlg.getTodo());
            }
            break;
        }
    }
}

// --- TodoEditDialog ---

TodoEditDialog::TodoEditDialog(const DatabaseManager::Todo& todo, QWidget* parent)
    : FramelessDialog(todo.id == -1 ? "新增待办" : "编辑待办", parent), m_todo(todo) {
    initUI();
    setFixedSize(450, 500);
}

void TodoEditDialog::initUI() {
    auto* layout = new QVBoxLayout(m_contentArea);
    layout->setSpacing(15);
    layout->setContentsMargins(20, 20, 20, 20);

    m_editTitle = new QLineEdit(this);
    m_editTitle->setPlaceholderText("待办标题...");
    m_editTitle->setText(m_todo.title);
    m_editTitle->setStyleSheet("font-size: 16px; padding: 8px; background: #333; border: 1px solid #444; color: white;");
    layout->addWidget(new QLabel("标题:"));
    layout->addWidget(m_editTitle);

    m_editContent = new QTextEdit(this);
    m_editContent->setPlaceholderText("详细内容(可选)...");
    m_editContent->setText(m_todo.content);
    m_editContent->setStyleSheet("background: #333; border: 1px solid #444; color: white;");
    layout->addWidget(new QLabel("备注:"));
    layout->addWidget(m_editContent);

    auto* timeLayout = new QHBoxLayout();
    m_editStart = new QDateTimeEdit(m_todo.startTime.isValid() ? m_todo.startTime : QDateTime::currentDateTime(), this);
    m_editEnd = new QDateTimeEdit(m_todo.endTime.isValid() ? m_todo.endTime : QDateTime::currentDateTime().addSecs(3600), this);
    m_editStart->setCalendarPopup(true);
    m_editEnd->setCalendarPopup(true);
    m_editStart->setStyleSheet("background: #333; color: white;");
    m_editEnd->setStyleSheet("background: #333; color: white;");

    timeLayout->addWidget(new QLabel("从:"));
    timeLayout->addWidget(m_editStart);
    timeLayout->addWidget(new QLabel("至:"));
    timeLayout->addWidget(m_editEnd);
    layout->addLayout(timeLayout);

    auto* reminderLayout = new QHBoxLayout();
    m_checkReminder = new QCheckBox("开启提醒", this);
    m_checkReminder->setChecked(m_todo.reminderTime.isValid());
    m_editReminder = new QDateTimeEdit(m_todo.reminderTime.isValid() ? m_todo.reminderTime : m_todo.startTime, this);
    m_editReminder->setCalendarPopup(true);
    m_editReminder->setEnabled(m_checkReminder->isChecked());
    m_editReminder->setStyleSheet("background: #333; color: white;");
    connect(m_checkReminder, &QCheckBox::toggled, m_editReminder, &QWidget::setEnabled);

    reminderLayout->addWidget(m_checkReminder);
    reminderLayout->addWidget(m_editReminder);
    layout->addLayout(reminderLayout);

    auto* botLayout = new QHBoxLayout();
    m_comboPriority = new QComboBox(this);
    m_comboPriority->addItems({"普通", "高优先级", "紧急"});
    m_comboPriority->setCurrentIndex(m_todo.priority);
    m_comboPriority->setStyleSheet("background: #333; color: white;");
    botLayout->addWidget(new QLabel("优先级:"));
    botLayout->addWidget(m_comboPriority);

    auto* btnSave = new QPushButton("保存", this);
    btnSave->setStyleSheet("background: #007acc; color: white; padding: 8px 20px; border-radius: 4px; font-weight: bold;");
    connect(btnSave, &QPushButton::clicked, this, &TodoEditDialog::onSave);
    botLayout->addWidget(btnSave);

    layout->addLayout(botLayout);
}

void TodoEditDialog::onSave() {
    if (m_editTitle->text().trimmed().isEmpty()) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), "请输入标题", 2000, QColor("#e74c3c"));
        return;
    }

    m_todo.title = m_editTitle->text().trimmed();
    m_todo.content = m_editContent->toPlainText();
    m_todo.startTime = m_editStart->dateTime();
    m_todo.endTime = m_editEnd->dateTime();
    m_todo.priority = m_comboPriority->currentIndex();
    if (m_checkReminder->isChecked()) {
        m_todo.reminderTime = m_editReminder->dateTime();
    } else {
        m_todo.reminderTime = QDateTime();
    }

    accept();
}

DatabaseManager::Todo TodoEditDialog::getTodo() const {
    return m_todo;
}
