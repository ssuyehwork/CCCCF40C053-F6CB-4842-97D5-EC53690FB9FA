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
    void showTodoCalendar();
    void showHelpRequested();
    void showSettings();
    void quitApp();
    void toggleFloatingBall(bool visible);

public slots:
    void updateBallAction(bool visible);

private:
    QSystemTrayIcon* m_trayIcon;
    QMenu* m_menu;
    QAction* m_ballAction;
};

#endif // SYSTEMTRAY_H
