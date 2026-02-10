#include "QuickWindow.h"
#include "NoteEditWindow.h"
#include "StringUtils.h"
#include "AdvancedTagSelector.h"
#include "IconHelper.h"
#include "QuickNoteDelegate.h"
#include "CategoryDelegate.h"
#include "../core/DatabaseManager.h"
#include "../core/ClipboardMonitor.h"
#include <QGuiApplication>
#include <utility>
#include <QScreen>
#include <QKeyEvent>
#include <QGraphicsDropShadowEffect>
#include <QSettings>
#include <QMenu>
#include <QWindow>
#include <QShortcut>
#include <QKeySequence>
#include <QClipboard>
#include <QMimeData>
#include <QDrag>
#include <QTimer>
#include <QApplication>
#include <QActionGroup>
#include <QAction>
#include <QUrl>
#include <QBuffer>
#include <QToolTip>
#include <QRegularExpression>
#include <QImage>
#include <QMap>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QDesktopServices>
#include <QCoreApplication>
#include <QLineEdit>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QInputDialog>
#include <QColorDialog>
#include <QToolTip>
#include "FramelessDialog.h"
#include "CategoryPasswordDialog.h"
#include "SettingsWindow.h"
#include <QRandomGenerator>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>

#ifdef Q_OS_WIN
#include <windows.h>
#include <windowsx.h>
#endif

// --- AppLockWidget 实现 (Eagle 风格启动锁) ---
class AppLockWidget : public QWidget {
    Q_OBJECT
public:
    AppLockWidget(const QString& correctPassword, QWidget* parent = nullptr)
        : QWidget(parent), m_correctPassword(correctPassword) {
        setObjectName("AppLockWidget");
        setFocusPolicy(Qt::StrongFocus);
        setAttribute(Qt::WA_StyledBackground);
        
        auto* layout = new QVBoxLayout(this);
        layout->setAlignment(Qt::AlignCenter);
        layout->setSpacing(20);

        // 背景色
        setStyleSheet("QWidget#AppLockWidget { background-color: #1C1C1C; border-radius: 10px; } "
                      "QLabel { background: transparent; border: none; }");

        // 1. 锁图标
        auto* lockIcon = new QLabel();
        lockIcon->setPixmap(IconHelper::getIcon("lock_secure", "#aaaaaa").pixmap(64, 64));
        lockIcon->setAlignment(Qt::AlignCenter);
        layout->addWidget(lockIcon);

        // 2. 标题文字
        auto* titleLabel = new QLabel("已锁定");
        titleLabel->setStyleSheet("color: #EEEEEE; font-size: 18px; font-weight: bold;");
        titleLabel->setAlignment(Qt::AlignCenter);
        layout->addWidget(titleLabel);

        // 3. 密码提示文字
        QSettings settings("RapidNotes", "QuickWindow");
        QString hint = settings.value("appPasswordHint", "请输入启动密码").toString();
        auto* hintLabel = new QLabel(hint);
        hintLabel->setStyleSheet("color: #666666; font-size: 12px;");
        hintLabel->setAlignment(Qt::AlignCenter);
        layout->addWidget(hintLabel);

        // 4. 密码输入框
        m_pwdEdit = new QLineEdit();
        m_pwdEdit->setEchoMode(QLineEdit::Password);
        m_pwdEdit->setPlaceholderText("请输入密码");
        m_pwdEdit->setFixedWidth(240);
        m_pwdEdit->setFixedHeight(36);
        m_pwdEdit->setAlignment(Qt::AlignCenter);
        m_pwdEdit->setStyleSheet(
            "QLineEdit {"
            "  background-color: #2A2A2A; border: 1px solid #333; border-radius: 6px;"
            "  color: white; font-size: 14px;"
            "}"
            "QLineEdit:focus { border: 1px solid #3A90FF; }"
        );
        connect(m_pwdEdit, &QLineEdit::returnPressed, this, &AppLockWidget::handleVerify);
        layout->addWidget(m_pwdEdit, 0, Qt::AlignHCenter);

        // 5. 右上角关闭按钮
        m_closeBtn = new QPushButton(this);
        m_closeBtn->setIcon(IconHelper::getIcon("close", "#aaaaaa"));
        m_closeBtn->setIconSize(QSize(18, 18));
        m_closeBtn->setFixedSize(32, 32);
        m_closeBtn->setCursor(Qt::PointingHandCursor);
        m_closeBtn->setStyleSheet(
            "QPushButton { border: none; border-radius: 4px; background: transparent; } "
            "QPushButton:hover { background-color: #E81123; }"
        );
        connect(m_closeBtn, &QPushButton::clicked, []() { QApplication::quit(); });

        // 初始焦点
        m_pwdEdit->setFocus();
    }

    void focusInput() {
        m_pwdEdit->setFocus();
        m_pwdEdit->selectAll();
    }

protected:
    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Escape) {
            QApplication::quit();
        }
        QWidget::keyPressEvent(event);
    }

    void resizeEvent(QResizeEvent* event) override {
        m_closeBtn->move(width() - m_closeBtn->width() - 10, 10);
        QWidget::resizeEvent(event);
    }

private slots:
    void handleVerify() {
        if (m_pwdEdit->text() == m_correctPassword) {
            startFadeOut();
        } else {
            startShake();
        }
    }

    void startFadeOut() {
        auto* opacityEffect = new QGraphicsOpacityEffect(this);
        setGraphicsEffect(opacityEffect);
        auto* animation = new QPropertyAnimation(opacityEffect, "opacity");
        animation->setDuration(300);
        animation->setStartValue(1.0);
        animation->setEndValue(0.0);
        animation->setEasingCurve(QEasingCurve::OutCubic);
        connect(animation, &QPropertyAnimation::finished, this, [this]() {
            emit unlocked();
            this->deleteLater();
        });
        animation->start(QAbstractAnimation::DeleteWhenStopped);
    }

    void startShake() {
        m_pwdEdit->clear();
        auto* anim = new QPropertyAnimation(m_pwdEdit, "pos");
        anim->setDuration(400);
        anim->setLoopCount(1);
        
        QPoint pos = m_pwdEdit->pos();
        anim->setKeyValueAt(0, pos);
        anim->setKeyValueAt(0.1, pos + QPoint(-10, 0));
        anim->setKeyValueAt(0.3, pos + QPoint(10, 0));
        anim->setKeyValueAt(0.5, pos + QPoint(-10, 0));
        anim->setKeyValueAt(0.7, pos + QPoint(10, 0));
        anim->setKeyValueAt(0.9, pos + QPoint(-10, 0));
        anim->setKeyValueAt(1, pos);
        
        anim->start(QAbstractAnimation::DeleteWhenStopped);
    }

signals:
    void unlocked();

private:
    QLineEdit* m_pwdEdit;
    QPushButton* m_closeBtn;
    QString m_correctPassword;
};


// 定义调整大小的边缘触发区域宽度 (与边距一致，改为 12px 以匹配新边距)
#define RESIZE_MARGIN 12

QuickWindow::QuickWindow(QWidget* parent) 
    : QWidget(parent, Qt::FramelessWindowHint) 
{
     setWindowTitle("快速笔记");
    setAcceptDrops(true);
    setAttribute(Qt::WA_TranslucentBackground);
    // [CRITICAL] 强制开启非活动窗口的 ToolTip 显示。
    setAttribute(Qt::WA_AlwaysShowToolTips);
    setAttribute(Qt::WA_DeleteOnClose, false);
    
    // 关键修复：开启鼠标追踪，否则不按住鼠标时无法检测边缘
    setMouseTracking(true);
    setAttribute(Qt::WA_Hover);
    
    initUI();

    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setSingleShot(true);
    m_refreshTimer->setInterval(200);
    connect(m_refreshTimer, &QTimer::timeout, this, [this](){
        if (this->isVisible()) {
            refreshData();
            refreshSidebar();
        }
    });

    connect(&DatabaseManager::instance(), &DatabaseManager::noteAdded, this, &QuickWindow::onNoteAdded);
    connect(&DatabaseManager::instance(), &DatabaseManager::noteUpdated, this, &QuickWindow::scheduleRefresh);
    connect(&ClipboardMonitor::instance(), &ClipboardMonitor::newContentDetected, this, &QuickWindow::scheduleRefresh);

    connect(&DatabaseManager::instance(), &DatabaseManager::categoriesChanged, this, [this](){
        m_model->updateCategoryMap();
        
        // 如果当前正在查看某个分类，同步更新其高亮色
        if (m_currentFilterType == "category" && m_currentFilterValue != -1) {
            auto categories = DatabaseManager::instance().getAllCategories();
            for (const auto& cat : std::as_const(categories)) {
                if (cat.value("id").toInt() == m_currentFilterValue) {
                    m_currentCategoryColor = cat.value("color").toString();
                    if (m_currentCategoryColor.isEmpty()) m_currentCategoryColor = "#4a90e2";
                    applyListTheme(m_currentCategoryColor);
                    break;
                }
            }
        }
        
        scheduleRefresh();
    });

#ifdef Q_OS_WIN
    m_monitorTimer = new QTimer(this);
    connect(m_monitorTimer, &QTimer::timeout, [this]() {
        HWND currentHwnd = GetForegroundWindow();
        if (currentHwnd == 0 || currentHwnd == (HWND)winId()) return;
        if (currentHwnd != m_lastActiveHwnd) {
            m_lastActiveHwnd = currentHwnd;
            m_lastThreadId = GetWindowThreadProcessId(m_lastActiveHwnd, nullptr);
            
            GUITHREADINFO gti;
            gti.cbSize = sizeof(GUITHREADINFO);
            if (GetGUIThreadInfo(m_lastThreadId, &gti)) {
                m_lastFocusHwnd = gti.hwndFocus;
            } else {
                m_lastFocusHwnd = nullptr;
            }
        }
    });
    m_monitorTimer->start(200);
#endif
}

void QuickWindow::initUI() {
    auto* mainLayout = new QVBoxLayout(this);
    // 【修改点1】边距调整为 12px，给窄阴影留出空间防止截断，同时保持紧凑
    mainLayout->setContentsMargins(12, 12, 12, 12); 

    auto* container = new QWidget();
    container->setObjectName("container");
    container->setMouseTracking(true); // 确保容器不阻断鼠标追踪
    container->setStyleSheet(
        "QWidget#container { background: #1E1E1E; border-radius: 10px; border: 1px solid #333; }"
        "QListView, QTreeView { background: transparent; border: none; color: #BBB; outline: none; }"
        "QTreeView::item { height: 22px; padding: 0px 4px; border-radius: 4px; }"
        "QTreeView::item:hover { background-color: #2a2d2e; }"
        "QTreeView::item:selected { background-color: transparent; color: white; }"
        "QListView::item { padding: 6px; border-bottom: 1px solid #2A2A2A; }"
    );
    
    // 【修改点2】阴影参数调整：更窄(BlurRadius 15)且不扩散
    auto* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(15);               // 变窄：15 (原25)
    shadow->setColor(QColor(0, 0, 0, 90));   // 变柔：90 (原100)，略微降低浓度
    shadow->setOffset(0, 2);                 // 变贴：垂直偏移2 (原4)
    container->setGraphicsEffect(shadow);

    auto* containerLayout = new QHBoxLayout(container);
    containerLayout->setContentsMargins(0, 0, 0, 0);
    containerLayout->setSpacing(0);

    // --- 左侧内容区域 ---
    auto* leftContent = new QWidget();
    leftContent->setObjectName("leftContent");
    leftContent->setStyleSheet("QWidget#leftContent { background: #1E1E1E; border-top-left-radius: 10px; border-bottom-left-radius: 10px; }");
    leftContent->setMouseTracking(true);
    auto* leftLayout = new QVBoxLayout(leftContent);
    leftLayout->setContentsMargins(10, 10, 10, 5);
    leftLayout->setSpacing(8);
    
    m_searchEdit = new SearchLineEdit();
    m_searchEdit->setPlaceholderText("搜索灵感 (双击查看历史)");
    m_searchEdit->setClearButtonEnabled(true);
    leftLayout->addWidget(m_searchEdit);

    m_splitter = new QSplitter(Qt::Horizontal);
    m_splitter->setHandleWidth(4);
    m_splitter->setChildrenCollapsible(false);
    
    m_listView = new DittoListView();
    m_listView->setDragEnabled(true);
    m_listView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_listView->setIconSize(QSize(28, 28));
    m_listView->setAlternatingRowColors(true);
    m_listView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_listView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_listView->setMouseTracking(true);
    m_listView->setItemDelegate(new QuickNoteDelegate(this));
    m_model = new NoteModel(this);
    m_listView->setModel(m_model);
    m_listView->setContextMenuPolicy(Qt::CustomContextMenu);

    m_lockWidget = new CategoryLockWidget(this);
    m_lockWidget->setVisible(false);
    connect(m_lockWidget, &CategoryLockWidget::unlocked, this, [this](){
        refreshData();
    });
    connect(m_listView, &QListView::customContextMenuRequested, this, &QuickWindow::showListContextMenu);
    connect(m_listView, &QListView::doubleClicked, this, [this](const QModelIndex& index){
        activateNote(index);
    });

    auto* sidebarContainer = new QWidget();
    auto* sidebarLayout = new QVBoxLayout(sidebarContainer);
    sidebarLayout->setContentsMargins(0, 0, 0, 0);
    sidebarLayout->setSpacing(0);

    QString treeStyle = R"(
        QTreeView {
            background-color: transparent;
            border: none;
            outline: none;
            color: #ccc;
        }
        /* 针对我的分区标题进行加粗白色处理 */
        QTreeView::item:!selectable {
            color: #ffffff;
            font-weight: bold;
        }
        QTreeView::item {
            height: 22px;
            padding: 0px;
            border: none;
            background: transparent;
        }
        QTreeView::item:hover, QTreeView::item:selected {
            background: transparent;
        }
        QTreeView::branch:hover, QTreeView::branch:selected {
            background: transparent;
        }
        QTreeView::branch {
            image: none;
        }
    )";

    m_systemTree = new DropTreeView();
    m_systemTree->setStyleSheet(treeStyle);
    m_systemTree->setItemDelegate(new CategoryDelegate(this));
    m_systemModel = new CategoryModel(CategoryModel::System, this);
    m_systemTree->setModel(m_systemModel);
    m_systemTree->setHeaderHidden(true);
    m_systemTree->setMouseTracking(true);
    m_systemTree->setIndentation(12);
    m_systemTree->setFixedHeight(176); // 8 items * 22px = 176px
    m_systemTree->setEditTriggers(QAbstractItemView::NoEditTriggers); // 绝不可重命名
    m_systemTree->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_systemTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_systemTree, &QTreeView::customContextMenuRequested, this, &QuickWindow::showSidebarMenu);

    m_partitionTree = new DropTreeView();
    m_partitionTree->setStyleSheet(treeStyle);
    m_partitionTree->setItemDelegate(new CategoryDelegate(this));
    m_partitionModel = new CategoryModel(CategoryModel::User, this);
    m_partitionTree->setModel(m_partitionModel);
    m_partitionTree->setHeaderHidden(true);
    m_partitionTree->setMouseTracking(true);
    m_partitionTree->setIndentation(12);
    m_partitionTree->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_partitionTree->setDragEnabled(true);
    m_partitionTree->setAcceptDrops(true);
    m_partitionTree->setDropIndicatorShown(true);
    m_partitionTree->setDragDropMode(QAbstractItemView::InternalMove);
    m_partitionTree->setDefaultDropAction(Qt::MoveAction);
    m_partitionTree->expandAll();
    m_partitionTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_partitionTree, &QTreeView::customContextMenuRequested, this, &QuickWindow::showSidebarMenu);

    sidebarLayout->addWidget(m_systemTree);
    sidebarLayout->addWidget(m_partitionTree);

    // 树形菜单点击逻辑...
    auto onSelectionChanged = [this](DropTreeView* tree, const QModelIndex& index) {
        if (!index.isValid()) return;
        if (tree == m_systemTree) {
            m_partitionTree->selectionModel()->clearSelection();
            m_partitionTree->setCurrentIndex(QModelIndex());
        } else {
            m_systemTree->selectionModel()->clearSelection();
            m_systemTree->setCurrentIndex(QModelIndex());
        }
        m_currentFilterType = index.data(CategoryModel::TypeRole).toString();
        QString name = index.data(CategoryModel::NameRole).toString();
        updatePartitionStatus(name);

        // 统一从模型获取颜色，实现全分区变色联动
        m_currentCategoryColor = index.data(CategoryModel::ColorRole).toString();
        if (m_currentCategoryColor.isEmpty()) m_currentCategoryColor = "#4a90e2";

        if (m_currentFilterType == "category") {
            m_currentFilterValue = index.data(CategoryModel::IdRole).toInt();
            StringUtils::recordRecentCategory(m_currentFilterValue.toInt());
        } else {
            m_currentFilterValue = -1;
        }
        
        applyListTheme(m_currentCategoryColor);
        m_currentPage = 1;
        refreshData();
    };
    connect(m_systemTree, &QTreeView::clicked, this, [this, onSelectionChanged](const QModelIndex& idx){ onSelectionChanged(m_systemTree, idx); });
    connect(m_partitionTree, &QTreeView::clicked, this, [this, onSelectionChanged](const QModelIndex& idx){ onSelectionChanged(m_partitionTree, idx); });

    // 拖拽逻辑...
    auto onNotesDropped = [this](const QList<int>& ids, const QModelIndex& targetIndex) {
        if (!targetIndex.isValid()) return;
        QString type = targetIndex.data(CategoryModel::TypeRole).toString();
        
        if (type == "category") {
            int catId = targetIndex.data(CategoryModel::IdRole).toInt();
            DatabaseManager::instance().moveNotesToCategory(ids, catId);
            StringUtils::recordRecentCategory(catId);
        } else if (type == "uncategorized") {
            DatabaseManager::instance().moveNotesToCategory(ids, -1);
        } else {
            for (int id : ids) {
                if (type == "bookmark") DatabaseManager::instance().updateNoteState(id, "is_favorite", 1);
                else if (type == "trash") DatabaseManager::instance().updateNoteState(id, "is_deleted", 1);
            }
        }
        // refreshData 和 refreshSidebar 将通过 DatabaseManager 信号触发的 scheduleRefresh 异步执行，
        // 从而避免在 dropEvent 堆栈中立即 reset model 导致的潜在闪退。
    };
    connect(m_systemTree, &DropTreeView::notesDropped, this, onNotesDropped);
    connect(m_partitionTree, &DropTreeView::notesDropped, this, onNotesDropped);

    // 右键菜单...
    // (此处省略部分右键菜单代码以保持简洁，逻辑与原版保持一致)
    // 主要是 showSidebarMenu 的实现...

    m_splitter->addWidget(m_listView);
    m_splitter->addWidget(m_lockWidget);
    m_splitter->addWidget(sidebarContainer);
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setStretchFactor(2, 0);
    m_splitter->setSizes({550, 0, 150});
    leftLayout->addWidget(m_splitter);

    applyListTheme(""); // 【核心修复】初始化时即应用深色主题

    // --- 底部状态栏与标签输入框 ---
    auto* bottomLayout = new QHBoxLayout();
    bottomLayout->setContentsMargins(2, 0, 10, 5);
    bottomLayout->setSpacing(10);

    m_statusLabel = new QLabel("当前分区: 全部数据");
    m_statusLabel->setStyleSheet("font-size: 11px; color: #888;");
    m_statusLabel->setFixedHeight(32);
    bottomLayout->addWidget(m_statusLabel);

    m_tagEdit = new ClickableLineEdit();
    m_tagEdit->setPlaceholderText("输入标签添加... (双击显示历史)");
    m_tagEdit->setStyleSheet(
        "QLineEdit { background-color: rgba(255, 255, 255, 0.05); "
        "border: 1px solid rgba(255, 255, 255, 0.1); "
        "border-radius: 10px; "
        "padding: 6px 12px; "
        "font-size: 12px; "
        "color: #EEE; } "
        "QLineEdit:focus { border-color: #4a90e2; background-color: rgba(255, 255, 255, 0.08); } "
        "QLineEdit:disabled { background-color: transparent; border: 1px solid #333; color: #666; }"
    );
    m_tagEdit->setEnabled(false); // 初始禁用
    connect(m_tagEdit, &QLineEdit::returnPressed, this, &QuickWindow::handleTagInput);
    connect(m_tagEdit, &ClickableLineEdit::doubleClicked, this, [this](){
        this->openTagSelector();
    });
    bottomLayout->addWidget(m_tagEdit, 1);

    leftLayout->addLayout(bottomLayout);

    containerLayout->addWidget(leftContent);

    // --- 右侧垂直工具栏 (Custom Toolbar Implementation) ---
    // 【核心修正】根据图二 1:1 还原，压缩宽度，修正图标名，重构分页布局
    
    QWidget* customToolbar = new QWidget(this);
    customToolbar->setFixedWidth(40); // 压缩至 40px
    customToolbar->setStyleSheet(
        "QWidget { background-color: #252526; border-top-right-radius: 10px; border-bottom-right-radius: 10px; border-left: 1px solid #333; }"
        "QPushButton { border: none; border-radius: 4px; background: transparent; padding: 0px; outline: none; }"
        "QPushButton:hover { background-color: #3e3e42; }"
        "QPushButton#btnClose:hover { background-color: #E81123; }"
        "QPushButton:pressed { background-color: #2d2d2d; }"
        "QLabel { color: #888; font-size: 11px; }"
        "QLineEdit { background: transparent; border: 1px solid #444; border-radius: 4px; color: white; font-size: 11px; font-weight: bold; padding: 0; }"
    );
    
    QVBoxLayout* toolLayout = new QVBoxLayout(customToolbar);
    toolLayout->setContentsMargins(4, 8, 4, 8); // 对齐 Python 版边距
    toolLayout->setSpacing(4); // 紧凑间距，匹配图二

    // 辅助函数：创建图标按钮，支持旋转
    auto createToolBtn = [](QString iconName, QString color, QString tooltip, int rotate = 0) {
        QPushButton* btn = new QPushButton();
        QIcon icon = IconHelper::getIcon(iconName, color);
        if (rotate != 0) {
            QPixmap pix = icon.pixmap(32, 32);
            QTransform trans;
            trans.rotate(rotate);
            btn->setIcon(QIcon(pix.transformed(trans, Qt::SmoothTransformation)));
        } else {
            btn->setIcon(icon);
        }
        btn->setIconSize(QSize(20, 20)); // 统一标准化为 20px 图标
        btn->setFixedSize(32, 32);
        btn->setToolTip(StringUtils::wrapToolTip(tooltip));
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFocusPolicy(Qt::NoFocus);
        return btn;
    };

    // 1. 顶部窗口控制区 (修正图标名为 SvgIcons 中存在的名称)
    QPushButton* btnClose = createToolBtn("close", "#aaaaaa", "关闭");
    btnClose->setObjectName("btnClose");
    connect(btnClose, &QPushButton::clicked, this, &QuickWindow::hide);

    QPushButton* btnFull = createToolBtn("maximize", "#aaaaaa", "打开/关闭主窗口");
    connect(btnFull, &QPushButton::clicked, [this](){ emit toggleMainWindowRequested(); });

    QPushButton* btnMin = createToolBtn("minimize", "#aaaaaa", "最小化");
    connect(btnMin, &QPushButton::clicked, this, &QuickWindow::showMinimized);

    toolLayout->addWidget(btnClose, 0, Qt::AlignHCenter);
    toolLayout->addWidget(btnFull, 0, Qt::AlignHCenter);
    toolLayout->addWidget(btnMin, 0, Qt::AlignHCenter);

    toolLayout->addSpacing(8);

    // 2. 功能按钮区
    QPushButton* btnPin = createToolBtn("pin_tilted", "#aaaaaa", "置顶");
    btnPin->setCheckable(true);
    btnPin->setObjectName("btnPin");
    btnPin->setStyleSheet("QPushButton:checked { background-color: #3A90FF; }");
    if (windowFlags() & Qt::WindowStaysOnTopHint) {
        btnPin->setChecked(true);
        btnPin->setIcon(IconHelper::getIcon("pin_vertical", "#ffffff"));
    }
    connect(btnPin, &QPushButton::toggled, this, &QuickWindow::toggleStayOnTop);

    QPushButton* btnSidebar = createToolBtn("eye", "#aaaaaa", "显示/隐藏侧边栏");
    btnSidebar->setObjectName("btnSidebar");
    btnSidebar->setCheckable(true);
    btnSidebar->setChecked(true);
    btnSidebar->setStyleSheet("QPushButton:checked { background-color: #3A90FF; }");
    connect(btnSidebar, &QPushButton::clicked, this, &QuickWindow::toggleSidebar);

    QPushButton* btnRefresh = createToolBtn("refresh", "#aaaaaa", "刷新");
    connect(btnRefresh, &QPushButton::clicked, this, &QuickWindow::refreshData);

    QPushButton* btnToolbox = createToolBtn("toolbox", "#aaaaaa", "工具箱");
    btnToolbox->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(btnToolbox, &QPushButton::clicked, this, &QuickWindow::toolboxRequested);
    connect(btnToolbox, &QPushButton::customContextMenuRequested, this, &QuickWindow::showToolboxMenu);

    QPushButton* btnLock = createToolBtn("lock_secure", "#aaaaaa", "锁定应用");
    connect(btnLock, &QPushButton::clicked, this, &QuickWindow::doGlobalLock);

    toolLayout->addWidget(btnPin, 0, Qt::AlignHCenter);
    toolLayout->addWidget(btnSidebar, 0, Qt::AlignHCenter);
    toolLayout->addWidget(btnRefresh, 0, Qt::AlignHCenter);
    toolLayout->addWidget(btnToolbox, 0, Qt::AlignHCenter);
    toolLayout->addWidget(btnLock, 0, Qt::AlignHCenter);

    toolLayout->addStretch();

    // 3. 分页区 (完全复刻图二布局：箭头+输入框+下方总数)
    QPushButton* btnPrev = createToolBtn("nav_prev", "#aaaaaa", "上一页", 90);
    btnPrev->setFixedSize(32, 20);
    connect(btnPrev, &QPushButton::clicked, [this](){
        if (m_currentPage > 1) { m_currentPage--; refreshData(); }
    });

    QLineEdit* pageInput = new QLineEdit("1");
    pageInput->setObjectName("pageInput");
    pageInput->setAlignment(Qt::AlignCenter);
    pageInput->setFixedSize(28, 20);
    connect(pageInput, &QLineEdit::returnPressed, [this, pageInput](){
        int p = pageInput->text().toInt();
        if (p > 0 && p <= m_totalPages) { m_currentPage = p; refreshData(); }
    });

    QLabel* totalLabel = new QLabel("1");
    totalLabel->setObjectName("totalLabel");
    totalLabel->setAlignment(Qt::AlignCenter);
    totalLabel->setStyleSheet("color: #666; font-size: 10px; border: none; background: transparent;");

    QPushButton* btnNext = createToolBtn("nav_next", "#aaaaaa", "下一页", 90);
    btnNext->setFixedSize(32, 20);
    connect(btnNext, &QPushButton::clicked, [this](){
        if (m_currentPage < m_totalPages) { m_currentPage++; refreshData(); }
    });

    toolLayout->addWidget(btnPrev, 0, Qt::AlignHCenter);
    toolLayout->addWidget(pageInput, 0, Qt::AlignHCenter);
    toolLayout->addWidget(totalLabel, 0, Qt::AlignHCenter);
    toolLayout->addWidget(btnNext, 0, Qt::AlignHCenter);

    toolLayout->addSpacing(20); // 增加分页与标题间距

    // 4. 垂直标题 "快速笔记"
    QLabel* verticalTitle = new QLabel("快\n速\n笔\n记");
    verticalTitle->setAlignment(Qt::AlignCenter);
    verticalTitle->setStyleSheet("color: #444; font-size: 11px; font-weight: bold; border: none; background: transparent; line-height: 1.1;");
    toolLayout->addWidget(verticalTitle, 0, Qt::AlignHCenter);

    toolLayout->addSpacing(12);

    // 5. 底部 Logo (修正为 zap 图标以匹配图二蓝闪电)
    QPushButton* btnLogo = createToolBtn("zap", "#3A90FF", "RapidNotes");
    btnLogo->setCursor(Qt::ArrowCursor);
    btnLogo->setStyleSheet("background: transparent; border: none;");
    toolLayout->addWidget(btnLogo, 0, Qt::AlignHCenter);

    containerLayout->addWidget(customToolbar);
    
    // m_toolbar = new QuickToolbar(this); // 移除旧代码
    // containerLayout->addWidget(m_toolbar); // 移除旧代码
    
    mainLayout->addWidget(container);
    
    // 初始大小和最小大小
    resize(900, 630);
    setMinimumSize(400, 300);

    m_quickPreview = new QuickPreview(this);
    connect(m_quickPreview, &QuickPreview::editRequested, this, &QuickWindow::doEditNote);
    connect(m_quickPreview, &QuickPreview::prevRequested, this, [this](){
        QModelIndex current = m_listView->currentIndex();
        if (!current.isValid() || m_model->rowCount() == 0) return;

        int row = current.row();
        int nextRow = row - 1;
        if (nextRow < 0) {
            nextRow = m_model->rowCount() - 1; // 循环至末尾
            QToolTip::showText(QCursor::pos(), StringUtils::wrapToolTip("已回环至列表末尾"));
        }
        
        QModelIndex nextIdx = m_model->index(nextRow, 0);
        m_listView->setCurrentIndex(nextIdx);
        m_listView->scrollTo(nextIdx);
        updatePreviewContent();
    });
    connect(m_quickPreview, &QuickPreview::nextRequested, this, [this](){
        QModelIndex current = m_listView->currentIndex();
        if (!current.isValid() || m_model->rowCount() == 0) return;

        int row = current.row();
        int nextRow = row + 1;
        if (nextRow >= m_model->rowCount()) {
            nextRow = 0; // 循环至开头
            QToolTip::showText(QCursor::pos(), StringUtils::wrapToolTip("已回环至列表起始"));
        }

        QModelIndex nextIdx = m_model->index(nextRow, 0);
        m_listView->setCurrentIndex(nextIdx);
        m_listView->scrollTo(nextIdx);
        updatePreviewContent();
    });
    m_listView->installEventFilter(this);
    m_systemTree->installEventFilter(this);
    m_partitionTree->installEventFilter(this);

    // 搜索逻辑
    m_searchTimer = new QTimer(this);
    m_searchTimer->setSingleShot(true);
    connect(m_searchTimer, &QTimer::timeout, this, &QuickWindow::refreshData);
    connect(m_searchEdit, &QLineEdit::textChanged, [this](const QString& text){
        m_currentPage = 1;
        m_searchTimer->start(300);
    });

    connect(m_searchEdit, &QLineEdit::returnPressed, [this](){
        QString text = m_searchEdit->text().trimmed();
        if (text.isEmpty()) return;
        m_searchEdit->addHistoryEntry(text);
        
        // 强制立即刷新一次数据，防止定时器延迟导致 rowCount 不准确
        m_searchTimer->stop();
        refreshData();
    });

    // 监听列表选择变化，动态切换输入框状态
    connect(m_listView->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this](){
        auto selected = m_listView->selectionModel()->selectedIndexes();
        if (selected.isEmpty()) {
            m_tagEdit->setEnabled(false);
            m_tagEdit->clear();
            m_tagEdit->setPlaceholderText("请先选择一个项目");
        } else {
            m_tagEdit->setEnabled(true);
            m_tagEdit->setPlaceholderText(selected.size() == 1 ? "输入新标签... (双击显示历史)" : "批量添加标签... (双击显示历史)");
            
            // 置顶联动：如果预览窗口处于置顶显示状态，随选中项即时更新内容
            if (m_quickPreview->isVisible() && m_quickPreview->isPinned()) {
                updatePreviewContent();
            }
        }
    });

    setupShortcuts();
    restoreState();
    refreshData();
    setupAppLock();
}

void QuickWindow::setupAppLock() {
    if (m_appLockWidget) return;
    QSettings settings("RapidNotes", "QuickWindow");
    QString appPwd = settings.value("appPassword").toString();
    if (!appPwd.isEmpty()) {
        auto* lock = new AppLockWidget(appPwd, this);
        m_appLockWidget = lock;
        lock->resize(this->size());
        
        connect(lock, &AppLockWidget::unlocked, this, [this]() {
            m_appLockWidget = nullptr;
            m_searchEdit->setFocus();
        });
        
        lock->show();
        lock->raise();
    }
}

void QuickWindow::saveState() {
    QSettings settings("RapidNotes", "QuickWindow");
    settings.setValue("geometry", saveGeometry());
    settings.setValue("splitter", m_splitter->saveState());
    settings.setValue("sidebarHidden", m_systemTree->parentWidget()->isHidden());
    settings.setValue("stayOnTop", m_isStayOnTop);
    settings.setValue("autoCategorizeClipboard", m_autoCategorizeClipboard);
}

void QuickWindow::restoreState() {
    QSettings settings("RapidNotes", "QuickWindow");
    if (settings.contains("geometry")) {
        restoreGeometry(settings.value("geometry").toByteArray());
    }
    if (settings.contains("splitter")) {
        m_splitter->restoreState(settings.value("splitter").toByteArray());
    }
    if (settings.contains("sidebarHidden")) {
        bool hidden = settings.value("sidebarHidden").toBool();
        m_systemTree->parentWidget()->setHidden(hidden);
        
        // 同步刷新眼睛图标状态
        auto* btnSidebar = findChild<QPushButton*>("btnSidebar");
        if (btnSidebar) {
            bool visible = !hidden;
            btnSidebar->setChecked(visible);
            btnSidebar->setIcon(IconHelper::getIcon("eye", visible ? "#ffffff" : "#aaaaaa"));
        }
    }
    if (settings.contains("stayOnTop")) {
        toggleStayOnTop(settings.value("stayOnTop").toBool());
    }
    if (settings.contains("autoCategorizeClipboard")) {
        m_autoCategorizeClipboard = settings.value("autoCategorizeClipboard").toBool();
    }
}

void QuickWindow::setupShortcuts() {
    new QShortcut(QKeySequence("Ctrl+F"), this, [this](){ m_searchEdit->setFocus(); m_searchEdit->selectAll(); });
    new QShortcut(QKeySequence("Delete"), this, [this](){ doDeleteSelected(false); });
    new QShortcut(QKeySequence("Shift+Delete"), this, [this](){ doDeleteSelected(true); });
    new QShortcut(QKeySequence("Ctrl+E"), this, [this](){ doToggleFavorite(); });
    new QShortcut(QKeySequence("Ctrl+P"), this, [this](){ doTogglePin(); });
    new QShortcut(QKeySequence("Ctrl+W"), this, [this](){ hide(); });
    new QShortcut(QKeySequence("Ctrl+S"), this, [this](){ doLockSelected(); });
    new QShortcut(QKeySequence("Ctrl+N"), this, [this](){ doNewIdea(); });
    new QShortcut(QKeySequence("Ctrl+A"), this, [this](){ m_listView->selectAll(); });
    new QShortcut(QKeySequence("Ctrl+T"), this, [this](){ doExtractContent(); });
    new QShortcut(QKeySequence("Ctrl+Shift+L"), this, [this](){
        if (m_currentFilterType == "category" && m_currentFilterValue != -1) {
            DatabaseManager::instance().lockCategory(m_currentFilterValue.toInt());
            refreshSidebar();
            refreshData();
        }
    });
    
    // Alt+D Toggle Stay on Top
    new QShortcut(QKeySequence("Alt+D"), this, [this](){ 
        toggleStayOnTop(!m_isStayOnTop); 
    });
    
    new QShortcut(QKeySequence("Alt+W"), this, [this](){ emit toggleMainWindowRequested(); });
    new QShortcut(QKeySequence("Ctrl+Shift+T"), this, [this](){ emit toolboxRequested(); });
    new QShortcut(QKeySequence("Ctrl+B"), this, [this](){ doEditSelected(); });
    new QShortcut(QKeySequence("Ctrl+Q"), this, [this](){ toggleSidebar(); });
    new QShortcut(QKeySequence("Alt+S"), this, [this](){ if(m_currentPage > 1) { m_currentPage--; refreshData(); } });
    new QShortcut(QKeySequence("Alt+X"), this, [this](){ if(m_currentPage < m_totalPages) { m_currentPage++; refreshData(); } });
    
    // 标签复制粘贴快捷键
    new QShortcut(QKeySequence("Ctrl+Shift+C"), this, [this](){ doCopyTags(); });
    new QShortcut(QKeySequence("Ctrl+Shift+V"), this, [this](){ doPasteTags(); });
    
    for (int i = 0; i <= 5; ++i) {
        new QShortcut(QKeySequence(QString("Ctrl+%1").arg(i)), this, [this, i](){ doSetRating(i); });
    }
    
}

void QuickWindow::scheduleRefresh() {
    m_refreshTimer->start();
}

void QuickWindow::onNoteAdded(const QVariantMap& note) {
    // 检查是否符合当前过滤条件
    bool matches = false;
    if (m_currentFilterType == "all") matches = true;
    else if (m_currentFilterType == "today") matches = true;
    else if (m_currentFilterType == "category") {
        matches = (note.value("category_id").toInt() == m_currentFilterValue.toInt());
    } else if (m_currentFilterType == "untagged") {
        matches = note.value("tags").toString().isEmpty();
    }
    
    if (matches && m_currentPage == 1) {
        m_model->prependNote(note);
    }
    
    // 依然需要触发侧边栏计数刷新 (节流执行)
    scheduleRefresh();
}

void QuickWindow::refreshData() {
    if (!isVisible()) return;

    // 记忆当前选中的 ID，以便在刷新后恢复选中状态
    int lastSelectedId = -1;
    QModelIndex currentIdx = m_listView->currentIndex();
    if (currentIdx.isValid()) {
        lastSelectedId = currentIdx.data(NoteModel::IdRole).toInt();
    }

    QString keyword = m_searchEdit->text();
    
    int totalCount = DatabaseManager::instance().getNotesCount(keyword, m_currentFilterType, m_currentFilterValue);
    
    const int pageSize = 100; // 对齐 Python 版
    m_totalPages = qMax(1, (totalCount + pageSize - 1) / pageSize); 
    if (m_currentPage > m_totalPages) m_currentPage = m_totalPages;
    if (m_currentPage < 1) m_currentPage = 1;

    // 检查当前分类是否锁定
    bool isLocked = false;
    if (m_currentFilterType == "category" && m_currentFilterValue != -1) {
        int catId = m_currentFilterValue.toInt();
        if (DatabaseManager::instance().isCategoryLocked(catId)) {
            isLocked = true;
            QString hint;
            auto cats = DatabaseManager::instance().getAllCategories();
            for(const auto& c : std::as_const(cats)) if(c.value("id").toInt() == catId) hint = c.value("password_hint").toString();
            m_lockWidget->setCategory(catId, hint);
        }
    }

    m_listView->setVisible(!isLocked);
    m_lockWidget->setVisible(isLocked);

    if (isLocked && m_quickPreview->isVisible()) {
        m_quickPreview->hide();
    }

    m_model->setNotes(isLocked ? QList<QVariantMap>() : DatabaseManager::instance().searchNotes(keyword, m_currentFilterType, m_currentFilterValue, m_currentPage, pageSize));
    
    // 恢复选中状态
    if (lastSelectedId != -1) {
        for (int i = 0; i < m_model->rowCount(); ++i) {
            QModelIndex idx = m_model->index(i, 0);
            if (idx.data(NoteModel::IdRole).toInt() == lastSelectedId) {
                m_listView->selectionModel()->select(idx, QItemSelectionModel::Select | QItemSelectionModel::Rows);
                m_listView->setCurrentIndex(idx);
                break;
            }
        }
    }

    // 更新工具栏页码 (对齐新版 1:1 布局)
    auto* pageInput = findChild<QLineEdit*>("pageInput");
    if (pageInput) pageInput->setText(QString::number(m_currentPage));
    
    auto* totalLabel = findChild<QLabel*>("totalLabel");
    if (totalLabel) totalLabel->setText(QString::number(m_totalPages));
}

void QuickWindow::updatePartitionStatus(const QString& name) {
    m_statusLabel->setText(QString("当前分区: %1").arg(name.isEmpty() ? "全部数据" : name));
    m_statusLabel->show();
}

void QuickWindow::refreshSidebar() {
    if (!isVisible()) return;
    // 保存选中状态
    QString selectedType;
    QVariant selectedValue;
    QModelIndex sysIdx = m_systemTree->currentIndex();
    QModelIndex partIdx = m_partitionTree->currentIndex();
    
    if (sysIdx.isValid()) {
        selectedType = sysIdx.data(CategoryModel::TypeRole).toString();
        selectedValue = sysIdx.data(CategoryModel::NameRole);
    } else if (partIdx.isValid()) {
        selectedType = partIdx.data(CategoryModel::TypeRole).toString();
        selectedValue = partIdx.data(CategoryModel::IdRole);
    }

    m_systemModel->refresh();
    m_partitionModel->refresh();
    m_partitionTree->expandAll();

    // 恢复选中
    if (!selectedType.isEmpty()) {
        if (selectedType != "category") {
            for (int i = 0; i < m_systemModel->rowCount(); ++i) {
                QModelIndex idx = m_systemModel->index(i, 0);
                if (idx.data(CategoryModel::TypeRole).toString() == selectedType &&
                    idx.data(CategoryModel::NameRole) == selectedValue) {
                    m_systemTree->setCurrentIndex(idx);
                    break;
                }
            }
        } else {
            std::function<void(const QModelIndex&)> findAndSelect = [&](const QModelIndex& parent) {
                for (int i = 0; i < m_partitionModel->rowCount(parent); ++i) {
                    QModelIndex idx = m_partitionModel->index(i, 0, parent);
                    if (idx.data(CategoryModel::IdRole) == selectedValue) {
                        m_partitionTree->setCurrentIndex(idx);
                        return;
                    }
                    if (m_partitionModel->rowCount(idx) > 0) findAndSelect(idx);
                }
            };
            findAndSelect(QModelIndex());
        }
    }
}

void QuickWindow::applyListTheme(const QString& colorHex) {
    QString style;
    if (!colorHex.isEmpty()) {
        QColor c(colorHex);
        // 对齐 Python 版，背景保持深色，高亮色由 Delegate 处理，这里主要设置斑马纹
        style = QString("QListView { "
                        "  border: none; "
                        "  background-color: #1e1e1e; "
                        "  alternate-background-color: #252526; "
                        "  selection-background-color: transparent; "
                        "  color: #eee; "
                        "  outline: none; "
                        "}");
    } else {
        style = "QListView { "
                "  border: none; "
                "  background-color: #1e1e1e; "
                "  alternate-background-color: #252526; "
                "  selection-background-color: transparent; "
                "  color: #eee; "
                "  outline: none; "
                "}";
    }
    m_listView->setStyleSheet(style);
}

void QuickWindow::activateNote(const QModelIndex& index) {
    if (!index.isValid()) return;

    int id = index.data(NoteModel::IdRole).toInt();
    QVariantMap note = DatabaseManager::instance().getNoteById(id);
    
    // 记录访问
    DatabaseManager::instance().recordAccess(id);

    QString itemType = note.value("item_type").toString();
    QString content = note.value("content").toString();
    QByteArray blob = note.value("data_blob").toByteArray();
    
    if (itemType == "image") {
        QImage img;
        img.loadFromData(blob);
        ClipboardMonitor::instance().skipNext();
        QApplication::clipboard()->setImage(img);
    } else if (itemType == "local_file" || itemType == "local_folder" || itemType == "local_batch") {
        // 文件系统托管模式：从相对路径恢复绝对路径
        QString fullPath = QCoreApplication::applicationDirPath() + "/" + content;
        QFileInfo fi(fullPath);
        if (fi.exists()) {
            QMimeData* mimeData = new QMimeData();
            if (itemType == "local_batch") {
                // 批量托管模式：双击发送该批量的所有文件/文件夹内容
                QDir dir(fullPath);
                QList<QUrl> urls;
                for (const QString& fileName : dir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
                    urls << QUrl::fromLocalFile(dir.absoluteFilePath(fileName));
                }
                if (urls.isEmpty()) urls << QUrl::fromLocalFile(fullPath); // 保底发送文件夹自身
                mimeData->setUrls(urls);
            } else {
                mimeData->setUrls({QUrl::fromLocalFile(fullPath)});
            }
            ClipboardMonitor::instance().skipNext();
            QApplication::clipboard()->setMimeData(mimeData);
        } else {
            QApplication::clipboard()->setText(content);
            QToolTip::showText(QCursor::pos(), StringUtils::wrapToolTip("⚠️ 文件已丢失或被移动"), this);
        }
    } else if (!blob.isEmpty() && (itemType == "file" || itemType == "folder")) {
        // 旧的数据库存储模式：导出到临时目录
        QString title = note.value("title").toString();
        QString exportDir = QDir::tempPath() + "/RapidNotes_Export";
        QDir().mkpath(exportDir);
        QString tempPath = exportDir + "/" + title;
        
        QFile f(tempPath);
        if (f.open(QIODevice::WriteOnly)) {
            f.write(blob);
            f.close();
            
            QMimeData* mimeData = new QMimeData();
            mimeData->setUrls({QUrl::fromLocalFile(tempPath)});
            QApplication::clipboard()->setMimeData(mimeData);
        } else {
            QApplication::clipboard()->setText(content);
        }
    } else if (itemType != "text" && !itemType.isEmpty()) {
        QStringList rawPaths = content.split(';', Qt::SkipEmptyParts);
        QList<QUrl> validUrls;
        QStringList missingFiles;
        
        for (const QString& p : std::as_const(rawPaths)) {
            QString path = p.trimmed().remove('\"');
            if (QFileInfo::exists(path)) {
                validUrls << QUrl::fromLocalFile(path);
            } else {
                missingFiles << QFileInfo(path).fileName();
            }
        }
        
        if (!validUrls.isEmpty()) {
            QMimeData* mimeData = new QMimeData();
            mimeData->setUrls(validUrls);
            QApplication::clipboard()->setMimeData(mimeData);
        } else {
            QApplication::clipboard()->setText(content);
            if (!missingFiles.isEmpty()) {
                QToolTip::showText(QCursor::pos(), StringUtils::wrapToolTip("⚠️ 原文件已丢失，已复制路径文本"), this);
            }
        }
    } else {
        StringUtils::copyNoteToClipboard(content);
    }

    // hide(); // 用户要求不隐藏窗口

#ifdef Q_OS_WIN
    if (m_lastActiveHwnd && IsWindow(m_lastActiveHwnd)) {
        DWORD currThread = GetCurrentThreadId();
        bool attached = false;
        if (m_lastThreadId != 0 && m_lastThreadId != currThread) {
            attached = AttachThreadInput(currThread, m_lastThreadId, TRUE);
        }

        if (IsIconic(m_lastActiveHwnd)) {
            ShowWindow(m_lastActiveHwnd, SW_RESTORE);
        }
        SetForegroundWindow(m_lastActiveHwnd);
        
        if (m_lastFocusHwnd && IsWindow(m_lastFocusHwnd)) {
            SetFocus(m_lastFocusHwnd);
        }

        DWORD lastThread = m_lastThreadId;
        QTimer::singleShot(300, [lastThread, attached]() {
            // 1. 使用 SendInput 强制清理所有修饰键状态 (L/R Ctrl, Shift, Alt, Win)
            // 替换旧的 keybd_event，确保清理逻辑更原子化
            INPUT releaseInputs[8];
            memset(releaseInputs, 0, sizeof(releaseInputs));
            BYTE keys[] = { VK_LCONTROL, VK_RCONTROL, VK_LSHIFT, VK_RSHIFT, VK_LMENU, VK_RMENU, VK_LWIN, VK_RWIN };
            for (int i = 0; i < 8; ++i) {
                releaseInputs[i].type = INPUT_KEYBOARD;
                releaseInputs[i].ki.wVk = keys[i];
                releaseInputs[i].ki.dwFlags = KEYEVENTF_KEYUP;
            }
            SendInput(8, releaseInputs, sizeof(INPUT));

            // 2. 使用 SendInput 发送 Ctrl+V 序列 (显式指定 VK_LCONTROL 提高兼容性)
            INPUT inputs[4];
            memset(inputs, 0, sizeof(inputs));

            // Ctrl 按下
            inputs[0].type = INPUT_KEYBOARD;
            inputs[0].ki.wVk = VK_LCONTROL;
            inputs[0].ki.wScan = MapVirtualKey(VK_LCONTROL, MAPVK_VK_TO_VSC);

            // V 按下
            inputs[1].type = INPUT_KEYBOARD;
            inputs[1].ki.wVk = 'V';
            inputs[1].ki.wScan = MapVirtualKey('V', MAPVK_VK_TO_VSC);

            // V 抬起
            inputs[2].type = INPUT_KEYBOARD;
            inputs[2].ki.wVk = 'V';
            inputs[2].ki.wScan = MapVirtualKey('V', MAPVK_VK_TO_VSC);
            inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;

            // Ctrl 抬起
            inputs[3].type = INPUT_KEYBOARD;
            inputs[3].ki.wVk = VK_LCONTROL;
            inputs[3].ki.wScan = MapVirtualKey(VK_LCONTROL, MAPVK_VK_TO_VSC);
            inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;

            SendInput(4, inputs, sizeof(INPUT));

            if (attached) {
                // 确保按键消息推入后再分离线程
                AttachThreadInput(GetCurrentThreadId(), lastThread, FALSE);
            }
        });
    }
#endif
}

void QuickWindow::doDeleteSelected(bool physical) {
    auto selected = m_listView->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;

    bool inTrash = (m_currentFilterType == "trash");
    
    if (physical || inTrash) {
        // 物理删除前增加二次确认
        QString title = inTrash ? "清空项目" : "彻底删除";
        QString text = QString("确定要永久删除选中的 %1 条数据吗？\n此操作不可逆，数据将无法找回。").arg(selected.count());
        
        auto* msg = new FramelessMessageBox(title, text, this);
        msg->setAttribute(Qt::WA_DeleteOnClose);
        
        // 提取 ID 列表以备删除
        QList<int> idsToDelete;
        for (const auto& index : std::as_const(selected)) idsToDelete << index.data(NoteModel::IdRole).toInt();
        
        connect(msg, &FramelessMessageBox::confirmed, this, [this, idsToDelete]() {
            if (idsToDelete.isEmpty()) return;
            DatabaseManager::instance().deleteNotesBatch(idsToDelete);
            refreshData();
            refreshSidebar();
            QToolTip::showText(QCursor::pos(), 
                StringUtils::wrapToolTip(QString("<b style='color: #2ecc71;'>✔ 已永久删除 %1 条数据</b>").arg(idsToDelete.count())), this);
        });
        msg->show();
    } else {
        // 移至回收站：解除绑定
        QList<int> idsToTrash;
        for (const auto& index : std::as_const(selected)) idsToTrash << index.data(NoteModel::IdRole).toInt();
        DatabaseManager::instance().softDeleteNotes(idsToTrash);
        refreshData();
    }
    refreshSidebar();
}

void QuickWindow::doRestoreTrash() {
    if (DatabaseManager::instance().restoreAllFromTrash()) {
        refreshData();
        refreshSidebar();
    }
}

void QuickWindow::doToggleFavorite() {
    auto selected = m_listView->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;
    for (const auto& index : std::as_const(selected)) {
        int id = index.data(NoteModel::IdRole).toInt();
        DatabaseManager::instance().toggleNoteState(id, "is_favorite");
    }
    refreshData();
}

void QuickWindow::doTogglePin() {
    auto selected = m_listView->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;
    for (const auto& index : std::as_const(selected)) {
        int id = index.data(NoteModel::IdRole).toInt();
        DatabaseManager::instance().toggleNoteState(id, "is_pinned");
    }
    refreshData();
}

void QuickWindow::doLockSelected() {
    auto selected = m_listView->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;
    
    bool firstState = selected.first().data(NoteModel::LockedRole).toBool();
    bool targetState = !firstState;

    QList<int> ids;
    for (const auto& index : std::as_const(selected)) ids << index.data(NoteModel::IdRole).toInt();
    
    DatabaseManager::instance().updateNoteStateBatch(ids, "is_locked", targetState);
    refreshData();
}

void QuickWindow::doNewIdea() {
    NoteEditWindow* win = new NoteEditWindow();
    connect(win, &NoteEditWindow::noteSaved, this, &QuickWindow::refreshData);
    win->show();
}

void QuickWindow::doExtractContent() {
    auto selected = m_listView->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;
    QStringList texts;
    for (const auto& index : std::as_const(selected)) {
        int id = index.data(NoteModel::IdRole).toInt();
        QVariantMap note = DatabaseManager::instance().getNoteById(id);
        QString type = note.value("item_type").toString();
        if (type == "text" || type.isEmpty()) {
            QString content = note.value("content").toString();
            texts << StringUtils::htmlToPlainText(content);
        }
    }
    if (!texts.isEmpty()) {
        ClipboardMonitor::instance().skipNext();
        QApplication::clipboard()->setText(texts.join("\n---\n"));
    }
}

void QuickWindow::doEditSelected() {
    QModelIndex index = m_listView->currentIndex();
    if (!index.isValid()) return;
    doEditNote(index.data(NoteModel::IdRole).toInt());
}

void QuickWindow::doEditNote(int id) {
    if (id <= 0) return;
    NoteEditWindow* win = new NoteEditWindow(id);
    connect(win, &NoteEditWindow::noteSaved, this, &QuickWindow::refreshData);
    win->show();
}

void QuickWindow::doSetRating(int rating) {
    auto selected = m_listView->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;
    for (const auto& index : std::as_const(selected)) {
        int id = index.data(NoteModel::IdRole).toInt();
        DatabaseManager::instance().updateNoteState(id, "rating", rating);
    }
    refreshData();
}

void QuickWindow::doGlobalLock() {
    // 0. 预检密码是否设定
    QSettings settings("RapidNotes", "QuickWindow");
    if (settings.value("appPassword").toString().isEmpty()) {
        QToolTip::showText(QCursor::pos(), StringUtils::wrapToolTip("<b style='color: #e74c3c;'>✖ 尚未设定应用密码，请先进行设定</b>"), this);
        return;
    }

    // 1. 隐藏所有其它顶级业务窗口 (排除自身、悬浮球)
    for (QWidget* widget : QApplication::topLevelWidgets()) {
        if (widget == this) continue;
        if (widget->objectName() == "FloatingBall") continue;
        if (widget->inherits("QSystemTrayIcon")) continue; // 虽然不是 QWidget 但遍历通常不含它
        
        // 排除某些特定窗口类或对象名 (可选)
        if (widget->isVisible()) {
            widget->hide();
        }
    }

    // 2. 强制显示应用锁
    setupAppLock();

    // 3. 弹出极速窗口并聚焦
    showAuto();
    
    QToolTip::showText(QCursor::pos(), StringUtils::wrapToolTip("<b style='color: #2ecc71;'>✔ 应用已锁定</b>"), this);
}

void QuickWindow::updatePreviewContent() {
    QModelIndex index = m_listView->currentIndex();
    if (!index.isValid()) return;
    int id = index.data(NoteModel::IdRole).toInt();
    QVariantMap note = DatabaseManager::instance().getNoteById(id);
    
    // 记录访问
    DatabaseManager::instance().recordAccess(id);

    // 尽量保持当前预览窗口的位置，如果没显示则计算初始位置
    QPoint pos;
    if (m_quickPreview->isVisible()) {
        pos = m_quickPreview->pos();
    } else {
        pos = m_listView->mapToGlobal(m_listView->rect().center()) - QPoint(250, 300);
    }

    m_quickPreview->showPreview(
        id,
        note.value("title").toString(), 
        note.value("content").toString(), 
        note.value("item_type").toString(),
        note.value("data_blob").toByteArray(),
        pos
    );
}

void QuickWindow::doPreview() {
    // 保护：如果焦点在搜索框或其他输入框，空格键应保留其原始功能
    QWidget* focusWidget = QApplication::focusWidget();
    if (focusWidget && (qobject_cast<QLineEdit*>(focusWidget) || 
                        qobject_cast<QTextEdit*>(focusWidget) ||
                        qobject_cast<QPlainTextEdit*>(focusWidget))) {
        return;
    }

    if (m_quickPreview->isVisible()) {
        m_quickPreview->hide();
        return;
    }
    
    updatePreviewContent();
    
    m_quickPreview->raise();
    m_quickPreview->activateWindow();
}

void QuickWindow::toggleStayOnTop(bool checked) {
    m_isStayOnTop = checked;

    if (isVisible()) {
#ifdef Q_OS_WIN
        HWND hwnd = (HWND)winId();
        SetWindowPos(hwnd, checked ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
#else
        Qt::WindowFlags f = windowFlags();
        if (checked) f |= Qt::WindowStaysOnTopHint;
        else f &= ~Qt::WindowStaysOnTopHint;
        setWindowFlags(f);
        show();
#endif
    }
    // 更新按钮状态与图标
    auto* btnPin = findChild<QPushButton*>("btnPin");
    if (btnPin) {
        if (btnPin->isChecked() != checked) btnPin->setChecked(checked);
        // 切换图标样式 (选中时白色垂直，未选中时灰色倾斜)
        btnPin->setIcon(IconHelper::getIcon(checked ? "pin_vertical" : "pin_tilted", checked ? "#ffffff" : "#aaaaaa"));
    }
}

void QuickWindow::toggleSidebar() {
    bool visible = !m_systemTree->parentWidget()->isVisible();
    m_systemTree->parentWidget()->setVisible(visible);
    
    // 更新按钮状态
    auto* btnSidebar = findChild<QPushButton*>("btnSidebar");
    if (btnSidebar) {
        btnSidebar->setChecked(visible);
        btnSidebar->setIcon(IconHelper::getIcon("eye", visible ? "#ffffff" : "#aaaaaa"));
    }

    QString name;
    if (m_systemTree->currentIndex().isValid()) name = m_systemTree->currentIndex().data().toString();
    else name = m_partitionTree->currentIndex().data().toString();
    
    updatePartitionStatus(name);
}

void QuickWindow::showListContextMenu(const QPoint& pos) {
    auto selected = m_listView->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) {
        QModelIndex index = m_listView->indexAt(pos);
        if (index.isValid()) {
            m_listView->setCurrentIndex(index);
            selected << index;
        } else {
            return;
        }
    }

    int selCount = selected.size();
    QMenu menu(this);
    menu.setStyleSheet("QMenu { background-color: #2D2D2D; color: #EEE; border: 1px solid #444; padding: 4px; } "
                       /* 10px 间距规范：padding-left 10px + icon margin-left 6px */
                       "QMenu::item { padding: 6px 10px 6px 10px; border-radius: 3px; } "
                       "QMenu::icon { margin-left: 6px; } "
                       "QMenu::item:selected { background-color: #4a90e2; color: white; }");

    if (selCount == 1) {
        menu.addAction(IconHelper::getIcon("eye", "#1abc9c", 18), "预览 (Space)", this, &QuickWindow::doPreview);
    }
    
    menu.addAction(IconHelper::getIcon("copy", "#1abc9c", 18), QString("复制内容 (%1)").arg(selCount), this, &QuickWindow::doExtractContent);
    menu.addSeparator();

    if (selCount == 1) {
        menu.addAction(IconHelper::getIcon("edit", "#4a90e2", 18), "编辑 (Ctrl+B)", this, &QuickWindow::doEditSelected);
        menu.addSeparator();
    }

    auto* ratingMenu = menu.addMenu(IconHelper::getIcon("star", "#f39c12", 18), QString("设置星级 (%1)").arg(selCount));
    ratingMenu->setStyleSheet(menu.styleSheet());
    auto* starGroup = new QActionGroup(this);
    int currentRating = (selCount == 1) ? selected.first().data(NoteModel::RatingRole).toInt() : -1;
    
    for (int i = 1; i <= 5; ++i) {
        QString stars = QString("★").repeated(i);
        QAction* action = ratingMenu->addAction(stars, [this, i]() { doSetRating(i); });
        action->setCheckable(true);
        if (i == currentRating) action->setChecked(true);
        starGroup->addAction(action);
    }
    ratingMenu->addSeparator();
    ratingMenu->addAction("清除评级", [this]() { doSetRating(0); });

    bool isFavorite = selected.first().data(NoteModel::FavoriteRole).toBool();
    menu.addAction(IconHelper::getIcon(isFavorite ? "bookmark_filled" : "bookmark", "#ff6b81", 18), 
                   isFavorite ? "取消书签" : "添加书签 (Ctrl+E)", this, &QuickWindow::doToggleFavorite);

    bool isPinned = selected.first().data(NoteModel::PinnedRole).toBool();
    menu.addAction(IconHelper::getIcon(isPinned ? "pin_vertical" : "pin_tilted", isPinned ? "#e74c3c" : "#aaaaaa", 18), 
                   isPinned ? "取消置顶" : "置顶选中项 (Ctrl+P)", this, &QuickWindow::doTogglePin);
    
    bool isLocked = selected.first().data(NoteModel::LockedRole).toBool();
    menu.addAction(IconHelper::getIcon("lock", isLocked ? "#aaaaaa" : "#888888", 18), 
                   isLocked ? "解锁选中项" : "锁定选中项 (Ctrl+S)", this, &QuickWindow::doLockSelected);
    
    menu.addSeparator();

    auto* catMenu = menu.addMenu(IconHelper::getIcon("branch", "#cccccc", 18), QString("移动选中项到分类 (%1)").arg(selCount));
    catMenu->setStyleSheet(menu.styleSheet());
    catMenu->addAction(IconHelper::getIcon("uncategorized", "#e67e22", 18), "未分类", [this]() { doMoveToCategory(-1); });
    
    QVariantList recentCats = StringUtils::getRecentCategories();
    auto allCategories = DatabaseManager::instance().getAllCategories();
    QMap<int, QVariantMap> catMap;
    for (const auto& cat : std::as_const(allCategories)) catMap[cat.value("id").toInt()] = cat;

    int count = 0;
    for (const auto& v : std::as_const(recentCats)) {
        if (count >= 10) break;
        int cid = v.toInt();
        if (catMap.contains(cid)) {
            const auto& cat = catMap.value(cid);
            catMenu->addAction(IconHelper::getIcon("branch", cat.value("color").toString(), 18), cat.value("name").toString(), [this, cid]() {
                doMoveToCategory(cid);
            });
            count++;
        }
    }

    menu.addSeparator();
    if (m_currentFilterType == "trash") {
        menu.addAction(IconHelper::getIcon("refresh", "#2ecc71", 18), "恢复 (还原到未分类)", [this, selected](){
            QList<int> ids;
            for (const auto& index : selected) ids << index.data(NoteModel::IdRole).toInt();
            DatabaseManager::instance().moveNotesToCategory(ids, -1);
            refreshData();
            refreshSidebar();
        });
        menu.addAction(IconHelper::getIcon("trash", "#e74c3c", 18), "彻底删除 (不可逆)", [this](){ doDeleteSelected(true); });
    } else {
        menu.addAction(IconHelper::getIcon("trash", "#e74c3c", 18), "移至回收站 (Delete)", [this](){ doDeleteSelected(false); });
    }

    menu.exec(m_listView->mapToGlobal(pos));
}

void QuickWindow::showSidebarMenu(const QPoint& pos) {
    auto* tree = qobject_cast<QTreeView*>(sender());
    if (!tree) return;

    QModelIndex index = tree->indexAt(pos);
    QMenu menu(this);
    menu.setStyleSheet("QMenu { background-color: #2D2D2D; color: #EEE; border: 1px solid #444; padding: 4px; } "
                       /* 10px 间距规范：padding-left 10px + icon margin-left 6px */
                       "QMenu::item { padding: 6px 10px 6px 10px; border-radius: 3px; } "
                       "QMenu::icon { margin-left: 6px; } "
                       "QMenu::item:selected { background-color: #4a90e2; color: white; }");

    if (!index.isValid() || index.data().toString() == "我的分区") {
        menu.addAction(IconHelper::getIcon("add", "#3498db", 18), "新建分组", [this]() {
            bool ok;
            QString text = QInputDialog::getText(this, "新建组", "组名称:", QLineEdit::Normal, "", &ok);
            if (ok && !text.isEmpty()) {
                DatabaseManager::instance().addCategory(text);
            }
        });
        menu.exec(tree->mapToGlobal(pos));
        return;
    }

    QString type = index.data(CategoryModel::TypeRole).toString();
    if (type == "category") {
        int catId = index.data(CategoryModel::IdRole).toInt();
        QString currentName = index.data(CategoryModel::NameRole).toString();

        menu.addAction(IconHelper::getIcon("add", "#3498db", 18), "新建数据", [this, catId]() {
            auto* win = new NoteEditWindow();
            win->setDefaultCategory(catId);
            connect(win, &NoteEditWindow::noteSaved, this, &QuickWindow::refreshData);
            win->show();
        });
        menu.addSeparator();
        menu.addAction(IconHelper::getIcon("palette", "#e67e22", 18), "设置颜色", [this, catId]() {
            auto* dlg = new QColorDialog(Qt::gray, this);
            dlg->setWindowTitle("选择分类颜色");
            dlg->setWindowFlags(dlg->windowFlags() | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
            connect(dlg, &QColorDialog::colorSelected, [this, catId](const QColor& color){
                if (color.isValid()) {
                    DatabaseManager::instance().setCategoryColor(catId, color.name());
                    refreshSidebar();
                }
            });
            connect(dlg, &QColorDialog::finished, dlg, &QObject::deleteLater);
            dlg->show();
        });
        menu.addAction(IconHelper::getIcon("random_color", "#FF6B9D", 18), "随机颜色", [this, catId]() {
            static const QStringList palette = {
                "#FF6B6B", "#4ECDC4", "#45B7D1", "#96CEB4", "#FFEEAD",
                "#D4A5A5", "#9B59B6", "#3498DB", "#E67E22", "#2ECC71",
                "#E74C3C", "#F1C40F", "#1ABC9C", "#34495E", "#95A5A6"
            };
            QString chosenColor = palette.at(QRandomGenerator::global()->bounded(palette.size()));
            DatabaseManager::instance().setCategoryColor(catId, chosenColor);
            refreshData();
            refreshSidebar();
        });
        menu.addAction(IconHelper::getIcon("tag", "#FFAB91", 18), "设置预设标签", [this, catId]() {
            QString currentTags = DatabaseManager::instance().getCategoryPresetTags(catId);
            auto* dlg = new FramelessInputDialog("设置预设标签", "标签 (逗号分隔):", currentTags, this);
            connect(dlg, &FramelessInputDialog::accepted, [this, catId, dlg](){
                DatabaseManager::instance().setCategoryPresetTags(catId, dlg->text());
            });
            dlg->show();
        });
        menu.addSeparator();
        menu.addAction(IconHelper::getIcon("add", "#3498db", 18), "新建分组", [this]() {
            auto* dlg = new FramelessInputDialog("新建分组", "组名称:", "", this);
            connect(dlg, &FramelessInputDialog::accepted, [this, dlg](){
                QString text = dlg->text();
                if (!text.isEmpty()) {
                    DatabaseManager::instance().addCategory(text);
                    refreshSidebar();
                }
            });
            dlg->show();
            dlg->activateWindow();
            dlg->raise();
        });
        menu.addAction(IconHelper::getIcon("add", "#3498db", 18), "新建子分区", [this, catId]() {
            auto* dlg = new FramelessInputDialog("新建子分区", "区名称:", "", this);
            connect(dlg, &FramelessInputDialog::accepted, [this, catId, dlg](){
                QString text = dlg->text();
                if (!text.isEmpty()) {
                    DatabaseManager::instance().addCategory(text, catId);
                    refreshSidebar();
                }
            });
            dlg->show();
            dlg->activateWindow();
            dlg->raise();
        });
        menu.addSeparator();

        menu.addAction(IconHelper::getIcon("edit", "#aaaaaa", 18), "重命名", [this, catId, currentName]() {
            auto* dlg = new FramelessInputDialog("重命名", "新名称:", currentName, this);
            connect(dlg, &FramelessInputDialog::accepted, [this, catId, dlg](){
                QString text = dlg->text();
                if (!text.isEmpty()) {
                    DatabaseManager::instance().renameCategory(catId, text);
                    refreshSidebar();
                }
            });
            dlg->show();
            dlg->activateWindow();
            dlg->raise();
        });
        menu.addAction(IconHelper::getIcon("trash", "#e74c3c", 18), "删除", [this, catId]() {
            auto* dlg = new FramelessMessageBox("确认删除", "确定要删除此分类吗？内容将移至未分类。", this);
            dlg->setAttribute(Qt::WA_DeleteOnClose);
            connect(dlg, &FramelessMessageBox::confirmed, [this, catId](){
                DatabaseManager::instance().deleteCategory(catId);
                refreshSidebar();
            });
            dlg->show();
        });

        menu.addSeparator();
        auto* sortMenu = menu.addMenu(IconHelper::getIcon("list_ol", "#aaaaaa", 18), "排列");
        sortMenu->setStyleSheet(menu.styleSheet());

        int parentId = -1;
        QModelIndex parentIdx = index.parent();
        if (parentIdx.isValid() && parentIdx.data(CategoryModel::TypeRole).toString() == "category") {
            parentId = parentIdx.data(CategoryModel::IdRole).toInt();
        }

        sortMenu->addAction("标题(当前层级) (A→Z)", [this, parentId]() {
            if (DatabaseManager::instance().reorderCategories(parentId, true))
                QToolTip::showText(QCursor::pos(), StringUtils::wrapToolTip("<b style='color:#2ecc71;'>✔ 排列已完成</b>"), this);
        });
        sortMenu->addAction("标题(当前层级) (Z→A)", [this, parentId]() {
            if (DatabaseManager::instance().reorderCategories(parentId, false))
                QToolTip::showText(QCursor::pos(), StringUtils::wrapToolTip("<b style='color:#2ecc71;'>✔ 排列已完成</b>"), this);
        });
        sortMenu->addAction("标题(全部) (A→Z)", [this]() {
            if (DatabaseManager::instance().reorderAllCategories(true))
                QToolTip::showText(QCursor::pos(), StringUtils::wrapToolTip("<b style='color:#2ecc71;'>✔ 全部排列已完成</b>"), this);
        });
        sortMenu->addAction("标题(全部) (Z→A)", [this]() {
            if (DatabaseManager::instance().reorderAllCategories(false))
                QToolTip::showText(QCursor::pos(), StringUtils::wrapToolTip("<b style='color:#2ecc71;'>✔ 全部排列已完成</b>"), this);
        });

        menu.addSeparator();
        auto* pwdMenu = menu.addMenu(IconHelper::getIcon("lock", "#aaaaaa", 18), "密码保护");
        pwdMenu->setStyleSheet(menu.styleSheet());

        pwdMenu->addAction("设置", [this, catId]() {
            QTimer::singleShot(0, [this, catId]() {
                auto* dlg = new CategoryPasswordDialog("设置密码", this);
                connect(dlg, &QDialog::accepted, [this, catId, dlg]() {
                    DatabaseManager::instance().setCategoryPassword(catId, dlg->password(), dlg->passwordHint());
                    refreshSidebar();
                    refreshData();
                });
                dlg->show();
                dlg->activateWindow();
                dlg->raise();
            });
        });
        pwdMenu->addAction("修改", [this, catId]() {
            QTimer::singleShot(0, [this, catId]() {
                auto* verifyDlg = new FramelessInputDialog("验证旧密码", "请输入当前密码:", "", this);
                verifyDlg->setEchoMode(QLineEdit::Password);
                connect(verifyDlg, &FramelessInputDialog::accepted, [this, catId, verifyDlg]() {
                    if (DatabaseManager::instance().verifyCategoryPassword(catId, verifyDlg->text())) {
                        auto* dlg = new CategoryPasswordDialog("修改密码", this);
                        QString currentHint;
                        auto cats = DatabaseManager::instance().getAllCategories();
                        for(const auto& c : std::as_const(cats)) if(c.value("id").toInt() == catId) currentHint = c.value("password_hint").toString();
                        dlg->setInitialData(currentHint);
                        connect(dlg, &QDialog::accepted, [this, catId, dlg]() {
                            DatabaseManager::instance().setCategoryPassword(catId, dlg->password(), dlg->passwordHint());
                            refreshSidebar();
                            refreshData();
                        });
                        dlg->show();
                        dlg->activateWindow();
                        dlg->raise();
                    } else {
                        QToolTip::showText(QCursor::pos(), StringUtils::wrapToolTip("<b style='color: #e74c3c;'>✖ 旧密码验证失败</b>"), this);
                    }
                });
                verifyDlg->show();
                verifyDlg->activateWindow();
                verifyDlg->raise();
            });
        });
        pwdMenu->addAction("移除", [this, catId]() {
            QTimer::singleShot(0, [this, catId]() {
                auto* dlg = new FramelessInputDialog("验证密码", "请输入当前密码以移除保护:", "", this);
                dlg->setEchoMode(QLineEdit::Password);
                connect(dlg, &FramelessInputDialog::accepted, [this, catId, dlg]() {
                    if (DatabaseManager::instance().verifyCategoryPassword(catId, dlg->text())) {
                        DatabaseManager::instance().removeCategoryPassword(catId);
                        refreshSidebar();
                        refreshData();
                    } else {
                        QToolTip::showText(QCursor::pos(), StringUtils::wrapToolTip("<b style='color: #e74c3c;'>✖ 密码错误</b>"), this);
                    }
                });
                dlg->show();
                dlg->activateWindow();
                dlg->raise();
            });
        });
        pwdMenu->addAction("立即锁定", [this, catId]() {
            DatabaseManager::instance().lockCategory(catId);
            refreshSidebar();
            refreshData();
        })->setShortcut(QKeySequence("Ctrl+Shift+L"));
    } else if (type == "trash") {
        menu.addAction(IconHelper::getIcon("refresh", "#2ecc71", 18), "全部恢复 (到未分类)", this, &QuickWindow::doRestoreTrash);
        menu.addSeparator();
        menu.addAction(IconHelper::getIcon("trash", "#e74c3c", 18), "清空回收站", [this]() {
            auto* dlg = new FramelessMessageBox("确认清空", "确定要永久删除回收站中的所有内容吗？\n(此操作不可逆)", this);
            dlg->setAttribute(Qt::WA_DeleteOnClose);
            connect(dlg, &FramelessMessageBox::confirmed, [this](){
                DatabaseManager::instance().emptyTrash();
                refreshData();
                refreshSidebar();
            });
            dlg->show();
        });
    }

    menu.exec(tree->mapToGlobal(pos));
}

void QuickWindow::showToolboxMenu(const QPoint& pos) {
    QMenu menu(this);
    menu.setStyleSheet("QMenu { background-color: #2D2D2D; color: #EEE; border: 1px solid #444; padding: 4px; } "
                       /* 10px 间距规范：padding-left 10px + icon margin-left 6px */
                       "QMenu::item { padding: 6px 10px 6px 10px; border-radius: 3px; } "
                       "QMenu::icon { margin-left: 6px; } "
                       "QMenu::item:selected { background-color: #4a90e2; color: white; }");

    QAction* autoCatAction = menu.addAction(IconHelper::getIcon("zap", "#aaaaaa", 18), "剪贴板自动归档到当前分类");
    autoCatAction->setCheckable(true);
    autoCatAction->setChecked(m_autoCategorizeClipboard);
    connect(autoCatAction, &QAction::triggered, [this](bool checked){
        m_autoCategorizeClipboard = checked;
        QToolTip::showText(QCursor::pos(), StringUtils::wrapToolTip(m_autoCategorizeClipboard ? "✅ 剪贴板自动归档已开启" : "❌ 剪贴板自动归档已关闭"), this);
    });

    menu.addSeparator();
    
    menu.addAction(IconHelper::getIcon("settings", "#aaaaaa", 18), "更多设置...", [this]() {
        auto* dlg = new SettingsWindow(this);
        dlg->exec();
    });

    menu.exec(QCursor::pos());
}

void QuickWindow::doMoveToCategory(int catId) {
    auto selected = m_listView->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;

    if (catId != -1) {
        StringUtils::recordRecentCategory(catId);
    }

    QList<int> ids;
    for (const auto& index : std::as_const(selected)) ids << index.data(NoteModel::IdRole).toInt();
    
    DatabaseManager::instance().moveNotesToCategory(ids, catId);
    refreshData();
}

void QuickWindow::handleTagInput() {
    QString text = m_tagEdit->text().trimmed();
    if (text.isEmpty()) return;
    
    auto selected = m_listView->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;

    QStringList tags = { text };
    for (const auto& index : std::as_const(selected)) {
        int id = index.data(NoteModel::IdRole).toInt();
        DatabaseManager::instance().addTagsToNote(id, tags);
    }
    
    m_tagEdit->clear();
    refreshData();
    m_listView->clearSelection();
    m_listView->setCurrentIndex(QModelIndex());
    QToolTip::showText(QCursor::pos(), StringUtils::wrapToolTip("✅ 标签已添加"), this);
}

void QuickWindow::openTagSelector() {
    auto selected = m_listView->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;

    QStringList currentTags;
    if (selected.size() == 1) {
        int id = selected.first().data(NoteModel::IdRole).toInt();
        QVariantMap note = DatabaseManager::instance().getNoteById(id);
        currentTags = note.value("tags").toString().split(",", Qt::SkipEmptyParts);
    }

    for (QString& t : currentTags) t = t.trimmed();

    auto* selector = new AdvancedTagSelector(this);
    auto recentTags = DatabaseManager::instance().getRecentTagsWithCounts(20);
    auto allTags = DatabaseManager::instance().getAllTags();
    selector->setup(recentTags, allTags, currentTags);

    connect(selector, &AdvancedTagSelector::tagsConfirmed, [this, selected](const QStringList& tags){
        for (const auto& index : std::as_const(selected)) {
            int id = index.data(NoteModel::IdRole).toInt();
            DatabaseManager::instance().updateNoteState(id, "tags", tags.join(", "));
        }
        refreshData();
        m_listView->clearSelection();
        m_listView->setCurrentIndex(QModelIndex());
        QToolTip::showText(QCursor::pos(), StringUtils::wrapToolTip("✅ 标签已更新"), this);
    });

    selector->showAtCursor();
}

void QuickWindow::doCopyTags() {
    auto selected = m_listView->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;

    // 获取选中的第一个项的标签
    int id = selected.first().data(NoteModel::IdRole).toInt();
    QVariantMap note = DatabaseManager::instance().getNoteById(id);
    QString tagsStr = note.value("tags").toString();
    QStringList tags = tagsStr.split(QRegularExpression("[,，]"), Qt::SkipEmptyParts);
    for (QString& t : tags) t = t.trimmed();

    DatabaseManager::setTagClipboard(tags);
    QToolTip::showText(QCursor::pos(), StringUtils::wrapToolTip(QString("✅ 已复制 %1 个标签").arg(tags.size())), this);
}

void QuickWindow::doPasteTags() {
    auto selected = m_listView->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;

    QStringList tagsToPaste = DatabaseManager::getTagClipboard();
    if (tagsToPaste.isEmpty()) {
        QToolTip::showText(QCursor::pos(), StringUtils::wrapToolTip("❌ 标签剪贴板为空"), this);
        return;
    }

    // 直接覆盖标签 (符合粘贴语义)
    for (const auto& index : std::as_const(selected)) {
        int id = index.data(NoteModel::IdRole).toInt();
        DatabaseManager::instance().updateNoteState(id, "tags", tagsToPaste.join(", "));
    }

    refreshData();
    QToolTip::showText(QCursor::pos(), StringUtils::wrapToolTip(QString("✅ 已覆盖粘贴标签至 %1 条数据").arg(selected.size())), this);
}

void QuickWindow::focusLockInput() {
    if (m_appLockWidget) {
        static_cast<AppLockWidget*>(m_appLockWidget)->focusInput();
    }
}

void QuickWindow::showAuto() {
#ifdef Q_OS_WIN
    HWND myHwnd = (HWND)winId();
    HWND current = GetForegroundWindow();
    if (current != myHwnd) {
        m_lastActiveHwnd = current;
        m_lastThreadId = GetWindowThreadProcessId(m_lastActiveHwnd, nullptr);
        GUITHREADINFO gti;
        gti.cbSize = sizeof(GUITHREADINFO);
        if (GetGUIThreadInfo(m_lastThreadId, &gti)) {
            m_lastFocusHwnd = gti.hwndFocus;
        } else {
            m_lastFocusHwnd = nullptr;
        }
    }
#endif

    // 仅在从未保存过位置时执行居中逻辑
    QSettings settings("RapidNotes", "QuickWindow");
    if (!settings.contains("geometry")) {
        QScreen *screen = QGuiApplication::primaryScreen();
        if (screen) {
            QRect screenGeom = screen->geometry();
            move(screenGeom.center() - rect().center());
        }
    }

    QPoint targetPos = pos();
    bool wasHidden = !isVisible() || isMinimized();

    if (isMinimized()) {
        showNormal();
    } else {
        show();
    }

    if (wasHidden) {
        setWindowOpacity(0);
        auto* fade = new QPropertyAnimation(this, "windowOpacity");
        fade->setDuration(300);
        fade->setStartValue(0.0);
        fade->setEndValue(1.0);
        fade->setEasingCurve(QEasingCurve::OutCubic);

        auto* slide = new QPropertyAnimation(this, "pos");
        slide->setDuration(300);
        slide->setStartValue(targetPos + QPoint(0, 10));
        slide->setEndValue(targetPos);
        slide->setEasingCurve(QEasingCurve::OutCubic);

        fade->start(QAbstractAnimation::DeleteWhenStopped);
        slide->start(QAbstractAnimation::DeleteWhenStopped);
    }
    
    raise();
    activateWindow();
    
#ifdef Q_OS_WIN
    // 强制置顶并激活，即使在其他窗口之后也能强制唤起
    SetForegroundWindow(myHwnd);
#endif

    if (isLocked()) {
        focusLockInput();
    } else {
        m_searchEdit->setFocus();
        m_searchEdit->selectAll();
    }
}

void QuickWindow::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    
    // 强制每次显示时都清除选择，确保输入框初始处于禁用状态
    if (m_listView && m_listView->selectionModel()) {
        m_listView->clearSelection();
        m_listView->setCurrentIndex(QModelIndex());
    }

#ifdef Q_OS_WIN
    HWND myHwnd = (HWND)winId();
    if (m_isStayOnTop) {
        SetWindowPos(myHwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    } else {
        // 瞬间置顶再取消，确保能强制唤起
        SetWindowPos(myHwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        QTimer::singleShot(150, [myHwnd]() {
            SetWindowPos(myHwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        });
    }
#endif
}

#ifdef Q_OS_WIN
bool QuickWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result) {
    MSG* msg = static_cast<MSG*>(message);
    if (msg->message == WM_NCHITTEST) {
        // 原生边缘检测，实现丝滑的双向箭头缩放体验
        int x = GET_X_LPARAM(msg->lParam);
        int y = GET_Y_LPARAM(msg->lParam);
        
        // 转换为本地坐标
        QPoint pos = mapFromGlobal(QPoint(x, y));
        int margin = RESIZE_MARGIN;
        int w = width();
        int h = height();

        bool left = pos.x() < margin;
        bool right = pos.x() > w - margin;
        bool top = pos.y() < margin;
        bool bottom = pos.y() > h - margin;

        if (top && left) *result = HTTOPLEFT;
        else if (top && right) *result = HTTOPRIGHT;
        else if (bottom && left) *result = HTBOTTOMLEFT;
        else if (bottom && right) *result = HTBOTTOMRIGHT;
        else if (top) *result = HTTOP;
        else if (bottom) *result = HTBOTTOM;
        else if (left) *result = HTLEFT;
        else if (right) *result = HTRIGHT;
        else return QWidget::nativeEvent(eventType, message, result);

        return true;
    }
    return QWidget::nativeEvent(eventType, message, result);
}
#endif

bool QuickWindow::event(QEvent* event) {
    return QWidget::event(event);
}

void QuickWindow::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        // 不在边距区域，启动移动窗口（只有点击空白处时）
        if (auto* handle = windowHandle()) {
            handle->startSystemMove();
        }
        event->accept();
    }
}

void QuickWindow::mouseMoveEvent(QMouseEvent* event) {
    QWidget::mouseMoveEvent(event);
}

void QuickWindow::mouseReleaseEvent(QMouseEvent* event) {
    QWidget::mouseReleaseEvent(event);
}

void QuickWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls() || event->mimeData()->hasText() || event->mimeData()->hasImage()) {
        event->acceptProposedAction();
    }
}

void QuickWindow::dragMoveEvent(QDragMoveEvent* event) {
    event->acceptProposedAction();
}

void QuickWindow::dropEvent(QDropEvent* event) {
    const QMimeData* mime = event->mimeData();
    int targetId = -1;
    if (m_currentFilterType == "category") {
        targetId = m_currentFilterValue.toInt();
    }

    QString itemType = "text";
    QString title;
    QString content;
    QByteArray dataBlob;
    QStringList tags;

    if (mime->hasText() && !mime->text().trimmed().isEmpty()) {
        content = mime->text();
        title = content.trimmed().left(50).replace("\n", " ");
        itemType = "text";
    } else if (mime->hasUrls()) {
        QList<QUrl> urls = mime->urls();
        QStringList paths;
        for (const QUrl& url : std::as_const(urls)) {
            if (url.isLocalFile()) {
                QString p = url.toLocalFile();
                paths << p;
                if (title.isEmpty()) {
                    QFileInfo info(p);
                    title = info.fileName();
                    itemType = info.isDir() ? "folder" : "file";
                }
            } else {
                paths << url.toString();
                if (title.isEmpty()) {
                    title = "外部链接";
                    itemType = "link";
                }
            }
        }
        content = paths.join(";");
        if (paths.size() > 1) {
            title = QString("批量导入 (%1个文件)").arg(paths.size());
            itemType = "files";
        }
    } else if (mime->hasImage()) {
        QImage img = qvariant_cast<QImage>(mime->imageData());
        if (!img.isNull()) {
            QBuffer buffer(&dataBlob);
            buffer.open(QIODevice::WriteOnly);
            img.save(&buffer, "PNG");
            itemType = "image";
            title = "[拖入图片] " + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
            content = "[Image Data]";
        }
    }

    if (!content.isEmpty() || !dataBlob.isEmpty()) {
        DatabaseManager::instance().addNote(title, content, tags, "", targetId, itemType, dataBlob);
        event->acceptProposedAction();
    }
}

void QuickWindow::hideEvent(QHideEvent* event) {
    // 保护：仅在非系统自发（spontaneous）且窗口确实不可见时才可能退出
    // 防止初始化或某些 Windows 系统消息导致的误退
    if (m_appLockWidget && !event->spontaneous() && !isVisible()) {
        qDebug() << "[QuickWin] 退出程序，因为应用锁处于活动状态且窗口被隐藏";
        QApplication::quit();
    }
    saveState();
    QWidget::hideEvent(event);
}

void QuickWindow::resizeEvent(QResizeEvent* event) {
    if (m_appLockWidget) {
        m_appLockWidget->resize(this->size());
    }
    QWidget::resizeEvent(event);
    saveState();
}

void QuickWindow::moveEvent(QMoveEvent* event) {
    QWidget::moveEvent(event);
    saveState();
}

void QuickWindow::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape || 
        (event->modifiers() == Qt::ControlModifier && event->key() == Qt::Key_W)) {
        hide();
        return;
    }
    if (event->key() == Qt::Key_Space && event->modifiers() == Qt::NoModifier) {
        doPreview();
        return;
    }
    QWidget::keyPressEvent(event);
}

bool QuickWindow::eventFilter(QObject* watched, QEvent* event) {
    // 逻辑 1: 鼠标移动到列表或侧边栏范围内，立即恢复正常光标
    if (watched == m_listView || watched == m_systemTree || watched == m_partitionTree) {
        if (event->type() == QEvent::MouseMove || event->type() == QEvent::Enter) {
            setCursor(Qt::ArrowCursor);
        }
    }

    // 逻辑 2: 侧边栏点击分类且不释放左键时，显示手指光标
    if (watched == m_partitionTree && event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        int key = keyEvent->key();
        auto modifiers = keyEvent->modifiers();

        if ((key == Qt::Key_Up || key == Qt::Key_Down) && (modifiers & Qt::ControlModifier)) {
            QModelIndex current = m_partitionTree->currentIndex();
            if (current.isValid() && current.data(CategoryModel::TypeRole).toString() == "category") {
                int catId = current.data(CategoryModel::IdRole).toInt();
                DatabaseManager::MoveDirection dir;

                if (key == Qt::Key_Up) {
                    dir = (modifiers & Qt::ShiftModifier) ? DatabaseManager::Top : DatabaseManager::Up;
                } else {
                    dir = (modifiers & Qt::ShiftModifier) ? DatabaseManager::Bottom : DatabaseManager::Down;
                }

                if (DatabaseManager::instance().moveCategory(catId, dir)) {
                    refreshSidebar();
                    return true;
                }
            }
        }
    }

    if (watched == m_systemTree || watched == m_partitionTree) {
        if (event->type() == QEvent::MouseButtonPress) {
            QMouseEvent* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                QTreeView* tree = qobject_cast<QTreeView*>(watched);
                if (tree && tree->indexAt(me->pos()).isValid()) {
                    setCursor(Qt::PointingHandCursor);
                }
            }
        } else if (event->type() == QEvent::MouseButtonRelease) {
            setCursor(Qt::ArrowCursor);
        }
    }

    if ((watched == m_listView || watched == m_searchEdit) && event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            if (watched == m_listView) {
                activateNote(m_listView->currentIndex());
                return true;
            }
        }
        if (keyEvent->key() == Qt::Key_Space && keyEvent->modifiers() == Qt::NoModifier) {
            if (watched == m_listView) {
                doPreview();
                return true;
            }
        }
        if (keyEvent->key() == Qt::Key_Escape) {
            hide();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void DittoListView::startDrag(Qt::DropActions supportedActions) {
    // 深度对齐 Ditto：禁用笨重的快照卡片 Pixmap，保持视觉清爽
    QDrag* drag = new QDrag(this);
    drag->setMimeData(model()->mimeData(selectedIndexes()));
    
    // 【深度修复】提供 1x1 透明占位符。
    // 许多现代应用（如 Chrome）在 Windows 上执行 DND 时会验证拖拽图像。
    // 如果完全没有 Pixmap，投放信号可能无法在网页输入框触发。
    QPixmap pix(1, 1);
    pix.fill(Qt::transparent);
    drag->setPixmap(pix);
    drag->setHotSpot(QPoint(0, 0));
    
    // 【核心修复】显式指定默认动作为 CopyAction。
    // 许多外部应用（特别是网页浏览器）需要明确的 Copy 握手信号。
    drag->exec(Qt::CopyAction | Qt::MoveAction, Qt::CopyAction);
}

void DittoListView::mousePressEvent(QMouseEvent* event) {
    QModelIndex index = indexAt(event->pos());
    if (!index.isValid()) {
        // 点击在空白区域，清除选择
        clearSelection();
        setCurrentIndex(QModelIndex());
    }
    QListView::mousePressEvent(event);
}

#include "QuickWindow.moc"