#include "SystemTray.h"
#include "StringUtils.h"

#include "IconHelper.h"
#include <QApplication>
#include <QIcon>
#include <QStyle>

SystemTray::SystemTray(QObject* parent) : QObject(parent) {
    m_trayIcon = new QSystemTrayIcon(this);
    
    // 2026-03-20 恢复原版托盘图标
    m_trayIcon->setIcon(QIcon(":/app_icon.png"));
    // 2026-03-xx 按照项目宪法，严禁使用原生 ToolTip。
    m_trayIcon->setToolTip("");

    m_menu = new QMenu();
    IconHelper::setupMenu(m_menu);
    m_menu->setStyleSheet(
        "QMenu { background-color: #2D2D2D; color: #EEE; border: 1px solid #444; padding: 4px; } "
        "QMenu::item { padding: 6px 10px 6px 10px; border-radius: 3px; } "
        "QMenu::icon { margin-left: 6px; } "
        "QMenu::item:selected { background-color: #3e3e42; color: white; }"
    );
    
    // 2026-03-23 瘦身：移除快速笔记及工具项，仅保留主界面入口
    m_menu->addAction(IconHelper::getIcon("home", "#aaaaaa", 18), "显示主界面", this, &SystemTray::showMainWindow);
    
    m_menu->addSeparator();
    
    m_menu->addAction(IconHelper::getIcon("help", "#aaaaaa", 18), "使用说明", this, &SystemTray::showHelpRequested);
    m_menu->addAction(IconHelper::getIcon("settings", "#aaaaaa", 18), "设置", this, &SystemTray::showSettings);
    m_menu->addSeparator();
    m_menu->addAction(IconHelper::getIcon("power", "#aaaaaa", 18), "退出程序", this, &SystemTray::quitApp);

    m_trayIcon->setContextMenu(m_menu);

    connect(m_trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason){
        if (reason == QSystemTrayIcon::Trigger) {
            emit showMainWindow();
        }
    });
}

void SystemTray::show() {
    m_trayIcon->show();
}
