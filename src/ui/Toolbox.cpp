#include "Toolbox.h"
#include "ToolTipOverlay.h"
#include "IconHelper.h"
#include "StringUtils.h"
#include "../core/DatabaseManager.h"
#include "../core/FileStorageHelper.h"
#include <QVBoxLayout>
#include <QBuffer>
#include <QMimeData>
#include <QDateTime>
#include <utility>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QApplication>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QSettings>
#include <QCheckBox>
#include <QDialog>
#include <QWindow>
#include <QTransform>
#include <QMenu>
#include <QClipboard>
#include <QRegularExpression>

Toolbox::Toolbox(QWidget* parent) : FramelessDialog("工具箱", parent) {
    setObjectName("ToolboxLauncher");
    setAcceptDrops(true);
    
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

    // 2026-04-xx 按照用户要求：关闭按钮常驻红底白字
    if (m_closeBtn) {
        m_closeBtn->setIcon(IconHelper::getIcon("close", "#FFFFFF"));
        // 2026-04-xx 按照用户要求：关闭按钮悬停锁定红色系
        m_closeBtn->setStyleSheet("QPushButton { background-color: #E81123; border: none; border-radius: 4px; } "
                                 "QPushButton:hover { background-color: #D71520; }");
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
    emit visibilityChanged(true); // 2026-03-22 [NEW] 同步状态
}

void Toolbox::hideEvent(QHideEvent* event) {
    saveSettings();
    FramelessDialog::hideEvent(event);
    emit visibilityChanged(false); // 2026-03-22 [NEW] 同步状态
}

void Toolbox::initUI() {
    // 隐藏默认标题文字，因为我们要把图标放上去
    m_titleLabel->hide();

    // 置顶和最大化按钮在工具箱中永久隐藏
    if (m_btnPin) m_btnPin->hide();
    if (m_maxBtn) m_maxBtn->hide();

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
                             "QPushButton:hover { background-color: #3e3e42; }"); // 2026-03-xx 统一悬停色
        
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

    // [USER_REQUEST] 调整工具箱按钮顺序：将 ①批量识别 和 ②截图取文 迁移到 ③截图 (红相机) 按钮左侧。
    addTool("time", "时间输出", "clock", "#1abc9c", &Toolbox::showTimePasteRequested);
    addTool("password", "密码生成器", "password_generator", "#3498db", &Toolbox::showPasswordGeneratorRequested);
    addTool("tag", "标签管理", "tag", "#f1c40f", &Toolbox::showTagManagerRequested);
    addTool("file_search", "查找文件", "search", "#95a5a6", &Toolbox::showFileSearchRequested);
    addTool("keyword_search", "查找关键字", "find_keyword", "#3498db", &Toolbox::showKeywordSearchRequested);
    addTool("color_picker", "颜色提取器", "paint_bucket", "#ff6b81", &Toolbox::showColorPickerRequested);
    addTool("pixel_ruler", "标尺", "pixel_ruler", "#e67e22", &Toolbox::showPixelRulerRequested);
    addTool("immediate_color_picker", "选取颜色", "screen_picker", "#ff4757", &Toolbox::startColorPickerRequested);
    addTool("ocr", "批量识别", "text", "#4a90e2", &Toolbox::showOCRRequested);
    addTool("immediate_ocr", "截图取文", "screenshot_ocr", "#3498db", &Toolbox::startOCRRequested);
    addTool("screenshot", "截图", "camera", "#e74c3c", &Toolbox::screenshotRequested);

    // [ARCH-RECONSTRUCT] 规范：后期新增功能按钮必须统一添加在此处（即“待办事项”按钮的左侧），以保持 UI 逻辑一致性
    // 自动归档开关
    ToolInfo autoCatInfo;
    autoCatInfo.id = "auto_categorize";
    autoCatInfo.callback = [this]() {
        auto& db = DatabaseManager::instance();
        bool newState = !db.isAutoCategorizeEnabled();
        db.setAutoCategorizeEnabled(newState);
        if (newState) {
            int catId = db.extensionTargetCategoryId();
            QString catName = (catId > 0) ? db.getCategoryNameById(catId) : "未分类";
            ToolTipOverlay::instance()->showText(QCursor::pos(), QString("<b style='color: #00A650;'>[OK] 自动归档已开启 （%1）</b>").arg(catName));
        } else {
            ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #aaaaaa;'>[OFF] 自动归档已关闭</b>");
        }
    };
    autoCatInfo.btn = createToolButton("", "", ""); // 初始化空，由 updateAutoCategorizeButton 填充
    autoCatInfo.btn->setCheckable(true);
    connect(autoCatInfo.btn, &QPushButton::clicked, this, autoCatInfo.callback);
    m_toolInfos.append(autoCatInfo);

    updateAutoCategorizeButton();

    connect(&DatabaseManager::instance(), &DatabaseManager::autoCategorizeEnabledChanged, this, &Toolbox::updateAutoCategorizeButton);
    connect(&DatabaseManager::instance(), &DatabaseManager::extensionTargetCategoryIdChanged, this, &Toolbox::updateAutoCategorizeButton);
    connect(&DatabaseManager::instance(), &DatabaseManager::categoriesChanged, this, &Toolbox::updateAutoCategorizeButton);

    addTool("todo", "待办事项", "todo", "#2ecc71", &Toolbox::showTodoCalendarRequested);
    addTool("alarm", "闹钟提醒", "bell", "#f1c40f", &Toolbox::showAlarmRequested);
    // 2026-03-xx 按照用户要求，统一采用 home 图标打开主界面
    addTool("main_window", "主界面", "home", "#4FACFE", &Toolbox::showMainWindowRequested);
    addTool("quick_window", "快速笔记", "zap", "#F1C40F", &Toolbox::showQuickWindowRequested);

    // 新增“+”按钮 (放置在末尾，确保在垂直布局中出现在最上方)
    ToolInfo addToolInfo;
    addToolInfo.id = "add";
    addToolInfo.tip = "新建数据";
    addToolInfo.icon = "add";
    addToolInfo.color = "#aaaaaa";
    addToolInfo.btn = createToolButton("新建数据", "add", "#aaaaaa");
    addToolInfo.btn->setStyleSheet(addToolInfo.btn->styleSheet() + " QPushButton::menu-indicator { width: 0px; image: none; }");

    QMenu* addMenu = new QMenu(this);
    IconHelper::setupMenu(addMenu);
    addMenu->setStyleSheet("QMenu { background-color: #2D2D2D; color: #EEE; border: 1px solid #444; padding: 4px; } "
                           "QMenu::item { padding: 6px 10px 6px 10px; border-radius: 3px; } "
                           "QMenu::icon { margin-left: 6px; } "
                           "QMenu::item:selected { background-color: #3e3e42; }"); // 2026-03-xx 统一菜单悬停色为 #3e3e42

    addMenu->addAction(IconHelper::getIcon("add", "#aaaaaa", 18), "新建数据", [this](){
        emit newNoteRequested();
    });

    QMenu* createByLineMenu = addMenu->addMenu(IconHelper::getIcon("list_ul", "#aaaaaa", 18), "按行创建数据");
    createByLineMenu->setStyleSheet(addMenu->styleSheet());
    createByLineMenu->addAction("从复制的内容创建", [this](){
        this->doCreateByLine(true);
    });
    createByLineMenu->addAction("从选中数据创建", [this](){
        this->doCreateByLine(false);
    });

    addToolInfo.btn->setMenu(addMenu);
    connect(addToolInfo.btn, &QPushButton::clicked, [addToolInfo](){
        addToolInfo.btn->showMenu();
    });
    m_toolInfos.append(addToolInfo);

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

void Toolbox::keyPressEvent(QKeyEvent* event) {
    // [USER_REQUEST] 当焦点在工具箱时，按下 Ctrl+F 组合键打开“文件查找”界面
    if (event->key() == Qt::Key_F && (event->modifiers() & Qt::ControlModifier)) {
        emit showFileSearchRequested();
        event->accept();
        return;
    }
    FramelessDialog::keyPressEvent(event);
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

void Toolbox::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls() || event->mimeData()->hasText() || event->mimeData()->hasImage()) {
        event->acceptProposedAction();
    }
}

void Toolbox::dragMoveEvent(QDragMoveEvent* event) {
    event->acceptProposedAction();
}

void Toolbox::dropEvent(QDropEvent* event) {
    const QMimeData* mime = event->mimeData();
    int targetId = DatabaseManager::instance().activeCategoryId();

    QString itemType = "text";
    QString title;
    QString content;
    QByteArray dataBlob;
    QStringList tags;

    // 1. 处理本地路径 (与 QuickWindow 逻辑对齐)
    QStringList localPaths = StringUtils::extractLocalPathsFromMime(mime);
    if (!localPaths.isEmpty()) {
        int count = FileStorageHelper::processImport(localPaths, targetId);
        if (count > 0) {
            ToolTipOverlay::instance()->showText(QCursor::pos(), QString("<b style='color:#2ecc71;'>[OK] 已导入 %1 个项目到当前分类</b>").arg(count));
        }
        event->acceptProposedAction();
        return;
    }

    // 2. 处理 URL/文本/图片 (与 QuickWindow 逻辑对齐)
    if (mime->hasUrls()) {
        QList<QUrl> urls = mime->urls();
        QStringList remoteUrls;
        for (const QUrl& url : std::as_const(urls)) {
            if (!url.isLocalFile() && !url.toString().startsWith("file:///")) {
                remoteUrls << url.toString();
            }
        }
        
        if (!remoteUrls.isEmpty()) {
            content = remoteUrls.join(";");
            title = "外部链接";
            itemType = "link";
        }
    } else if (mime->hasText() && !mime->text().trimmed().isEmpty()) {
        content = mime->text();
        title = content.trimmed().left(50).replace("\n", " ");
        itemType = "text";
    } else if (mime->hasImage()) {
        QImage img = qvariant_cast<QImage>(mime->imageData());
        if (!img.isNull()) {
            QBuffer buffer(&dataBlob);
            buffer.open(QIODevice::WriteOnly);
            img.save(&buffer, "PNG");
            itemType = "image";
            title = "[工具箱拖入图片] " + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
            content = "[Image Data]";
        }
    }

    if (!content.isEmpty() || !dataBlob.isEmpty()) {
        DatabaseManager::instance().addNote(title, content, tags, "", targetId, itemType, dataBlob);
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#2ecc71;'>[OK] 已通过工具箱创建新灵感</b>");
        event->acceptProposedAction();
    }
}

bool Toolbox::eventFilter(QObject* watched, QEvent* event) {
    // [MODIFIED] 特殊处理“移动”按钮的右键菜单
    if (watched == m_minBtn && event->type() == QEvent::ContextMenu) {
        auto* ce = static_cast<QContextMenuEvent*>(event);
        showMoveMenu(ce->globalPos());
        return true;
    }

    // [ULTIMATE BLOCK] 拦截所有其它 ContextMenu 事件，确保右键不会触发任何系统或第三方菜单
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
            ToolTipOverlay::instance()->showText(QCursor::pos(), text, 700);
            // Don't return true, let buttons process hover for style updates
        }
    } else if (event->type() == QEvent::HoverLeave) {
        ToolTipOverlay::hideTip();
    }
    
    return FramelessDialog::eventFilter(watched, event);
}

void Toolbox::checkSnapping() {
    QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) screen = QGuiApplication::primaryScreen();
    if (!screen) return;

    QRect screenGeom = screen->availableGeometry();
    QRect winGeom = frameGeometry();
    const int threshold = 40;
    // 2026-03-xx 按照用户要求：修正吸附边距为 12px
    const int margin = 12; // FramelessDialog 阴影外边距

    // 计算到四个边缘的距离（考虑阴影边距后的视觉边缘）
    int distLeft = (winGeom.left() + margin) - screenGeom.left();
    int distRight = screenGeom.right() - (winGeom.right() - margin);
    int distTop = (winGeom.top() + margin) - screenGeom.top();
    int distBottom = screenGeom.bottom() - (winGeom.bottom() - margin);

    // 找到最小距离且在阈值内的边缘
    int minDist = threshold + 1;
    enum Side { None, Left, Right, Top, Bottom } side = None;

    if (distLeft < threshold && distLeft < minDist) { minDist = distLeft; side = Left; }
    if (distRight < threshold && distRight < minDist) { minDist = distRight; side = Right; }
    if (distTop < threshold && distTop < minDist) { minDist = distTop; side = Top; }
    if (distBottom < threshold && distBottom < minDist) { minDist = distBottom; side = Bottom; }

    if (side != None) {
        Orientation targetOrientation = m_orientation;
        if (side == Left || side == Right) {
            targetOrientation = Orientation::Vertical;
        } else {
            targetOrientation = Orientation::Horizontal;
        }

        // 如果需要切换布局，先切换以获取真实的宽高
        if (targetOrientation != m_orientation) {
            updateLayout(targetOrientation);
        }
        
        // [CRITICAL] 必须强制调整尺寸以确保 width() 和 height() 返回新布局下的正确值
        adjustSize();

        // 重新获取当前尺寸（布局已更新）并计算最终坐标
        int curW = width();
        int curH = height();
        int finalX = pos().x();
        int finalY = pos().y();

        if (side == Left) {
            finalX = screenGeom.left() - margin;
        } else if (side == Right) {
            finalX = screenGeom.right() - curW + margin;
        } else if (side == Top) {
            finalY = screenGeom.top() - margin;
        } else if (side == Bottom) {
            finalY = screenGeom.bottom() - curH + margin;
        }

        // 辅助边界校验：防止在切换形态后，另一轴超出屏幕范围
        if (side == Left || side == Right) {
            if (finalY + curH - margin > screenGeom.bottom()) finalY = screenGeom.bottom() - curH + margin;
            if (finalY + margin < screenGeom.top()) finalY = screenGeom.top() - margin;
        } else {
            if (finalX + curW - margin > screenGeom.right()) finalX = screenGeom.right() - curW + margin;
            if (finalX + margin < screenGeom.left()) finalX = screenGeom.left() - margin;
        }

        move(finalX, finalY);
        saveSettings();
    }
}

void Toolbox::updateAutoCategorizeButton() {
    auto& db = DatabaseManager::instance();
    for (auto& info : m_toolInfos) {
        if (info.id == "auto_categorize") {
            bool enabled = db.isAutoCategorizeEnabled();
            info.btn->setChecked(enabled);
            info.btn->setIcon(IconHelper::getIcon(enabled ? "switch_on" : "switch_off", enabled ? "#00A650" : "#aaaaaa"));
            
            if (enabled) {
                // [MODIFIED] 使用 extensionTargetCategoryId (即右键菜单指定的分类) 而不是当前选中的分类
                int catId = db.extensionTargetCategoryId();
                QString catName = (catId > 0) ? db.getCategoryNameById(catId) : "未分类";
                info.tip = QString("自动归档：开启 （%1）").arg(catName);
            } else {
                info.tip = "自动归档：关闭";
            }
            
            info.btn->setProperty("tooltipText", info.tip);
            break;
        }
    }
}

void Toolbox::toggleOrientation() {
    // [UX] 以工具箱中心点为旋转原点
    QPoint oldCenter = frameGeometry().center();

    Orientation next = (m_orientation == Orientation::Horizontal) ? Orientation::Vertical : Orientation::Horizontal;
    updateLayout(next);

    // 重新根据中心点定位
    move(oldCenter.x() - width() / 2, oldCenter.y() - height() / 2);

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
        "QCheckBox:hover { background-color: #3e3e42; color: #fff; border-color: #555; }" // 2026-03-xx 统一悬停色
        "QCheckBox::indicator { width: 0px; height: 0px; } " // 胶囊样式下隐藏复选框勾选图标
        "QCheckBox:checked { background-color: rgba(0, 122, 204, 0.3); color: #fff; font-weight: bold; border-color: #007ACC; }"
        "QCheckBox:checked:hover { background-color: #3e3e42; border-color: #0098FF; }" // 2026-03-xx 统一悬停色
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
    
    // [REMOVED] 构造函数中禁止调用 show()，避免与 WindowManager::toggle 逻辑产生竞态导致首次开启失效

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

void Toolbox::showMoveMenu(const QPoint& globalPos) {
    QMenu menu(this);
    IconHelper::setupMenu(&menu);
    menu.setStyleSheet("QMenu { background-color: #2D2D2D; color: #EEE; border: 1px solid #444; padding: 4px; } "
                       "QMenu::item { padding: 6px 20px 6px 10px; border-radius: 3px; } "
                       "QMenu::item:selected { background-color: #3e3e42; color: white; }"); // 2026-03-xx 统一菜单悬停色为 #3e3e42

    auto add = [&](const QString& text, const QString& icon, std::function<void()> cb) {
        QAction* act = menu.addAction(IconHelper::getIcon(icon, "#aaaaaa"), text);
        connect(act, &QAction::triggered, this, cb);
    };

    QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) screen = QGuiApplication::primaryScreen();
    if (!screen) return;
    QRect screenGeom = screen->availableGeometry();
    // 2026-03-xx 按照用户要求：修正位置菜单边距为 12px
    const int margin = 12; // FramelessDialog 阴影外边距

    add("置于右侧", "align_right", [this, screenGeom, margin]() {
        updateLayout(Orientation::Vertical);
        move(screenGeom.right() - width() + margin, screenGeom.center().y() - height() / 2);
        checkSnapping();
    });
    add("置于左侧", "align_left", [this, screenGeom, margin]() {
        updateLayout(Orientation::Vertical);
        move(screenGeom.left() - margin, screenGeom.center().y() - height() / 2);
        checkSnapping();
    });
    add("置于顶部", "align_top", [this, screenGeom, margin]() {
        updateLayout(Orientation::Horizontal);
        move(screenGeom.center().x() - width() / 2, screenGeom.top() - margin);
        checkSnapping();
    });
    add("置于底部", "align_bottom", [this, screenGeom, margin]() {
        updateLayout(Orientation::Horizontal);
        move(screenGeom.center().x() - width() / 2, screenGeom.bottom() - height() + margin);
        checkSnapping();
    });
    menu.addSeparator();
    add("横向屏幕中心", "align_center_h", [this, screenGeom]() {
        updateLayout(Orientation::Horizontal);
        move(screenGeom.center().x() - width() / 2, screenGeom.center().y() - height() / 2);
        saveSettings();
    });
    add("纵向屏幕中心", "align_center_v", [this, screenGeom]() {
        updateLayout(Orientation::Vertical);
        move(screenGeom.center().x() - width() / 2, screenGeom.center().y() - height() / 2);
        saveSettings();
    });

    menu.exec(globalPos);
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
        "  background-color: #3e3e42;" // 2026-03-xx 统一悬停色
        "}"
        "QPushButton:pressed {"
        "  background-color: rgba(255, 255, 255, 0.15);"
        "}"
    );
    
    return btn;
}

void Toolbox::doCreateByLine(bool fromClipboard) {
    QString text;
    if (fromClipboard) {
        text = QApplication::clipboard()->text();
    } else {
        // 工具箱没有“选中”上下文，这里逻辑上应回退或提示
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #e67e22;'>[!] 工具箱模式仅支持从剪贴板按行创建</b>");
        return;
    }

    if (text.trimmed().isEmpty()) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #e67e22;'>[!] 剪贴板为空</b>");
        return;
    }

    QStringList lines = text.split(QRegularExpression("[\\r\\n]+"), Qt::SkipEmptyParts);
    int catId = DatabaseManager::instance().activeCategoryId();
    
    DatabaseManager::instance().beginBatch();
    int count = 0;
    for (const QString& line : lines) {
        QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) continue;
        
        QString title, content;
        StringUtils::smartSplitLanguage(trimmed, title, content);
        DatabaseManager::instance().addNote(title, content, {}, "", catId);
        count++;
    }
    DatabaseManager::instance().endBatch();
    
    ToolTipOverlay::instance()->showText(QCursor::pos(), QString("<b style='color: #2ecc71;'>[OK] 已成功按行创建 %1 条数据</b>").arg(count));
}
