#include "Toolbox.h"
#include "ToolTipOverlay.h"
#include "IconHelper.h"
#include "StringUtils.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QApplication>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QToolTip>
#include <QSettings>
#include <QCheckBox>
#include <QDialog>
#include <QWindow>
#include <QTransform>

Toolbox::Toolbox(QWidget* parent) : FramelessDialog("工具箱", parent) {
    setObjectName("ToolboxLauncher");
    
    // [CRITICAL] 强制开启非活动窗口的 ToolTip 显示。
    // setAttribute(Qt::WA_AlwaysShowToolTips); // Custom tooltip doesn't need this
    
    // 设置为工具窗口：任务栏不显示，且置顶
    setWindowFlags(windowFlags() | Qt::Tool | Qt::WindowStaysOnTopHint);

    // 关键修复：强制注入 ToolTip 样式。
    // 在 Windows 平台下，Qt::Tool 窗口的子控件在弹出 ToolTip 时往往无法正确继承全局 QSS。
    this->setStyleSheet("");
    
    // 允许通过拉伸边缘来调整大小
    setMinimumSize(40, 40);

    // [FINAL FIX] 从政策层面彻底禁绝右键菜单生成，配合 eventFilter 拦截全链路。
    setContextMenuPolicy(Qt::NoContextMenu);

    // 修改工具箱圆角为 6px
    QWidget* container = findChild<QWidget*>("DialogContainer");
    if (container) {
        container->setStyleSheet(container->styleSheet().replace("border-radius: 12px;", "border-radius: 6px;"));
    }

    initUI();
    loadSettings();
    updateLayout(m_orientation);
}

Toolbox::~Toolbox() {
    saveSettings();
}

void Toolbox::showEvent(QShowEvent* event) {
    saveSettings();
    FramelessDialog::showEvent(event);
}

void Toolbox::hideEvent(QHideEvent* event) {
    saveSettings();
    FramelessDialog::hideEvent(event);
}

void Toolbox::initUI() {
    // 隐藏默认标题文字，因为我们要把图标放上去
    m_titleLabel->hide();

    // 置顶按钮在工具箱中永久隐藏
    if (m_btnPin) m_btnPin->hide();

    // 将最小化按钮改为移动手柄
    if (m_minBtn) {
        // 仅断开与基类的连接，避免使用通配符 disconnect() 触发 destroyed 信号警告
        m_minBtn->disconnect(this); 
        m_minBtn->setIcon(IconHelper::getIcon("move", "#888888"));
        // m_minBtn->setToolTip("按住移动");
        m_minBtn->setToolTip(""); // [CRITICAL] 清除基类的“最小化”原生 Tooltip，避免与下方的 tooltipText 冲突
        m_minBtn->setProperty("tooltipText", "按住移动");
        m_minBtn->setCursor(Qt::SizeAllCursor);
        // 保留 Hover 背景提供视觉反馈
        m_minBtn->setStyleSheet("QPushButton { background: transparent; border: none; border-radius: 4px; } "
                             "QPushButton:hover { background-color: rgba(255, 255, 255, 0.1); }");
        
        // 安装事件过滤器以实现拖拽
        m_minBtn->installEventFilter(this);
    }
    
    // 清空内容区原有边距
    m_contentArea->layout() ? delete m_contentArea->layout() : (void)0;

    // 创建按钮列表
    auto addTool = [&](const QString& id, const QString& tip, const QString& icon, const QString& color, auto signal) {
        ToolInfo info;
        info.id = id;
        info.tip = tip;
        info.icon = icon;
        info.color = color;
        info.callback = [this, signal]() { emit (this->*signal)(); };
        info.btn = createToolButton(tip, icon, color);
        connect(info.btn, &QPushButton::clicked, this, info.callback);
        m_toolInfos.append(info);
    };

    addTool("time", "时间输出", "clock", "#1abc9c", &Toolbox::showTimePasteRequested);
    addTool("password", "密码生成器", "password_generator", "#3498db", &Toolbox::showPasswordGeneratorRequested);
    addTool("ocr", "批量识别", "text", "#4a90e2", &Toolbox::showOCRRequested);
    addTool("immediate_ocr", "截图取文", "screenshot_ocr", "#3498db", &Toolbox::startOCRRequested);
    addTool("tag", "标签管理", "tag", "#f1c40f", &Toolbox::showTagManagerRequested);
    addTool("file_search", "查找文件", "search", "#95a5a6", &Toolbox::showFileSearchRequested);
    addTool("keyword_search", "查找关键字", "find_keyword", "#3498db", &Toolbox::showKeywordSearchRequested);
    addTool("color_picker", "颜色提取器", "paint_bucket", "#ff6b81", &Toolbox::showColorPickerRequested);
    addTool("pixel_ruler", "标尺", "pixel_ruler", "#e67e22", &Toolbox::showPixelRulerRequested);
    addTool("immediate_color_picker", "选取颜色", "screen_picker", "#ff4757", &Toolbox::startColorPickerRequested);
    addTool("screenshot", "截图", "camera", "#e74c3c", &Toolbox::screenshotRequested);
    addTool("main_window", "主界面", "maximize", "#4FACFE", &Toolbox::showMainWindowRequested);
    addTool("quick_window", "快速笔记", "zap", "#F1C40F", &Toolbox::showQuickWindowRequested);
    addTool("todo", "待办事项", "todo", "#2ecc71", &Toolbox::showTodoCalendarRequested);
    addTool("alarm", "闹钟提醒", "bell", "#f1c40f", &Toolbox::showAlarmRequested);

    m_btnRotate = createToolButton("切换布局", "rotate", "#aaaaaa");
    connect(m_btnRotate, &QPushButton::clicked, this, &Toolbox::toggleOrientation);

    m_btnMenu = createToolButton("配置按钮", "menu_dots", "#aaaaaa");
    connect(m_btnMenu, &QPushButton::clicked, this, &Toolbox::showConfigPanel);
}

void Toolbox::updateLayout(Orientation orientation) {
    m_orientation = orientation;
    
    // 获取控制按钮 (使用基类成员)
    auto* btnPin = m_btnPin;
    auto* minBtn = m_minBtn; // 在工具箱中作为“移动”手柄
    auto* closeBtn = m_closeBtn;

    // 根据方向设置菜单图标（垂直模式下旋转90度变为横向三点）
    if (m_btnMenu) {
        m_btnMenu->setIcon(IconHelper::getIcon("menu_dots", "#aaaaaa"));
        if (orientation == Orientation::Vertical) {
            QPixmap pix = m_btnMenu->icon().pixmap(32, 32);
            QTransform trans;
            trans.rotate(90);
            m_btnMenu->setIcon(QIcon(pix.transformed(trans, Qt::SmoothTransformation)));
        }
    }

    // 寻找标题栏 widget
    QWidget* titleBar = nullptr;
    if (m_mainLayout->count() > 0) {
        titleBar = m_mainLayout->itemAt(0)->widget();
    }
    if (!titleBar) return;

    // 彻底重置标题栏布局与尺寸限制，防止横纵切换冲突导致的 squashed 状态
    titleBar->setMinimumSize(0, 0);
    titleBar->setMaximumSize(16777215, 16777215);
    
    // 移除基类默认的 10px 底部边距，确保尺寸严格受控
    m_mainLayout->setContentsMargins(0, 0, 0, 0);

    if (titleBar->layout()) {
        QLayoutItem* item;
        while ((item = titleBar->layout()->takeAt(0)) != nullptr) {
            // 不删除 widget，只移除
        }
        delete titleBar->layout();
    }

    // 统一隐藏内容区，所有按钮都放在标题栏内以便在纵向时能正确拉伸且顺序一致
    m_contentArea->hide();

    int visibleCount = 0;
    for (const auto& info : m_toolInfos) if (info.visible) visibleCount++;

    if (orientation == Orientation::Horizontal) {
        titleBar->setFixedHeight(42);
        titleBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        titleBar->setStyleSheet("background-color: transparent; border: none;");
        auto* layout = new QHBoxLayout(titleBar);
        layout->setContentsMargins(8, 0, 8, 0);
        layout->setSpacing(2); // 紧凑间距
        
        // 1. 功能按钮
        for (auto& info : m_toolInfos) {
            if (info.visible) {
                layout->addWidget(info.btn, 0, Qt::AlignVCenter);
                info.btn->show();
            } else {
                info.btn->hide();
            }
        }
        // 2. 旋转与配置按钮
        layout->addWidget(m_btnRotate, 0, Qt::AlignVCenter);
        layout->addWidget(m_btnMenu, 0, Qt::AlignVCenter);
        
        // 4. 系统控制按钮 (统一间距，移除 Stretch)
        if (minBtn) layout->addWidget(minBtn, 0, Qt::AlignVCenter);
        if (closeBtn) layout->addWidget(closeBtn, 0, Qt::AlignVCenter);

        // 确保 m_mainLayout 正确分配空间
        m_mainLayout->setStretchFactor(titleBar, 0);
    } else {
        titleBar->setFixedWidth(42);
        titleBar->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        titleBar->setStyleSheet("background-color: transparent; border: none;");
        auto* layout = new QVBoxLayout(titleBar);
        layout->setContentsMargins(0, 8, 0, 8);
        layout->setSpacing(2); // 紧凑间距
        layout->setAlignment(Qt::AlignHCenter);

        // 垂直模式下，顺序完全反转：系统按钮在最上方
        if (closeBtn) layout->addWidget(closeBtn, 0, Qt::AlignHCenter);
        if (minBtn) layout->addWidget(minBtn, 0, Qt::AlignHCenter);
        // 置顶按钮在垂直模式也隐藏

        // 旋转与配置按钮 (反转顺序，移除 Stretch 实现统一间距)
        layout->addWidget(m_btnMenu, 0, Qt::AlignHCenter);
        layout->addWidget(m_btnRotate, 0, Qt::AlignHCenter);

        // 功能工具按钮 (反转顺序)
        for (int i = m_toolInfos.size() - 1; i >= 0; --i) {
            auto& info = m_toolInfos[i];
            if (info.visible) {
                layout->addWidget(info.btn, 0, Qt::AlignHCenter);
                info.btn->show();
            } else {
                info.btn->hide();
            }
        }

        // 在纵向模式下，让 titleBar 填满整个布局
        m_mainLayout->setStretchFactor(titleBar, 1);
    }

    // 强制触发布局计算与尺寸同步，确保 sizeHint 有效且不触发 Windows 渲染报错
    titleBar->updateGeometry();
    m_mainLayout->activate();
    
    setMinimumSize(0, 0);
    setMaximumSize(16777215, 16777215);

    // 先通过 adjustSize 让窗口系统同步布局，再锁定固定尺寸，防止 UpdateLayeredWindowIndirect 报错
    adjustSize();
    setFixedSize(sizeHint());
    update();
}

void Toolbox::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_pressPos = event->globalPosition().toPoint();
        m_dragOffset = event->globalPosition().toPoint() - frameGeometry().topLeft();
        m_isDragging = false;
        event->accept();
    }
    FramelessDialog::mousePressEvent(event);
}

void Toolbox::contextMenuEvent(QContextMenuEvent* event) {
    // 拦截右键菜单事件，防止在工具箱上点击右键时弹出系统菜单
    event->accept();
}

void Toolbox::mouseMoveEvent(QMouseEvent* event) {
    if (event->buttons() & Qt::LeftButton) {
        if (!m_isDragging) {
            if ((event->globalPosition().toPoint() - m_pressPos).manhattanLength() > QApplication::startDragDistance()) {
                m_isDragging = true;
            }
        }
        
        if (m_isDragging) {
            move(event->globalPosition().toPoint() - m_dragOffset);
            event->accept();
            return;
        }
    }
}

void Toolbox::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        if (m_isDragging) {
            m_isDragging = false;
            checkSnapping();
            saveSettings();
            event->accept();
            return;
        }
    } else if (event->button() == Qt::RightButton) {
        // [BLOCK] 吞掉右键释放，确保右键操作在工具箱内完全无感
        event->accept();
        return;
    }
    checkSnapping();
    saveSettings();
}

void Toolbox::moveEvent(QMoveEvent* event) {
    FramelessDialog::moveEvent(event);
    // 移除 moveEvent 中的 saveSettings 以消除拖拽卡顿
}

bool Toolbox::eventFilter(QObject* watched, QEvent* event) {
    // [ULTIMATE BLOCK] 拦截所有 ContextMenu 事件，确保右键不会触发任何系统或第三方菜单
    if (event->type() == QEvent::ContextMenu) {
        event->accept();
        return true;
    }

    // 处理所有按钮的拖拽重定向
    QPushButton* btn = qobject_cast<QPushButton*>(watched);
    if (btn) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                m_pressPos = me->globalPosition().toPoint();
                m_dragOffset = me->globalPosition().toPoint() - frameGeometry().topLeft();
                m_isDragging = false;
                // 不拦截 Press，允许按钮显示按下效果
            } else if (me->button() == Qt::RightButton) {
                return true; // 拦截右键按下，防止触发潜在行为
            }
        } else if (event->type() == QEvent::MouseMove) {
            auto* me = static_cast<QMouseEvent*>(event);
            if (me->buttons() & Qt::LeftButton) {
                if (!m_isDragging) {
                    if ((me->globalPosition().toPoint() - m_pressPos).manhattanLength() > QApplication::startDragDistance()) {
                        m_isDragging = true;
                    }
                }
                if (m_isDragging) {
                    // [FIX] 一旦开始拖拽，立即强制取消按钮的按下效果，防止高亮残留
                    btn->setDown(false);
                    move(me->globalPosition().toPoint() - m_dragOffset);
                    return true; // 拦截 Move，防止按钮处理
                }
            }
        } else if (event->type() == QEvent::MouseButtonRelease) {
            auto* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                if (m_isDragging) {
                    m_isDragging = false;
                    // [FIX] 释放时同样确保按钮状态重置
                    btn->setDown(false);
                    checkSnapping();
                    saveSettings();
                    return true; // 拦截 Release，防止触发按钮点击信号
                }
                // 对于移动手柄，即使没拖动也要拦截 Release 防止误触逻辑（如果基类有的话）
                if (watched == m_minBtn) return true;
            } else if (me->button() == Qt::RightButton) {
                return true; // 拦截右键释放
            }
        }
    }
    
    if (event->type() == QEvent::HoverEnter) {
        QString text = watched->property("tooltipText").toString();
        if (!text.isEmpty()) {
            ToolTipOverlay::instance()->showText(QCursor::pos(), text);
            // Don't return true, let buttons process hover for style updates
        }
    } else if (event->type() == QEvent::HoverLeave) {
        ToolTipOverlay::hideTip();
    }
    
    return FramelessDialog::eventFilter(watched, event);
}

void Toolbox::checkSnapping() {
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) return;

    QRect screenGeom = screen->availableGeometry();
    QRect winGeom = frameGeometry();
    const int threshold = 40;

    int targetX = winGeom.x();
    int targetY = winGeom.y();
    Orientation newOrientation = m_orientation;

    bool snapped = false;

    // 考虑 FramelessDialog 的 15px 外部边距 (用于阴影)
    const int margin = 15;

    // 检查左右边缘
    if (winGeom.left() + margin - screenGeom.left() < threshold) {
        targetX = screenGeom.left() - margin;
        newOrientation = Orientation::Vertical;
        snapped = true;
    } else if (screenGeom.right() - (winGeom.right() - margin) < threshold) {
        targetX = screenGeom.right() - winGeom.width() + margin;
        newOrientation = Orientation::Vertical;
        snapped = true;
    }

    // 检查上下边缘
    if (winGeom.top() + margin - screenGeom.top() < threshold) {
        targetY = screenGeom.top() - margin;
        if (!snapped) newOrientation = Orientation::Horizontal;
        snapped = true;
    } else if (screenGeom.bottom() - (winGeom.bottom() - margin) < threshold) {
        targetY = screenGeom.bottom() - winGeom.height() + margin;
        if (!snapped) newOrientation = Orientation::Horizontal;
        snapped = true;
    }

    if (snapped) {
        if (newOrientation != m_orientation) {
            updateLayout(newOrientation);
            adjustSize(); // 确保获取更新布局后的最新尺寸
            // 切换布局后再次校验边界，防止超出屏幕 (针对 Requirement 4)
            QRect newWinGeom = frameGeometry();
            if (targetX + newWinGeom.width() - margin > screenGeom.right()) {
                targetX = screenGeom.right() - newWinGeom.width() + margin;
            }
            if (targetY + newWinGeom.height() - margin > screenGeom.bottom()) {
                targetY = screenGeom.bottom() - newWinGeom.height() + margin;
            }
        }
        move(targetX, targetY);
        saveSettings(); // 吸附后显式保存，确保位置被记录
    }
}

void Toolbox::toggleOrientation() {
    Orientation next = (m_orientation == Orientation::Horizontal) ? Orientation::Vertical : Orientation::Horizontal;
    updateLayout(next);
    // 旋转后立即触发吸附与边界检测，防止因高度/宽度增加而溢出屏幕
    checkSnapping();
    saveSettings();
}

void Toolbox::showConfigPanel() {
    auto* panel = new QDialog(this, Qt::Popup | Qt::FramelessWindowHint);
    panel->setAttribute(Qt::WA_TranslucentBackground, true);
    
    auto* mainLayout = new QVBoxLayout(panel);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    // 引入背景容器 QFrame，彻底解决圆角处直角溢出的问题
    auto* bgFrame = new QFrame(panel);
    bgFrame->setObjectName("ConfigBgFrame");
    bgFrame->setAttribute(Qt::WA_StyledBackground, true);
    
    // 移除 500 像素硬编码宽度，改回自适应内容宽度
    panel->setMinimumWidth(150);

    bgFrame->setStyleSheet(
        "#ConfigBgFrame { background-color: #252526; border: 1px solid #444; border-radius: 10px; }"
        "QLabel { color: #888; border: none; font-size: 11px; font-weight: bold; padding: 2px 5px; background: transparent; }"
        "QCheckBox { background-color: #333336; color: #bbb; border: 1px solid #444; font-size: 11px; padding: 4px 15px; margin: 2px 0px; border-radius: 12px; spacing: 8px; }"
        "QCheckBox:hover { background-color: #404044; color: #fff; border-color: #555; }"
        "QCheckBox::indicator { width: 0px; height: 0px; } " // 胶囊样式下隐藏复选框勾选图标
        "QCheckBox:checked { background-color: rgba(0, 122, 204, 0.3); color: #fff; font-weight: bold; border-color: #007ACC; }"
        "QCheckBox:checked:hover { background-color: rgba(0, 122, 204, 0.4); border-color: #0098FF; }"
    );

    auto* contentLayout = new QVBoxLayout(bgFrame);
    contentLayout->setContentsMargins(12, 12, 12, 12);
    contentLayout->setSpacing(6);

    mainLayout->addWidget(bgFrame);

    auto* titleLabel = new QLabel("显示/隐藏功能按钮");
    contentLayout->addWidget(titleLabel);

    for (int i = 0; i < m_toolInfos.size(); ++i) {
        auto* cb = new QCheckBox(m_toolInfos[i].tip);
        cb->setIcon(IconHelper::getIcon(m_toolInfos[i].icon, m_toolInfos[i].color));
        cb->setIconSize(QSize(18, 18));
        cb->setCursor(Qt::PointingHandCursor);
        cb->setChecked(m_toolInfos[i].visible);
        connect(cb, &QCheckBox::toggled, this, [this, i](bool checked) {
            m_toolInfos[i].visible = checked;
            saveSettings();
            updateLayout(m_orientation);
        });
        contentLayout->addWidget(cb);
    }

    panel->adjustSize();

    QPoint pos = m_btnMenu->mapToGlobal(QPoint(0, 0));
    
    // 获取当前屏幕可用区域，确保不超出边界
    QScreen *screen = QGuiApplication::primaryScreen();
    if (this->window() && this->window()->windowHandle()) {
        screen = this->window()->windowHandle()->screen();
    }
    QRect screenGeom = screen ? screen->availableGeometry() : QRect(0, 0, 1920, 1080);

    int x = pos.x();
    int y = pos.y();

    if (m_orientation == Orientation::Horizontal) {
        // 优先向上弹出
        y = pos.y() - panel->height() - 5;
        if (y < screenGeom.top()) {
            // 空间不足则向下弹出
            y = pos.y() + m_btnMenu->height() + 5;
        }
        // 水平修正，保持在按钮附近
        if (x + panel->width() > screenGeom.right()) {
            x = screenGeom.right() - panel->width() - 5;
        }
    } else {
        // 纵向模式下，向左弹出
        x = pos.x() - panel->width() - 5;
        if (x < screenGeom.left()) {
            // 空间不足则向右弹出
            x = pos.x() + m_btnMenu->width() + 5;
        }
        // 垂直修正
        if (y + panel->height() > screenGeom.bottom()) {
            y = screenGeom.bottom() - panel->height() - 5;
        }
    }

    panel->move(x, y);
    panel->show();
}

void Toolbox::loadSettings() {
    QSettings settings("RapidNotes", "Toolbox");
    m_orientation = (Orientation)settings.value("orientation", (int)Orientation::Vertical).toInt();
    
    if (settings.value("isOpen", false).toBool()) {
        show();
    }

    // 恢复位置
    if (settings.contains("pos")) {
        move(settings.value("pos").toPoint());
    } else {
        // 首次运行：默认停靠在屏幕右侧
        QScreen *screen = QGuiApplication::primaryScreen();
        if (screen) {
            QRect geom = screen->availableGeometry();
            move(geom.right() - 50, geom.center().y() - 150);
        }
    }

    for (auto& info : m_toolInfos) {
        info.visible = settings.value("visible_" + info.id, true).toBool();
    }
}

void Toolbox::saveSettings() {
    QSettings settings("RapidNotes", "Toolbox");
    settings.setValue("orientation", (int)m_orientation);
    settings.setValue("isOpen", isVisible());
    
    // 记录最后一次有效位置
    if (isVisible() && !isMinimized()) {
        settings.setValue("pos", pos());
    }
    
    for (const auto& info : m_toolInfos) {
        settings.setValue("visible_" + info.id, info.visible);
    }
}

QPushButton* Toolbox::createToolButton(const QString& tooltip, const QString& iconName, const QString& color) {
    auto* btn = new QPushButton();
    // 显式屏蔽子按钮的菜单策略
    btn->setContextMenuPolicy(Qt::NoContextMenu);
    btn->setIcon(IconHelper::getIcon(iconName, color));
    btn->setIconSize(QSize(20, 20));
    btn->setFixedSize(32, 32);
    // 使用简单的 HTML 包装以确保在所有平台上触发 QSS 样式化的富文本渲染
    // btn->setToolTip(tooltip);
    btn->setProperty("tooltipText", tooltip);
    btn->installEventFilter(this);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setFocusPolicy(Qt::NoFocus);
    
    btn->setStyleSheet("QPushButton {"
        "  background-color: transparent;"
        "  border: none;"
        "  border-radius: 6px;"
        "}"
        "QPushButton:hover {"
        "  background-color: rgba(255, 255, 255, 0.08);"
        "}"
        "QPushButton:pressed {"
        "  background-color: rgba(255, 255, 255, 0.15);"
        "}"
    );
    
    return btn;
}
