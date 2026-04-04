#pragma once

#include <QMainWindow>
#include <QSplitter>
#include <QToolBar>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QStackedWidget>
#include <QHBoxLayout>

namespace ArcMeta {

class BreadcrumbBar;
class CategoryPanel;
class NavPanel;
class ContentPanel;
class MetaPanel;
class FilterPanel;

/**
 * @brief 主窗口类
 * 负责六栏布局的组装、QSplitter 管理及自定义标题栏按钮
 */
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override = default;

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onPinToggled(bool checked);
    void onBackClicked();
    void onForwardClicked();
    void onUpClicked();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    QWidget* m_titleBarWidget = nullptr;
    QHBoxLayout* m_titleBarLayout = nullptr;
    QLabel* m_appNameLabel = nullptr;
    QWidget* m_navBarWidget = nullptr;
    QHBoxLayout* m_navBarLayout = nullptr;

    void initUi();
    void updateNavButtons();
    void updateStatusBar();
    void navigateTo(const QString& path, bool record = true);
    void initToolbar();
    void setupSplitters();
    void setupCustomTitleBarButtons();

    // 面包屑地址栏
    BreadcrumbBar* m_breadcrumbBar = nullptr;
    QStackedWidget* m_pathStack = nullptr;

    // 六个面板
    CategoryPanel* m_categoryPanel = nullptr;
    NavPanel* m_navPanel = nullptr;
    ContentPanel* m_contentPanel = nullptr;
    MetaPanel* m_metaPanel = nullptr;
    FilterPanel* m_filterPanel = nullptr;

    QSplitter* m_mainSplitter = nullptr;

    // 工具栏组件
    QToolBar* m_toolbar = nullptr;
    QLineEdit* m_pathEdit = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QPushButton* m_btnBack = nullptr;
    QPushButton* m_btnForward = nullptr;
    QPushButton* m_btnUp = nullptr;
    
    // 标题栏按钮组 (用于 frameless 时的模拟，此处作为标准按钮展示)
    QPushButton* m_btnCreate = nullptr;
    QPushButton* m_btnPinTop = nullptr;
    QPushButton* m_btnMin = nullptr;
    QPushButton* m_btnMax = nullptr;
    QPushButton* m_btnClose = nullptr;

    // 状态管理
    bool m_isPinned = false;
    QString m_currentPath;
    QStringList m_history;
    int m_historyIndex = -1;

    // 底部状态栏
    QLabel* m_statusLeft = nullptr;
    QLabel* m_statusCenter = nullptr;
    QLabel* m_statusRight = nullptr;

    // 窗口拖动
    bool m_isDragging = false;
    QPoint m_dragPosition;
};

} // namespace ArcMeta
