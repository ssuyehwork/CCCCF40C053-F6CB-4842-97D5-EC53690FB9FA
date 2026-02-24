#include "TodoCalendarWindow.h"
#include "IconHelper.h"
#include "StringUtils.h"
#include "ToolTipOverlay.h"
#include "ResizeHandle.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDateTimeEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QTextEdit>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QToolTip>
#include <QCursor>
#include <QPainter>
#include <QLabel>
#include <QPushButton>
#include <QListWidget>
#include <QMenu>
#include <algorithm>

CustomCalendar::CustomCalendar(QWidget* parent) : QCalendarWidget(parent) {
}

void CustomCalendar::paintCell(QPainter* painter, const QRect& rect, QDate date) const {
    // [CRITICAL] 锁定：日历热力图逻辑。背景深浅表示任务密度。
    QList<DatabaseManager::Todo> todos = DatabaseManager::instance().getTodosByDate(date);
    if (!todos.isEmpty() && date != selectedDate()) {
        // [PROFESSIONAL] 优化：未选中时使用极淡的灰色背景作为热力图，避免与蓝色主题混淆
        int alpha = qMin(10 + (int)todos.size() * 10, 40);
        painter->fillRect(rect, QColor(255, 255, 255, alpha));
    }

    QCalendarWidget::paintCell(painter, rect, date);

    // [CRITICAL] 锁定：日历单元格内任务标题渲染。
    if (!todos.isEmpty()) {
        painter->save();
        QFont font = painter->font();
        font.setPointSize(7);
        painter->setFont(font);
        
        // [PROFESSIONAL] 修复：选中状态下强制白色，非选中状态下使用浅灰色避免与深蓝背景冲突
        if (date == selectedDate()) {
            painter->setPen(Qt::white);
        } else {
            painter->setPen(QColor("#999999"));
        }

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
    
    // [PROFESSIONAL] 集成窗口缩放手柄
    new ResizeHandle(this, this);

    // 安装事件过滤器用于 Tooltip
    m_calendar->installEventFilter(this);
    m_calendar->setMouseTracking(true);
    // QCalendarWidget 内部是由多个小部件组成的，我们需要给它的视图安装追踪
    if (m_calendar->findChild<QAbstractItemView*>()) {
        m_calendar->findChild<QAbstractItemView*>()->setMouseTracking(true);
        m_calendar->findChild<QAbstractItemView*>()->installEventFilter(this);
    }

    connect(m_calendar, &QCalendarWidget::selectionChanged, this, &TodoCalendarWindow::onDateSelected);
    connect(m_btnSwitch, &QPushButton::clicked, this, &TodoCalendarWindow::onSwitchView);
    connect(m_btnAlarm, &QPushButton::clicked, this, &TodoCalendarWindow::onAddAlarm);
    connect(m_btnAdd, &QPushButton::clicked, this, &TodoCalendarWindow::onAddTodo);
    connect(m_todoList, &QListWidget::itemDoubleClicked, this, &TodoCalendarWindow::onEditTodo);
    connect(&DatabaseManager::instance(), &DatabaseManager::todoChanged, this, &TodoCalendarWindow::refreshTodos);
}

void TodoCalendarWindow::initUI() {
    auto* mainLayout = new QHBoxLayout(m_contentArea);
    mainLayout->setContentsMargins(15, 15, 15, 15);
    mainLayout->setSpacing(20);

    // [CRITICAL] 锁定：布局迁移。左侧面板占 35% 宽度。
    auto* leftPanel = new QWidget(this);
    auto* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(10);

    m_dateLabel = new QLabel(this);
    m_dateLabel->setStyleSheet("font-size: 18px; font-weight: bold; color: #4facfe; margin-bottom: 10px;");
    leftLayout->addWidget(m_dateLabel);

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

    mainLayout->addWidget(leftPanel, 35);

    // [CRITICAL] 锁定：右侧面板占 65% 宽度，支持月历/24h 视图切换。
    auto* rightPanel = new QWidget(this);
    auto* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(5);

    auto* rightHeader = new QHBoxLayout();
    rightHeader->addStretch();

    m_btnAlarm = new QPushButton(this);
    m_btnAlarm->setFixedSize(32, 32);
    m_btnAlarm->setIcon(IconHelper::getIcon("bell", "#ccc"));
    m_btnAlarm->setToolTip("创建重复提醒闹钟");
    m_btnAlarm->setStyleSheet("QPushButton { background: transparent; border: 1px solid #444; border-radius: 4px; } QPushButton:hover { background: #444; }");
    rightHeader->addWidget(m_btnAlarm);

    m_btnSwitch = new QPushButton(this);
    m_btnSwitch->setFixedSize(32, 32);
    m_btnSwitch->setIcon(IconHelper::getIcon("clock", "#ccc"));
    m_btnSwitch->setToolTip("切换日历/24h详细视图");
    m_btnSwitch->setStyleSheet("QPushButton { background: transparent; border: 1px solid #444; border-radius: 4px; } QPushButton:hover { background: #444; }");
    rightHeader->addWidget(m_btnSwitch);
    rightLayout->addLayout(rightHeader);

    m_viewStack = new QStackedWidget(this);

    // 视图 1：月视图 (日历)
    m_calendar = new CustomCalendar(this);
    m_calendar->setGridVisible(true);
    m_calendar->setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader);
    m_calendar->setStyleSheet(
        "QCalendarWidget QAbstractItemView { background-color: #1e1e1e; color: #dcdcdc; selection-background-color: #007acc; selection-color: white; outline: none; }"
        "QCalendarWidget QHeaderView::section { background-color: #252526; color: #888; border: none; height: 35px; }"
        "QCalendarWidget QWidget#qt_calendar_navigationbar { background-color: #2d2d2d; border-bottom: 1px solid #333; }"
        "QCalendarWidget QToolButton { color: #eee; font-weight: bold; background-color: transparent; border: none; padding: 5px 15px; min-width: 60px; }"
        "QCalendarWidget QToolButton:hover { background-color: #444; border-radius: 4px; }"
        "QCalendarWidget QMenu { background-color: #2d2d2d; color: #eee; border: 1px solid #444; }"
        "QCalendarWidget QMenu::item:selected { background-color: #007acc; }"
        "QCalendarWidget QSpinBox { background-color: #2d2d2d; color: #eee; selection-background-color: #007acc; border: 1px solid #444; margin-right: 5px; }"
    );
    m_viewStack->addWidget(m_calendar);

    // 视图 2：详细 24h 视图
    m_detailed24hList = new QListWidget(this);
    m_detailed24hList->setStyleSheet(
        "QListWidget { background-color: #1e1e1e; border: 1px solid #333; border-radius: 4px; color: #dcdcdc; font-size: 14px; }"
        "QListWidget::item { padding: 15px; border-bottom: 1px solid #2d2d2d; min-height: 50px; }"
        "QListWidget::item:hover { background-color: #2d2d2d; }"
    );
    m_viewStack->addWidget(m_detailed24hList);

    rightLayout->addWidget(m_viewStack);
    mainLayout->addWidget(rightPanel, 65);
    
    onDateSelected();
}

void TodoCalendarWindow::showEvent(QShowEvent* event) {
    FramelessDialog::showEvent(event);
    refreshTodos();
}

bool TodoCalendarWindow::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::ContextMenu) {
        auto* menu = new QMenu(this);
        IconHelper::setupMenu(menu);
        menu->setStyleSheet("QMenu { background-color: #2d2d2d; color: #eee; border: 1px solid #444; } QMenu::item:selected { background-color: #3e3e42; }");

        auto* addAction = menu->addAction(IconHelper::getIcon("add", "#4facfe"), "新增待办事项");
        auto* detailAction = menu->addAction(IconHelper::getIcon("clock", "#4facfe"), "查看日排程视图");

        connect(addAction, &QAction::triggered, this, &TodoCalendarWindow::onAddTodo);
        connect(detailAction, &QAction::triggered, [this](){
            m_viewStack->setCurrentIndex(1); // 切换到24h视图
            onSwitchView(); // 更新图标
        });

        menu->exec(QCursor::pos());
        return true;
    }

    if (event->type() == QEvent::ToolTip || event->type() == QEvent::MouseMove) {
        QPoint pos;
        if (event->type() == QEvent::ToolTip) pos = static_cast<QHelpEvent*>(event)->pos();
        else pos = static_cast<QMouseEvent*>(event)->pos();

        // [CRITICAL] 锁定：日历 Tooltip 逻辑。通过坐标映射找到日期并显示待办。
        QWidget* view = m_calendar->findChild<QAbstractItemView*>();
        if (watched == m_calendar || watched == view) {
            QDate date = m_calendar->selectedDate();
            QList<DatabaseManager::Todo> todos = DatabaseManager::instance().getTodosByDate(date);
            if (!todos.isEmpty()) {
                QString tip = "<b>" + date.toString("yyyy-MM-dd") + " 待办概要:</b><br>";
                for (int i = 0; i < qMin((int)todos.size(), 5); ++i) {
                    const auto& t = todos[i];
                    QString time = t.startTime.isValid() ? "[" + t.startTime.toString("HH:mm") + "] " : "";
                    tip += "• " + time + t.title + "<br>";
                }
                if (todos.size() > 5) tip += QString("<i>...更多 (%1)</i>").arg(todos.size());
                QToolTip::showText(QCursor::pos(), tip, m_calendar);
            } else {
                QToolTip::hideText();
            }
        }
    }
    return FramelessDialog::eventFilter(watched, event);
}

void TodoCalendarWindow::update24hList(const QDate& date) {
    m_detailed24hList->clear();
    QList<DatabaseManager::Todo> todos = DatabaseManager::instance().getTodosByDate(date);
    
    for (int h = 0; h < 24; ++h) {
        QString timeStr = QString("%1:00").arg(h, 2, 10, QChar('0'));
        
        // [CRITICAL] 锁定：仅更新右侧详细列表。左侧冗余列表已移除。
        auto* itemDetailed = new QListWidgetItem(timeStr, m_detailed24hList);
        itemDetailed->setFont(QFont("Segoe UI", 12));
        bool hasTaskDetailed = false;
        for (const auto& t : todos) {
            if (t.startTime.isValid() && t.startTime.date() == date && t.startTime.time().hour() == h) {
                QString displayTime = t.startTime.toString("HH:mm");
                if (t.endTime.isValid()) displayTime += " - " + t.endTime.toString("HH:mm");
                itemDetailed->setText(QString("%1   |   %2").arg(displayTime, -15).arg(t.title));
                itemDetailed->setForeground(QColor("#4facfe"));
                
                if (t.status == 1) {
                    itemDetailed->setIcon(IconHelper::getIcon("select", "#666", 20));
                    itemDetailed->setForeground(QColor("#666"));
                } else if (t.status == 2) {
                    itemDetailed->setIcon(IconHelper::getIcon("close", "#e74c3c", 20));
                } else {
                    itemDetailed->setIcon(IconHelper::getIcon("circle_filled", "#007acc", 12));
                }
                hasTaskDetailed = true;
                break;
            }
        }
        if (!hasTaskDetailed) itemDetailed->setForeground(QColor("#444"));
        m_detailed24hList->addItem(itemDetailed);
    }
}

void TodoCalendarWindow::onDateSelected() {
    QDate date = m_calendar->selectedDate();
    m_dateLabel->setText(date.toString("yyyy年M月d日"));
    refreshTodos();
    update24hList(date);
}

void TodoCalendarWindow::onSwitchView() {
    int nextIdx = (m_viewStack->currentIndex() + 1) % 2;
    m_viewStack->setCurrentIndex(nextIdx);
    
    if (nextIdx == 0) {
        m_btnSwitch->setIcon(IconHelper::getIcon("clock", "#ccc"));
        m_btnSwitch->setToolTip("切换到24h详细视图");
    } else {
        m_btnSwitch->setIcon(IconHelper::getIcon("calendar", "#ccc"));
        m_btnSwitch->setToolTip("切换到月历视图");
    }
}

void TodoCalendarWindow::refreshTodos() {
    m_todoList->clear();
    QDate date = m_calendar->selectedDate();
    QList<DatabaseManager::Todo> todos = DatabaseManager::instance().getTodosByDate(date);

    // [CRITICAL] 锁定：逾期任务强制置顶。
    std::sort(todos.begin(), todos.end(), [](const DatabaseManager::Todo& a, const DatabaseManager::Todo& b){
        if (a.status == 2 && b.status != 2) return true;
        if (a.status != 2 && b.status == 2) return false;
        return a.priority > b.priority;
    });

    for (const auto& t : todos) {
        auto* item = new QListWidgetItem(m_todoList);
        QString timeStr = t.startTime.isValid() ? t.startTime.toString("HH:mm") : "--:--";
        if (t.endTime.isValid()) timeStr += " - " + t.endTime.toString("HH:mm");
        
        QString titleText = t.title;
        if (t.repeatMode > 0) titleText += " 🔄";
        if (t.noteId > 0) titleText += " 📝";
        if (t.progress > 0 && t.progress < 100) titleText += QString(" (%1%)").arg(t.progress);

        item->setText(QString("%1 %2").arg(timeStr).arg(titleText));
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
            item->setBackground(QColor(231, 76, 60, 30));
        } else {
            item->setIcon(IconHelper::getIcon("circle_filled", "#007acc", 8));
        }
        
        if (t.priority > 0 && t.status != 2) {
            item->setBackground(QColor(0, 122, 204, 30));
        }

        m_todoList->addItem(item);
    }
}

void TodoCalendarWindow::onAddAlarm() {
    DatabaseManager::Todo t;
    t.title = "新闹钟";
    t.startTime = QDateTime::currentDateTime();
    t.endTime = t.startTime.addSecs(60);
    t.repeatMode = 1; // 默认每天重复
    t.priority = 2;   // 闹钟默认为紧急

    TodoEditDialog dlg(t, this);
    if (dlg.exec() == QDialog::Accepted) {
        DatabaseManager::instance().addTodo(dlg.getTodo());
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

    auto* extraLayout = new QHBoxLayout();
    m_comboRepeat = new QComboBox(this);
    m_comboRepeat->addItems({"不重复", "每天", "每周", "每月", "每小时", "每分钟", "每秒"});
    m_comboRepeat->setCurrentIndex(m_todo.repeatMode);
    m_comboRepeat->setStyleSheet("background: #333; color: white;");
    extraLayout->addWidget(new QLabel("重复:"));
    extraLayout->addWidget(m_comboRepeat);

    m_sliderProgress = new QSlider(Qt::Horizontal, this);
    m_sliderProgress->setRange(0, 100);
    m_sliderProgress->setValue(m_todo.progress);
    m_labelProgress = new QLabel(QString("%1%").arg(m_todo.progress), this);
    connect(m_sliderProgress, &QSlider::valueChanged, [this](int v){ m_labelProgress->setText(QString("%1%").arg(v)); });
    extraLayout->addWidget(new QLabel("进度:"));
    extraLayout->addWidget(m_sliderProgress);
    extraLayout->addWidget(m_labelProgress);
    layout->addLayout(extraLayout);

    auto* botLayout = new QHBoxLayout();
    m_comboPriority = new QComboBox(this);
    m_comboPriority->addItems({"普通", "高优先级", "紧急"});
    m_comboPriority->setCurrentIndex(m_todo.priority);
    m_comboPriority->setStyleSheet("background: #333; color: white;");
    botLayout->addWidget(new QLabel("优先级:"));
    botLayout->addWidget(m_comboPriority);

    // [PROFESSIONAL] 如果有关联笔记，显示跳转按钮
    if (m_todo.noteId > 0) {
        auto* btnJump = new QPushButton("跳转笔记", this);
        btnJump->setIcon(IconHelper::getIcon("link", "#ffffff"));
        btnJump->setStyleSheet("background: #27ae60; color: white; padding: 8px 15px; border-radius: 4px;");
        connect(btnJump, &QPushButton::clicked, [this](){
             // 这里通常通过信号发给 MainWindow，或者通过 QuickPreview。为了简单实现：
             ToolTipOverlay::instance()->showText(QCursor::pos(), "跳转逻辑已触发");
        });
        botLayout->addWidget(btnJump);
    }
    
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
    m_todo.repeatMode = m_comboRepeat->currentIndex();
    m_todo.progress = m_sliderProgress->value();
    
    if (m_todo.progress == 100) m_todo.status = 1; // 自动完成
    
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
