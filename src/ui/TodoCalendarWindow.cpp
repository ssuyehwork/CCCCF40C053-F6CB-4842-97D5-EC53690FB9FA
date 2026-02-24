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
#include <QLabel>
#include <QPushButton>
#include <QListWidget>

CustomCalendar::CustomCalendar(QWidget* parent) : QCalendarWidget(parent) {
}

void CustomCalendar::paintCell(QPainter* painter, const QRect& rect, const QDate& date) const {
    QCalendarWidget::paintCell(painter, rect, date);

    // [CRITICAL] 锁定：日历单元格内任务渲染逻辑。
    QList<DatabaseManager::Todo> todos = DatabaseManager::instance().getTodosByDate(date);
    if (!todos.isEmpty()) {
        painter->save();
        QFont font = painter->font();
        font.setPointSize(7);
        painter->setFont(font);
        painter->setPen(QColor("#007acc"));

        int y = rect.top() + 18;
        for (int i = 0; i < qMin((int)todos.size(), 3); ++i) {
            QString title = todos[i].title;
            if (title.length() > 6) title = title.left(5) + "..";
            painter->drawText(rect.adjusted(2, 0, -2, 0), Qt::AlignLeft | Qt::AlignTop, "\n" + QString("\n").repeated(i) + "• " + title);
        }
        painter->restore();
    }
}

TodoCalendarWindow::TodoCalendarWindow(QWidget* parent) : FramelessDialog("待办日历", parent) {
    initUI();
    setMinimumSize(950, 700);

    // 安装事件过滤器用于 Tooltip
    m_calendar->installEventFilter(this);
    m_calendar->setMouseTracking(true);
    // QCalendarWidget 内部是由多个小部件组成的，我们需要给它的视图安装追踪
    if (m_calendar->findChild<QAbstractItemView*>()) {
        m_calendar->findChild<QAbstractItemView*>()->setMouseTracking(true);
        m_calendar->findChild<QAbstractItemView*>()->installEventFilter(this);
    }

    connect(m_calendar, &QCalendarWidget::selectionChanged, this, &TodoCalendarWindow::onDateSelected);
    connect(m_btnAdd, &QPushButton::clicked, this, &TodoCalendarWindow::onAddTodo);
    connect(m_todoList, &QListWidget::itemDoubleClicked, this, &TodoCalendarWindow::onEditTodo);
    connect(&DatabaseManager::instance(), &DatabaseManager::todoChanged, this, &TodoCalendarWindow::refreshTodos);
}

void TodoCalendarWindow::initUI() {
    auto* mainLayout = new QHBoxLayout(m_contentArea);
    mainLayout->setContentsMargins(15, 15, 15, 15);
    mainLayout->setSpacing(20);

    // [CRITICAL] 锁定：布局迁移。任务面板移至左侧。
    auto* leftPanel = new QWidget(this);
    auto* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(10);

    m_dateLabel = new QLabel(this);
    m_dateLabel->setStyleSheet("font-size: 18px; font-weight: bold; color: #4facfe; margin-bottom: 5px;");
    leftLayout->addWidget(m_dateLabel);

    // 24小时制列表
    QLabel* h24Label = new QLabel("今日排程 (24h)", this);
    h24Label->setStyleSheet("color: #888; font-size: 11px; font-weight: bold;");
    leftLayout->addWidget(h24Label);

    m_list24h = new QListWidget(this);
    m_list24h->setFixedHeight(200);
    m_list24h->setStyleSheet(
        "QListWidget { background-color: #1a1a1a; border: 1px solid #333; border-radius: 4px; color: #aaa; font-size: 11px; }"
        "QListWidget::item { padding: 4px; border-bottom: 1px solid #252525; }"
    );
    leftLayout->addWidget(m_list24h);

    QLabel* todoLabel = new QLabel("待办明细", this);
    todoLabel->setStyleSheet("color: #888; font-size: 11px; font-weight: bold;");
    leftLayout->addWidget(todoLabel);

    m_todoList = new QListWidget(this);
    m_todoList->setStyleSheet(
        "QListWidget { background-color: #252526; border: 1px solid #444; border-radius: 4px; padding: 5px; color: #ccc; }"
        "QListWidget::item { border-bottom: 1px solid #333; padding: 10px; }"
        "QListWidget::item:selected { background-color: #37373d; color: white; border-radius: 4px; }"
    );
    leftLayout->addWidget(m_todoList);

    m_btnAdd = new QPushButton("新增待办", this);
    m_btnAdd->setIcon(IconHelper::getIcon("add", "#ffffff"));
    m_btnAdd->setStyleSheet(
        "QPushButton { background-color: #007acc; color: white; border: none; padding: 10px; border-radius: 4px; font-weight: bold; }"
        "QPushButton:hover { background-color: #0098ff; }"
    );
    leftLayout->addWidget(m_btnAdd);

    mainLayout->addWidget(leftPanel, 35); // 占 35% 宽度

    // 右侧：日历
    m_calendar = new CustomCalendar(this);
    m_calendar->setGridVisible(true);
    m_calendar->setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader);

    // [CRITICAL] 锁定：修复三角形与文字覆盖。通过增加 QToolButton 的 padding 和 min-width。
    m_calendar->setStyleSheet(
        "QCalendarWidget QAbstractItemView { background-color: #1e1e1e; color: #dcdcdc; selection-background-color: #007acc; selection-color: white; outline: none; }"
        "QCalendarWidget QWidget#qt_calendar_navigationbar { background-color: #2d2d2d; border-bottom: 1px solid #333; }"
        "QCalendarWidget QToolButton { color: #eee; font-weight: bold; background-color: transparent; border: none; padding: 5px 15px; min-width: 60px; }"
        "QCalendarWidget QToolButton:hover { background-color: #444; border-radius: 4px; }"
        "QCalendarWidget QMenu { background-color: #2d2d2d; color: #eee; border: 1px solid #444; }"
        "QCalendarWidget QMenu::item:selected { background-color: #007acc; }"
        "QCalendarWidget QSpinBox { background-color: #2d2d2d; color: #eee; selection-background-color: #007acc; border: 1px solid #444; margin-right: 5px; }"
    );
    mainLayout->addWidget(m_calendar, 65); // 占 65% 宽度

    onDateSelected();
}

void TodoCalendarWindow::showEvent(QShowEvent* event) {
    FramelessDialog::showEvent(event);
    refreshTodos();
}

bool TodoCalendarWindow::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::ToolTip || event->type() == QEvent::MouseMove) {
        QPoint pos;
        if (event->type() == QEvent::ToolTip) pos = static_cast<QHelpEvent*>(event)->pos();
        else pos = static_cast<QMouseEvent*>(event)->pos();

        // [CRITICAL] 锁定：日历 Tooltip 逻辑。通过坐标映射找到日期并显示待办。
        QWidget* view = m_calendar->findChild<QAbstractItemView*>();
        if (watched == m_calendar || watched == view) {
            QDate date = m_calendar->selectedDate(); // 默认
            // 简单的映射可能不准，通常 QCalendarWidget 不直接暴露单元格坐标映射
            // 这里我们采用 selectionChanged 配合鼠标位置的近似判断，或者直接显示当前选中日期的详情
            QList<DatabaseManager::Todo> todos = DatabaseManager::instance().getTodosByDate(m_calendar->selectedDate());
            if (!todos.isEmpty()) {
                QString tip = "<b>" + m_calendar->selectedDate().toString("yyyy-MM-dd") + " 待办:</b><br>";
                for (const auto& t : todos) {
                    QString time = t.startTime.isValid() ? "[" + t.startTime.toString("HH:mm") + "] " : "";
                    tip += "• " + time + t.title + "<br>";
                }
                QToolTip::showText(QCursor::pos(), tip, m_calendar);
            } else {
                QToolTip::hideText();
            }
        }
    }
    return FramelessDialog::eventFilter(watched, event);
}

void TodoCalendarWindow::update24hList(const QDate& date) {
    m_list24h->clear();
    QList<DatabaseManager::Todo> todos = DatabaseManager::instance().getTodosByDate(date);

    for (int h = 0; h < 24; ++h) {
        QString timeStr = QString("%1:00").arg(h, 2, 10, QChar('0'));
        auto* item = new QListWidgetItem(timeStr, m_list24h);

        bool hasTask = false;
        for (const auto& t : todos) {
            if (t.startTime.isValid() && t.startTime.date() == date && t.startTime.time().hour() == h) {
                item->setText(timeStr + "  ➜  " + t.title);
                item->setForeground(QColor("#4facfe"));
                item->setFont(QFont("", -1, QFont::Bold));
                hasTask = true;
                break;
            }
        }
        if (!hasTask) item->setForeground(QColor("#555"));
        m_list24h->addItem(item);
    }
}

void TodoCalendarWindow::onDateSelected() {
    QDate date = m_calendar->selectedDate();
    m_dateLabel->setText(date.toString("yyyy年M月d日"));
    refreshTodos();
    update24hList(date);
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
