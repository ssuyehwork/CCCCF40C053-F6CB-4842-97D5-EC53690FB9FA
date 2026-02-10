#include "SystemTray.h"
#include "StringUtils.h"

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
    m_menu->setStyleSheet(
        "QMenu { background-color: #2D2D2D; color: #EEE; border: 1px solid #444; padding: 4px; } "
        /* 10px 间距规范：padding-left 10px + icon margin-left 6px */
        "QMenu::item { padding: 6px 10px 6px 10px; border-radius: 3px; } "
        "QMenu::icon { margin-left: 6px; } "
        "QMenu::item:selected { background-color: #4a90e2; color: white; }"
    );
    
    m_menu->addAction(IconHelper::getIcon("monitor", "#aaaaaa", 18), "显示主界面", this, &SystemTray::showMainWindow);
    m_menu->addAction(IconHelper::getIcon("zap", "#aaaaaa", 18), "显示快速笔记", this, &SystemTray::showQuickWindow);
    
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
