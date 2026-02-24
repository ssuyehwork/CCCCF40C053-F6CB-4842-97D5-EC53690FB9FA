#include "SystemTray.h"
#include "StringUtils.h"
#include "../core/DatabaseManager.h"

#include "IconHelper.h"
#include "FloatingBall.h"
#include <QApplication>
#include <QIcon>
#include <QStyle>

SystemTray::SystemTray(QObject* parent) : QObject(parent) {
    m_trayIcon = new QSystemTrayIcon(this);
    
    // 复刻 Python 版：使用渲染的悬浮球作为托盘图标
    m_trayIcon->setIcon(FloatingBall::generateBallIcon());
    m_trayIcon->setToolTip("快速笔记");

    m_menu = new QMenu();
    IconHelper::setupMenu(m_menu);
    m_menu->setStyleSheet(
        "QMenu { background-color: #2D2D2D; color: #EEE; border: 1px solid #444; padding: 4px; } "
        /* 10px 间距规范：padding-left 10px + icon margin-left 6px */
        "QMenu::item { padding: 6px 10px 6px 10px; border-radius: 3px; } "
        "QMenu::icon { margin-left: 6px; } "
        "QMenu::item:selected { background-color: #4a90e2; color: white; }"
    );
    
    m_menu->addAction(IconHelper::getIcon("monitor", "#aaaaaa", 18), "显示主界面", this, &SystemTray::showMainWindow);
    m_menu->addAction(IconHelper::getIcon("zap", "#aaaaaa", 18), "显示快速笔记", this, &SystemTray::showQuickWindow);
    m_menu->addAction(IconHelper::getIcon("calendar", "#aaaaaa", 18), "待办事项", this, &SystemTray::showTodoCalendar);

    m_menu->addSeparator();
    // 今日待办动态子菜单
    QMenu* todoMenu = new QMenu("今日待办", m_menu);
    todoMenu->setIcon(IconHelper::getIcon("todo", "#aaaaaa", 18));
    m_menu->addMenu(todoMenu);
    connect(m_menu, &QMenu::aboutToShow, [=](){
        todoMenu->clear();
        QList<DatabaseManager::Todo> todayTodos = DatabaseManager::instance().getTodosByDate(QDate::currentDate());
        if (todayTodos.isEmpty()) {
            QAction* empty = todoMenu->addAction("今日暂无任务");
            empty->setEnabled(false);
        } else {
            for (const auto& t : todayTodos) {
                QString time = t.startTime.isValid() ? t.startTime.toString("HH:mm") : "全天";
                QAction* action = todoMenu->addAction(time + " " + t.title);
                if (t.status == 1) {
                    action->setIcon(IconHelper::getIcon("select", "#2ecc71", 16));
                } else if (t.status == 2) {
                    action->setIcon(IconHelper::getIcon("close", "#e74c3c", 16));
                } else {
                    action->setIcon(IconHelper::getIcon("circle_filled", "#007acc", 8));
                }
            }
        }
    });

    m_menu->addSeparator();
    
    m_ballAction = new QAction("隐藏悬浮球", this);
    m_ballAction->setIcon(IconHelper::getIcon("ball_off", "#aaaaaa", 18));
    connect(m_ballAction, &QAction::triggered, this, [this](){
        bool willBeVisible = (m_ballAction->text() == "显示悬浮球");
        emit toggleFloatingBall(willBeVisible);
    });
    m_menu->addAction(m_ballAction);

    m_menu->addAction(IconHelper::getIcon("help", "#aaaaaa", 18), "使用说明", this, &SystemTray::showHelpRequested);
    m_menu->addAction(IconHelper::getIcon("settings", "#aaaaaa", 18), "设置", this, &SystemTray::showSettings);
    m_menu->addSeparator();
    m_menu->addAction(IconHelper::getIcon("power", "#aaaaaa", 18), "退出程序", this, &SystemTray::quitApp);

    m_trayIcon->setContextMenu(m_menu);

    connect(m_trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason){
        if (reason == QSystemTrayIcon::Trigger) {
            emit showQuickWindow();
        }
    });
}

void SystemTray::show() {
    m_trayIcon->show();
}

void SystemTray::updateBallAction(bool visible) {
    if (visible) {
        m_ballAction->setText("隐藏悬浮球");
        m_ballAction->setIcon(IconHelper::getIcon("ball_off", "#aaaaaa", 18));
    } else {
        m_ballAction->setText("显示悬浮球");
        m_ballAction->setIcon(IconHelper::getIcon("ball_on", "#aaaaaa", 18));
    }
}
