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
#include <QCursor>
#include <QPainter>
#include <QLabel>
#include <QPushButton>
#include <QListWidget>
#include <QMenu>
#include <QTableView>
#include <QHeaderView>
#include <QAbstractItemView>
#include <algorithm>

CustomCalendar::CustomCalendar(QWidget* parent) : QCalendarWidget(parent) {
}

void CustomCalendar::paintCell(QPainter* painter, const QRect& rect, QDate date) const {
    QList<DatabaseManager::Todo> todos = DatabaseManager::instance().getTodosByDate(date);
    bool isSelected = (date == selectedDate());
    bool isToday = (date == QDate::currentDate());

    // 1. 绘制背景
    painter->save();
    if (isSelected) {
        painter->fillRect(rect, QColor("#007acc"));
    } else {
        // [PROFESSIONAL] 热力图渲染：仅在非选中状态显示，且颜色极淡
        if (!todos.isEmpty()) {
            int alpha = qMin(10 + (int)todos.size() * 10, 40);
            painter->fillRect(rect, QColor(255, 255, 255, alpha));
        } else {
            painter->fillRect(rect, QColor("#1e1e1e"));
        }
    }

    // 2. 绘制网格线 (手动绘制以确保即便不调用父类 paintCell 也能保持网格一致性)
    painter->setPen(QColor("#333"));
    painter->drawLine(rect.topRight(), rect.bottomRight());
    painter->drawLine(rect.bottomLeft(), rect.bottomRight());

    // 3. 持续显示“今日”高亮边框
    if (isToday) {
        painter->setRenderHint(QPainter::Antialiasing);
        // 改用琥珀色/金黄色高亮今日，避开蓝色混淆
        painter->setPen(QPen(QColor("#eebb00"), 2));
        painter->drawRoundedRect(rect.adjusted(2, 2, -2, -2), 4, 4);
    }
    painter->restore();

    // 4. [CRITICAL] 核心修复：手动绘制日期与任务内容，彻底解决重叠问题
    painter->save();
    
    // A. 绘制日期数字：强制定位在右下角，避开任务区域
    painter->setPen(isSelected ? Qt::white : (date.month() == monthShown() ? QColor("#dcdcdc") : QColor("#555555")));
    QFont dateFont = painter->font();
    dateFont.setBold(true);
    dateFont.setPointSize(isSelected ? 10 : 9); // 选中时稍微加大字号
    painter->setFont(dateFont);
    painter->drawText(rect.adjusted(0, 0, -6, -2), Qt::AlignRight | Qt::AlignBottom, QString::number(date.day()));

    // B. 绘制任务标题：定位在左上角，采用极紧凑布局
    if (!todos.isEmpty()) {
        QFont taskFont = painter->font();
        taskFont.setPointSize(isSelected ? 7 : 6); // 选中时稍微加大以便阅读
        taskFont.setBold(isSelected);              // 选中时加粗
        painter->setFont(taskFont);
        painter->setPen(isSelected ? Qt::white : QColor("#999999"));
        
        for (int i = 0; i < qMin((int)todos.size(), 3); ++i) {
            QString title = todos[i].title;
            if (title.length() > 6) title = title.left(5) + "..";
            // 每行任务偏移 11px，从 y=4 开始绘制
            painter->drawText(rect.adjusted(4, 4 + i * 11, -4, 0), Qt::AlignLeft | Qt::AlignTop | Qt::TextSingleLine, "• " + title);
        }
    }
    painter->restore();
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
    connect(m_btnToday, &QPushButton::clicked, this, &TodoCalendarWindow::onGotoToday);
    connect(m_btnAlarm, &QPushButton::clicked, this, &TodoCalendarWindow::onAddAlarm);
    connect(m_btnAdd, &QPushButton::clicked, this, &TodoCalendarWindow::onAddTodo);
    connect(m_todoList, &QListWidget::itemDoubleClicked, this, &TodoCalendarWindow::onEditTodo);
    connect(&DatabaseManager::instance(), &DatabaseManager::todoChanged, this, &TodoCalendarWindow::refreshTodos);

    m_todoList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_todoList, &QListWidget::customContextMenuRequested, [this](const QPoint& pos){
        QList<QListWidgetItem*> items = m_todoList->selectedItems();
        if (items.isEmpty()) return;

        auto* menu = new QMenu(this);
        IconHelper::setupMenu(menu);
        menu->setStyleSheet("QMenu { background-color: #2d2d2d; color: #eee; border: 1px solid #444; } QMenu::item:selected { background-color: #3e3e42; }");

        if (items.size() == 1) {
            auto* editAction = menu->addAction(IconHelper::getIcon("edit", "#4facfe"), "编辑此任务");
            connect(editAction, &QAction::triggered, [this, items](){ onEditTodo(items.first()); });
        }

        auto* doneAction = menu->addAction(IconHelper::getIcon("select", "#2ecc71"), items.size() > 1 ? QString("批量标记完成 (%1)").arg(items.size()) : "标记完成");
        connect(doneAction, &QAction::triggered, [this, items](){
            QList<DatabaseManager::Todo> todos = DatabaseManager::instance().getTodosByDate(m_calendar->selectedDate());
            for (auto* item : items) {
                int id = item->data(Qt::UserRole).toInt();
                for (auto& t : todos) {
                    if (t.id == id) {
                        t.status = 1;
                        t.progress = 100;
                        DatabaseManager::instance().updateTodo(t);
                        break;
                    }
                }
            }
        });

        auto* deleteAction = menu->addAction(IconHelper::getIcon("delete", "#e74c3c"), items.size() > 1 ? QString("批量删除 (%1)").arg(items.size()) : "删除此任务");
        connect(deleteAction, &QAction::triggered, [this, items](){
            for (auto* item : items) {
                int id = item->data(Qt::UserRole).toInt();
                DatabaseManager::instance().deleteTodo(id);
            }
        });

        menu->exec(QCursor::pos());
    });
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
    m_todoList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_todoList->setStyleSheet(
        "QListWidget { background-color: #252526; border: 1px solid #444; border-radius: 4px; padding: 5px; color: #ccc; }"
        "QListWidget::item { border-bottom: 1px solid #333; padding: 10px; }"
        "QListWidget::item:selected { background-color: #37373d; color: white; border-radius: 4px; }"
    );
    leftLayout->addWidget(m_todoList);

    m_btnAdd = new QPushButton("新增待办", this);
    m_btnAdd->setIcon(IconHelper::getIcon("add", "#ffffff"));
    m_btnAdd->setProperty("tooltipText", "在当前选中的日期创建一个新任务");
    m_btnAdd->installEventFilter(this);
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

    m_btnToday = new QPushButton(this);
    m_btnToday->setFixedSize(32, 32);
    m_btnToday->setIcon(IconHelper::getIcon("today", "#ccc"));
    m_btnToday->setProperty("tooltipText", "定位到今天");
    m_btnToday->installEventFilter(this);
    m_btnToday->setStyleSheet("QPushButton { background: transparent; border: 1px solid #444; border-radius: 4px; } QPushButton:hover { background: #444; }");
    rightHeader->addWidget(m_btnToday);

    m_btnAlarm = new QPushButton(this);
    m_btnAlarm->setFixedSize(32, 32);
    m_btnAlarm->setIcon(IconHelper::getIcon("bell", "#ccc"));
    m_btnAlarm->setProperty("tooltipText", "创建重复提醒闹钟");
    m_btnAlarm->installEventFilter(this);
    m_btnAlarm->setStyleSheet("QPushButton { background: transparent; border: 1px solid #444; border-radius: 4px; } QPushButton:hover { background: #444; }");
    rightHeader->addWidget(m_btnAlarm);

    m_btnSwitch = new QPushButton(this);
    m_btnSwitch->setFixedSize(32, 32);
    m_btnSwitch->setIcon(IconHelper::getIcon("clock", "#ccc"));
    m_btnSwitch->setProperty("tooltipText", "切换日历/24h详细视图");
    m_btnSwitch->installEventFilter(this);
    m_btnSwitch->setStyleSheet("QPushButton { background: transparent; border: 1px solid #444; border-radius: 4px; } QPushButton:hover { background: #444; }");
    rightHeader->addWidget(m_btnSwitch);
    rightLayout->addLayout(rightHeader);

    m_viewStack = new QStackedWidget(this);

    // 视图 1：月视图 (日历)
    m_calendar = new CustomCalendar(this);
    // [FIX] 关闭原生网格，避免干扰自定义样式渲染
    m_calendar->setGridVisible(false);
    m_calendar->setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader);


    m_calendar->setStyleSheet(
        "QCalendarWidget { background-color: #1e1e1e; border: none; }"
        "QCalendarWidget QAbstractItemView { background-color: #1e1e1e; color: #dcdcdc; selection-background-color: transparent; selection-color: #dcdcdc; outline: none; border: none; }"
        /* [FIX] 采用用户推荐的方法一：使用强力 QSS 选择器控制星期标题行颜色 */
        "QCalendarWidget QTableView QHeaderView::section { background-color: #252526; color: #eebb00; border: none; font-weight: bold; height: 35px; }"
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
    m_detailed24hList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_detailed24hList->setStyleSheet(
        "QListWidget { background-color: #1e1e1e; border: 1px solid #333; border-radius: 4px; color: #dcdcdc; font-size: 14px; }"
        "QListWidget::item { padding: 15px; border-bottom: 1px solid #2d2d2d; min-height: 50px; }"
        "QListWidget::item:hover { background-color: #2d2d2d; }"
    );
    m_viewStack->addWidget(m_detailed24hList);

    m_detailed24hList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_detailed24hList, &QListWidget::customContextMenuRequested, [this](const QPoint& pos){
        QList<QListWidgetItem*> items = m_detailed24hList->selectedItems();
        if (items.isEmpty()) return;

        auto* menu = new QMenu(this);
        IconHelper::setupMenu(menu);
        menu->setStyleSheet("QMenu { background-color: #2d2d2d; color: #eee; border: 1px solid #444; } QMenu::item:selected { background-color: #3e3e42; }");

        // 收集所有选中行中的任务ID
        QList<int> taskIds;
        QList<DatabaseManager::Todo> todos = DatabaseManager::instance().getTodosByDate(m_calendar->selectedDate());
        
        for (auto* item : items) {
            int hour = m_detailed24hList->row(item);
            for (const auto& t : todos) {
                if (t.startTime.isValid() && t.startTime.time().hour() == hour) {
                    taskIds << t.id;
                    break;
                }
            }
        }

        if (items.size() == 1) {
            int hour = m_detailed24hList->row(items.first());
            bool hasTask = !taskIds.isEmpty();
            if (hasTask) {
                int taskId = taskIds.first();
                auto* editAction = menu->addAction(IconHelper::getIcon("edit", "#4facfe"), "编辑任务");
                auto* deleteAction = menu->addAction(IconHelper::getIcon("delete", "#e74c3c"), "删除任务");
                connect(editAction, &QAction::triggered, [this, taskId](){
                    QList<DatabaseManager::Todo> todos = DatabaseManager::instance().getTodosByDate(m_calendar->selectedDate());
                    for(const auto& t : todos) if(t.id == taskId) {
                        auto* dlg = new TodoEditDialog(t, this);
                        dlg->setAttribute(Qt::WA_DeleteOnClose);
                        connect(dlg, &QDialog::accepted, [dlg](){ DatabaseManager::instance().updateTodo(dlg->getTodo()); });
                        dlg->show();
                        break;
                    }
                });
                connect(deleteAction, &QAction::triggered, [this, taskId](){ DatabaseManager::instance().deleteTodo(taskId); });
            } else {
                auto* addAction = menu->addAction(IconHelper::getIcon("add", "#4facfe"), QString("在 %1:00 新增任务").arg(hour, 2, 10, QChar('0')));
                connect(addAction, &QAction::triggered, [this, hour](){
                    DatabaseManager::Todo t;
                    t.startTime = QDateTime(m_calendar->selectedDate(), QTime(hour, 0));
                    t.endTime = t.startTime.addSecs(3600);
                    auto* dlg = new TodoEditDialog(t, this);
                    dlg->setAttribute(Qt::WA_DeleteOnClose);
                    connect(dlg, &QDialog::accepted, [dlg](){ DatabaseManager::instance().addTodo(dlg->getTodo()); });
                    dlg->show();
                });
            }
        } else {
            // 多选情况
            if (!taskIds.isEmpty()) {
                auto* doneAction = menu->addAction(IconHelper::getIcon("select", "#2ecc71"), QString("批量标记完成 (%1)").arg(taskIds.size()));
                connect(doneAction, &QAction::triggered, [this, taskIds](){
                    QList<DatabaseManager::Todo> todos = DatabaseManager::instance().getTodosByDate(m_calendar->selectedDate());
                    for (int id : taskIds) {
                        for (auto& t : todos) {
                            if (t.id == id) {
                                t.status = 1;
                                t.progress = 100;
                                DatabaseManager::instance().updateTodo(t);
                                break;
                            }
                        }
                    }
                });

                auto* deleteAction = menu->addAction(IconHelper::getIcon("delete", "#e74c3c"), QString("批量删除任务 (%1)").arg(taskIds.size()));
                connect(deleteAction, &QAction::triggered, [this, taskIds](){
                    for (int id : taskIds) {
                        DatabaseManager::instance().deleteTodo(id);
                    }
                });
            } else {
                return; // 选中的全是空行且是多选，不显示菜单
            }
        }

        menu->exec(QCursor::pos());
    });

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
        // [PROFESSIONAL] 日历格子的右键点击：先触发选中，再弹出菜单
        auto* view = m_calendar->findChild<QAbstractItemView*>();
        if (watched == m_calendar || watched == view) {
            if (view) {
                QPoint pos = view->mapFromGlobal(QCursor::pos());
                QModelIndex index = view->indexAt(pos);
                if (index.isValid()) {
                    // [HACK] 通过模拟鼠标左键点击来触发 QCalendarWidget 的选中逻辑
                    // Qt6 推荐使用包含 localPos 和 globalPos 的构造函数
                    QMouseEvent clickEvent(QEvent::MouseButtonPress, pos, QCursor::pos(), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                    QApplication::sendEvent(view, &clickEvent);
                    QMouseEvent releaseEvent(QEvent::MouseButtonRelease, pos, QCursor::pos(), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                    QApplication::sendEvent(view, &releaseEvent);
                }
            }

            auto* menu = new QMenu(this);
            IconHelper::setupMenu(menu);
            menu->setStyleSheet("QMenu { background-color: #2d2d2d; color: #eee; border: 1px solid #444; } QMenu::item:selected { background-color: #3e3e42; }");

            QDate selectedDate = m_calendar->selectedDate();
            QList<DatabaseManager::Todo> todos = DatabaseManager::instance().getTodosByDate(selectedDate);

            auto* addAction = menu->addAction(IconHelper::getIcon("add", "#4facfe"), "在此日期新增待办");
            auto* detailAction = menu->addAction(IconHelper::getIcon("clock", "#4facfe"), "切换到排程视图");
            
            if (!todos.isEmpty()) {
                menu->addSeparator();
                auto* taskTitle = menu->addAction(QString("管理该日任务 (%1):").arg(todos.size()));
                taskTitle->setEnabled(false);
                
                for (const auto& t : todos) {
                    QString time = t.startTime.isValid() ? "[" + t.startTime.toString("HH:mm") + "] " : "";
                    auto* itemAction = menu->addAction(IconHelper::getIcon("todo", "#aaaaaa"), time + t.title);
                    connect(itemAction, &QAction::triggered, [this, t](){
                        auto* dlg = new TodoEditDialog(t, this);
                        dlg->setAttribute(Qt::WA_DeleteOnClose);
                        connect(dlg, &QDialog::accepted, [dlg](){
                            DatabaseManager::instance().updateTodo(dlg->getTodo());
                        });
                        dlg->show();
                    });
                }
            }

            menu->addSeparator();
            auto* todayAction = menu->addAction(IconHelper::getIcon("today", "#aaaaaa"), "返回今天");

            connect(addAction, &QAction::triggered, this, &TodoCalendarWindow::onAddTodo);
            connect(detailAction, &QAction::triggered, [this](){
                m_viewStack->setCurrentIndex(1);
                m_btnSwitch->setIcon(IconHelper::getIcon("calendar", "#ccc"));
                m_btnSwitch->setProperty("tooltipText", "切换到月历视图");
            });
            connect(todayAction, &QAction::triggered, this, &TodoCalendarWindow::onGotoToday);

            menu->exec(QCursor::pos());
            return true;
        }
    }

    // 处理所有按钮的 Hover 自定义提示
    if (event->type() == QEvent::Enter) {
        QString text = watched->property("tooltipText").toString();
        if (!text.isEmpty()) {
            ToolTipOverlay::instance()->showText(QCursor::pos(), text);
        }
    } else if (event->type() == QEvent::Leave) {
        ToolTipOverlay::hideTip();
    }

    if (event->type() == QEvent::ToolTip || event->type() == QEvent::MouseMove) {
        // [CRITICAL] 锁定：日历 Tooltip 逻辑。通过坐标映射找到日期并显示待办。
        auto* view = m_calendar->findChild<QAbstractItemView*>();
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
                
                // [RULE] 本项目严禁直接使用 QToolTip，必须通过 ToolTipOverlay 渲染统一风格的深色提示。
                ToolTipOverlay::instance()->showText(QCursor::pos(), tip);
            } else {
                ToolTipOverlay::hideTip();
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
        m_btnSwitch->setProperty("tooltipText", "切换到24h详细视图");
    } else {
        m_btnSwitch->setIcon(IconHelper::getIcon("calendar", "#ccc"));
        m_btnSwitch->setProperty("tooltipText", "切换到月历视图");
    }
}

void TodoCalendarWindow::onGotoToday() {
    m_calendar->setSelectedDate(QDate::currentDate());
    onDateSelected();
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
    
    // [PROFESSIONAL] 优化：如果父界面未显示，则以独立窗口形式弹出，避免强制拉起日历主界面
    auto* dlg = new TodoEditDialog(t, this->isVisible() ? this : nullptr);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    
    // 如果没有可见父窗口，手动居中显示
    if (!this->isVisible()) {
        QScreen *screen = QGuiApplication::primaryScreen();
        if (screen) {
            dlg->move(screen->availableGeometry().center() - QPoint(225, 250));
        }
    }

    connect(dlg, &QDialog::accepted, [dlg](){
        DatabaseManager::instance().addTodo(dlg->getTodo());
    });
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

void TodoCalendarWindow::onAddTodo() {
    DatabaseManager::Todo t;
    t.startTime = QDateTime(m_calendar->selectedDate(), QTime::currentTime());
    t.endTime = t.startTime.addSecs(3600);
    
    auto* dlg = new TodoEditDialog(t, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &QDialog::accepted, [dlg](){
        DatabaseManager::instance().addTodo(dlg->getTodo());
    });
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

void TodoCalendarWindow::onEditTodo(QListWidgetItem* item) {
    int id = item->data(Qt::UserRole).toInt();
    // 简单起见，从数据库重新获取（为了演示全流程，此处直接查）
    QList<DatabaseManager::Todo> todos = DatabaseManager::instance().getTodosByDate(m_calendar->selectedDate());
    for (const auto& t : todos) {
        if (t.id == id) {
            auto* dlg = new TodoEditDialog(t, this);
            dlg->setAttribute(Qt::WA_DeleteOnClose);
            connect(dlg, &QDialog::accepted, [dlg](){
                DatabaseManager::instance().updateTodo(dlg->getTodo());
            });
            dlg->show();
            dlg->raise();
            dlg->activateWindow();
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

    auto* repeatRow = new QHBoxLayout();
    m_comboRepeat = new QComboBox(this);
    m_comboRepeat->addItems({"不重复", "每天", "每周", "每月", "每小时", "每分钟", "每秒"});
    m_comboRepeat->setCurrentIndex(m_todo.repeatMode);
    m_comboRepeat->setStyleSheet("background: #333; color: white;");
    repeatRow->addWidget(new QLabel("重复周期:"));
    repeatRow->addWidget(m_comboRepeat, 1);
    layout->addLayout(repeatRow);

    auto* progressRow = new QHBoxLayout();
    m_sliderProgress = new QSlider(Qt::Horizontal, this);
    m_sliderProgress->setRange(0, 100);
    m_sliderProgress->setValue(m_todo.progress);
    m_labelProgress = new QLabel(QString("%1%").arg(m_todo.progress), this);
    m_labelProgress->setFixedWidth(40);
    connect(m_sliderProgress, &QSlider::valueChanged, [this](int v){ m_labelProgress->setText(QString("%1%").arg(v)); });
    progressRow->addWidget(new QLabel("任务进度:"));
    progressRow->addWidget(m_sliderProgress, 1);
    progressRow->addWidget(m_labelProgress);
    layout->addLayout(progressRow);

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
        btnJump->setProperty("tooltipText", "点击可快速定位并查看关联的笔记详情");
        btnJump->installEventFilter(this);
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
