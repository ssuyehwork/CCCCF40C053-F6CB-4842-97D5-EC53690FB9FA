#ifndef SYSTEMTRAY_H
#define SYSTEMTRAY_H

#include <QSystemTrayIcon>
#include <QMenu>
#include <QObject>

class SystemTray : public QObject {
    Q_OBJECT
public:
    explicit SystemTray(QObject* parent = nullptr);
    void show();

signals:
    void showMainWindow();
    void showQuickWindow();
    void showHelpRequested();
    void showSettings();
    void quitApp();

private:
    QSystemTrayIcon* m_trayIcon;
    QMenu* m_menu;
};

#endif // SYSTEMTRAY_H
