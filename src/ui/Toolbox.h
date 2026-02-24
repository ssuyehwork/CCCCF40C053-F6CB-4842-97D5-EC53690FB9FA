#ifndef TOOLBOX_H
#define TOOLBOX_H

#include "FramelessDialog.h"
#include <QPushButton>
#include <QPoint>
#include <QMoveEvent>
#include <QBoxLayout>
#include <functional>

class ToolTipOverlay;

class Toolbox : public FramelessDialog {
    Q_OBJECT
public:
    explicit Toolbox(QWidget* parent = nullptr);
    ~Toolbox();

    enum class Orientation {
        Horizontal,
        Vertical
    };

signals:
    void showMainWindowRequested();
    void showQuickWindowRequested();
    void showTimePasteRequested();
    void showPasswordGeneratorRequested();
    void showOCRRequested();
    void startOCRRequested();
    void showTagManagerRequested();
    void showFileSearchRequested();
    void showKeywordSearchRequested();
    void showColorPickerRequested();
    void startColorPickerRequested();
    void showPixelRulerRequested();
    void showHelpRequested();
    void showTodoCalendarRequested();
    void showAlarmRequested();
    void screenshotRequested();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

    // 工具箱自身始终置顶且由构造函数控制，跳过基类的通用置顶记忆逻辑
    void loadWindowSettings() override {}
    void saveWindowSettings() override {}

private slots:
    void toggleOrientation();
    void showConfigPanel();

private:
    void initUI();
    void updateLayout(Orientation orientation);
    void checkSnapping();
    QPushButton* createToolButton(const QString& tooltip, const QString& iconName, const QString& color);
    void loadSettings();
    void saveSettings();

    Orientation m_orientation = Orientation::Vertical;
    
    // 拖拽平滑化状态
    QPoint m_pressPos;
    bool m_isDragging = false;
    QPoint m_dragOffset;

    struct ToolInfo {
        QString id;
        QString tip;
        QString icon;
        QString color;
        std::function<void()> callback;
        QPushButton* btn = nullptr;
        bool visible = true;
    };
    QList<ToolInfo> m_toolInfos;

    QPushButton* m_btnRotate = nullptr;
    QPushButton* m_btnMenu = nullptr;
};

#endif // TOOLBOX_H
