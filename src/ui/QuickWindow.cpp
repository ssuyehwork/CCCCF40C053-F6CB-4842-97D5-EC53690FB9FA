#include "ToolTipOverlay.h"
#include "QuickWindow.h"
#include <QDebug>
/* [MODIFIED] 遵循开发规范：补全因头文件清理而缺失的业务依赖，解决 incomplete type 编译错误 */
#include "SearchLineEdit.h"
#include "CleanListView.h"
#include "ClickableLineEdit.h"
#include "CategoryLockWidget.h"
#include "DropTreeView.h"
#include "QuickPreview.h"
#include "PasswordVerifyDialog.h" // 2026-03-20 增加身份验证支持
#include "FilterPanel.h"
#include "../models/NoteModel.h"
#include <QSortFilterProxyModel>
#include <QTreeView>
#include <QListView>
#include <QPushButton>
#include "../models/CategoryModel.h"
#include "NoteEditWindow.h"
#include "StringUtils.h"
#include "TitleEditorDialog.h"
#include "../core/FileStorageHelper.h"
#include "AdvancedTagSelector.h"
#include "IconHelper.h"
#include "QuickNoteDelegate.h"
#include "CategoryDelegate.h"
#include "../core/DatabaseManager.h"
#include "../core/ClipboardMonitor.h"
#include <QGuiApplication>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QHelpEvent>
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
#include <QElapsedTimer>
#include <QActionGroup>
#include <QAction>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QUrl>
#include <QBuffer>
#include <QScrollBar>
#include <QWheelEvent>
#include <QRegularExpression>
#include <QImage>
#include <QMap>
#include <QSet>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QDesktopServices>
#include <QCoreApplication>
#include <QLineEdit>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QColorDialog>
#include <QFileDialog>
#include <QTextStream>
#include <QStringConverter>
#include "FramelessDialog.h"
#include "../core/ActionRecorder.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSettings>
#include "PasswordVerifyDialog.h"
#include "CategoryPasswordDialog.h"
#include "SettingsWindow.h"
#include "../core/ShortcutManager.h"
#include "../core/HotkeyManager.h"
#include <QRandomGenerator>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QFont>
#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>
#include <QTransform>
#include <QtMath>
#include <QThreadPool>

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

        // 0. [NEW] 2026-03-22 按照用户要求：锁定界面顶部显示“快速笔记”标题
        auto* appTitle = new QLabel("快速笔记");
        appTitle->setStyleSheet("color: white; font-size: 24px; font-weight: bold; margin-bottom: 20px;");
        appTitle->setAlignment(Qt::AlignCenter);
        layout->addWidget(appTitle);

        // 1. 锁图标
        auto* lockIcon = new QLabel();
        // 2026-03-xx 按照用户要求，锁定界面中心图标改为绿色 #00A650，标识为安全
        lockIcon->setPixmap(IconHelper::getIcon("lock_secure", "#00A650").pixmap(64, 64));
        lockIcon->setAlignment(Qt::AlignCenter);
        layout->addWidget(lockIcon);

        // 2. 标题文字
        auto* titleLabel = new QLabel("已锁定");
        // 2026-03-xx 按照用户要求，锁定界面“已锁定”文字改为绿色 #00A650，标识为安全
        titleLabel->setStyleSheet("color: #00A650; font-size: 18px; font-weight: bold;");
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
            // [MODIFIED] 拦截 Esc 键，防止应用锁界面关闭退出程序
            event->accept();
            return;
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
    // [SECURITY] 核心硬件锁验证：严禁移除。这是确保程序仅在授权硬件上运行的第二道防线。
    if (!DatabaseManager::instance().validateGenuineHardware()) {
        qCritical() << "[SECURITY] QuickWindow 初始化检测到非法硬件指纹，熔断退出。";
        ::exit(-5);
    }

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

#ifdef Q_OS_WIN
    StringUtils::applyTaskbarMinimizeStyle((void*)winId());
#endif

    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setSingleShot(true);

    // [MODIFIED] 2026-03-xx 按照用户最新指令：加载折叠偏好
    QSettings settings("RapidNotes", "QuickWindow");
    m_isSidebarPersistent = settings.value("sidebarPersistent", true).toBool();
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
    connect(&DatabaseManager::instance(), &DatabaseManager::autoCategorizeEnabledChanged, this, &QuickWindow::updateAutoCategorizeButton);
    connect(&DatabaseManager::instance(), &DatabaseManager::extensionTargetCategoryIdChanged, this, &QuickWindow::updateAutoCategorizeButton);
    connect(&DatabaseManager::instance(), &DatabaseManager::appLockSettingsChanged, this, &QuickWindow::updateAppLockStatus);

    connect(&DatabaseManager::instance(), &DatabaseManager::activeCategoryIdChanged, this, [this](int id){
        // [CRITICAL] 核心修复：同步主窗口逻辑。防止点击非分类项（如今日、全部）时，由于信号回调
        // 导致当前窗口被强制重置回“未分类”模式，确保双窗联动的稳定性。
        if (id > 0) {
            if (m_currentFilterType == "category" && m_currentFilterValue == id) return;
        } else {
            if (m_currentFilterType != "category") return; 
            if (m_currentFilterValue == -1) return;
        }
        
        // 外部改变了活跃分类，同步本地状态并刷新
        m_currentFilterType = "category";
        m_currentFilterValue = id;
        m_currentPage = 1;
        scheduleRefresh();
    });

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
        
        // [NEW] 分类信息变化（如重命名）时，同步更新自动归档按钮提示
        updateAutoCategorizeButton();
        
        scheduleRefresh();
    });

    // [USER_REQUEST] 移除 200ms 定时器轮询捕获，改用“触发式捕获”模式以提升性能并减少冗余。
    installEventFilter(this);
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

    // --- 包装列表区域并添加焦点指示线 ---
    auto* listWrapper = new QWidget();
    auto* listWrapperLayout = new QVBoxLayout(listWrapper);
    listWrapperLayout->setContentsMargins(0, 0, 0, 0);
    listWrapperLayout->setSpacing(0);

    m_listFocusLine = new QWidget();
    m_listFocusLine->setFixedHeight(1);
    m_listFocusLine->setStyleSheet("background-color: #3498db;");
    m_listFocusLine->hide();
    listWrapperLayout->addWidget(m_listFocusLine);

    m_listStack = new QStackedWidget();
    m_listStack->setMinimumWidth(95); // 确保 117px 左边距
    listWrapperLayout->addWidget(m_listStack);
    
    m_listView = new CleanListView();
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
    m_listView->setAcceptDrops(true);
    m_listView->setDropIndicatorShown(true);
    
    connect(m_listView, &CleanListView::internalMoveRequested, this, [this](const QList<int>& ids, int row){
        if (m_currentFilterType == "recently_visited" || m_currentFilterType == "trash") {
            ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #e67e22;'>[!] 当前视图不支持手动排序</b>");
            return;
        }
        DatabaseManager::instance().moveNotesToRow(ids, row, m_currentFilterType, m_currentFilterValue);
    });

    m_listView->setContextMenuPolicy(Qt::CustomContextMenu);

    m_lockWidget = new CategoryLockWidget(this);
    connect(m_lockWidget, &CategoryLockWidget::unlocked, this, [this](){
        refreshData();
    });
    connect(m_lockWidget, &CategoryLockWidget::escPressed, this, [this](){
        this->setFocus();
    });
    connect(m_listView, &QListView::customContextMenuRequested, this, &QuickWindow::showListContextMenu);
    connect(m_listView, &QListView::doubleClicked, this, [this](const QModelIndex& index){
        activateNote(index);
    });

    // [REFINED] 列表项只有在拥有焦点（键盘导航）或被点击时，才切换到底部“标签输入”
    connect(m_listView->selectionModel(), &QItemSelectionModel::selectionChanged, [this](const QItemSelection& selected) {
        if (!selected.isEmpty() && m_listView->hasFocus()) {
            m_bottomStackedWidget->setCurrentIndex(1); 
        }
    });
    connect(m_listView, &QListView::clicked, [this](){
         m_bottomStackedWidget->setCurrentIndex(1);
    });

    m_sidebarWrapper = new QWidget();
    m_sidebarWrapper->setMinimumWidth(163); // 侧边栏宽度不能小于 163 像素
    auto* sidebarLayout = new QVBoxLayout(m_sidebarWrapper);
    sidebarLayout->setContentsMargins(0, 0, 0, 0);
    sidebarLayout->setSpacing(0);

    m_sidebarFocusLine = new QWidget();
    m_sidebarFocusLine->setFixedHeight(1);
    m_sidebarFocusLine->setStyleSheet("background-color: #3498db;");
    m_sidebarFocusLine->hide();
    sidebarLayout->addWidget(m_sidebarFocusLine);

    QString treeStyle = R"(
        QTreeView {
            background-color: transparent;
            border: none;
            outline: none;
            color: #ccc;
        }
        /* 针对我的分类标题进行加粗白色处理 */
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
        QTreeView::item:hover, QTreeView::item:selected, QTreeView::item:selected:!focus {
            background: transparent;
        }
        QTreeView::branch:hover, QTreeView::branch:selected {
            background: transparent;
        }
        QTreeView::branch:has-children:closed { image: url(:/icons/arrow_right.svg); }
        QTreeView::branch:has-children:open   { image: url(:/icons/arrow_down.svg); }
    )";

    m_systemTree = new DropTreeView();
    m_systemTree->setStyleSheet(treeStyle); 
    m_systemTree->setItemDelegate(new CategoryDelegate(this));
    m_systemModel = new CategoryModel(CategoryModel::System, this);
    
    m_systemProxyModel = new QSortFilterProxyModel(this);
    m_systemProxyModel->setSourceModel(m_systemModel);
    m_systemProxyModel->setFilterRole(CategoryModel::NameRole);
    m_systemProxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_systemProxyModel->setRecursiveFilteringEnabled(true);
    
    m_systemTree->setModel(m_systemProxyModel);
    m_systemTree->setHeaderHidden(true);
    m_systemTree->setMouseTracking(true);
    // 2026-03-xx 按照用户要求：开启系统项装饰，使图标向右偏移以对齐下方分类图标
    m_systemTree->setRootIsDecorated(true);
    m_systemTree->setIndentation(12);
    m_systemTree->setFixedHeight(176); // 8 items * 22px = 176px
    m_systemTree->setEditTriggers(QAbstractItemView::NoEditTriggers); // 绝不可重命名
    m_systemTree->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // 2026-03-xx 按照用户要求：彻底隐藏侧边栏横向滚动条
    m_systemTree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_systemTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_systemTree, &QTreeView::customContextMenuRequested, this, &QuickWindow::showSidebarMenu);

    m_partitionTree = new DropTreeView();
    m_partitionTree->setStyleSheet(treeStyle);
    m_partitionTree->setItemDelegate(new CategoryDelegate(this));
    m_partitionModel = new CategoryModel(CategoryModel::User, this);
    
    m_partitionProxyModel = new QSortFilterProxyModel(this);
    m_partitionProxyModel->setSourceModel(m_partitionModel);
    m_partitionProxyModel->setFilterRole(CategoryModel::NameRole);
    m_partitionProxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_partitionProxyModel->setRecursiveFilteringEnabled(true);
    
    m_partitionTree->setModel(m_partitionProxyModel);
    m_partitionTree->setHeaderHidden(true);
    m_partitionTree->setMouseTracking(true);
    // [CRITICAL] 必须设为 true 以确保与上方的 m_systemTree 保持垂直对齐
    m_partitionTree->setRootIsDecorated(true);
    m_partitionTree->setIndentation(12);
    // 2026-03-xx 按照用户要求：彻底隐藏侧边栏横向与纵向滚动条
    m_partitionTree->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_partitionTree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_partitionTree->setDragEnabled(true);
    m_partitionTree->setAcceptDrops(true);
    m_partitionTree->setDropIndicatorShown(true);
    m_partitionTree->setDragDropMode(QAbstractItemView::InternalMove);
    m_partitionTree->setDefaultDropAction(Qt::MoveAction);
    safeExpandPartitionTree();
    m_partitionTree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_partitionTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_partitionTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_partitionTree, &QTreeView::customContextMenuRequested, this, &QuickWindow::showSidebarMenu);
    // [CRITICAL] 严禁在此处连接 doubleClicked 手动切换展开/折叠，因为 QTreeView 默认已具备此行为。
    // 手动连接会导致状态切换两次（原生+手动），从而出现双击无响应的逻辑抵消问题。

    // [MODIFIED] 2026-03-xx 按照用户最终要求：实现侧边栏列表整体向右偏移 7 像素，向下偏移 10 像素
    // 引入与 MainWindow 一致的 sbContent 包装容器以实现结构对齐
    auto* sbContent = new QWidget();
    sbContent->setAttribute(Qt::WA_StyledBackground, true);
    sbContent->setStyleSheet("background: transparent; border: none;");
    auto* sbContentLayout = new QVBoxLayout(sbContent);
    sbContentLayout->setContentsMargins(7, 10, 0, 0);
    sbContentLayout->setSpacing(0);

    sbContentLayout->addWidget(m_systemTree);
    sbContentLayout->addWidget(m_partitionTree);
    sidebarLayout->addWidget(sbContent);

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

        // 统一从模型获取颜色，实现全分类变色联动
        m_currentCategoryColor = index.data(CategoryModel::ColorRole).toString();
        if (m_currentCategoryColor.isEmpty()) m_currentCategoryColor = "#4a90e2";

        if (m_currentFilterType == "category") {
            m_currentFilterValue = index.data(CategoryModel::IdRole).toInt();
            StringUtils::recordRecentCategory(m_currentFilterValue.toInt());
            DatabaseManager::instance().setActiveCategoryId(m_currentFilterValue.toInt());
        } else {
            m_currentFilterValue = -1;
            DatabaseManager::instance().setActiveCategoryId(-1);
        }
        
        applyListTheme(m_currentCategoryColor);
        m_currentPage = 1;
        refreshData();
        
        // 1:1 复刻旧版：点击分类项后，切换到底部“分类筛选”输入框
        m_bottomStackedWidget->setCurrentIndex(0);
    };

    connect(m_systemTree, &QTreeView::clicked, this, [this, onSelectionChanged](const QModelIndex& idx){ onSelectionChanged(m_systemTree, idx); });
    connect(m_partitionTree, &QTreeView::clicked, this, [this, onSelectionChanged](const QModelIndex& idx){ onSelectionChanged(m_partitionTree, idx); });

    // [USER_REQUEST] 2026-03-xx 侧边栏分类展开拦截逻辑 (针对快速窗口)
    // 如果该主分类处于锁定状态，严禁其伸展出子分类。
    auto onExpanded = [this](const QModelIndex& index) {
        auto* tree = qobject_cast<QTreeView*>(sender());
        if (!tree) return;
        // 关键点：即使在搜索过滤状态下（通过代理模型），IdRole 依然有效
        int catId = index.data(CategoryModel::IdRole).toInt();
        if (catId > 0 && DatabaseManager::instance().isCategoryLocked(catId)) {
            // 立即强制收起，阻止子分类显示
            tree->collapse(index);
        }
    };
    connect(m_systemTree, &QTreeView::expanded, this, onExpanded);
    connect(m_partitionTree, &QTreeView::expanded, this, onExpanded);

    // 拖拽逻辑...
    auto onNotesDropped = [this](const QList<int>& ids, const QModelIndex& targetIndex) {
        if (!targetIndex.isValid()) return;
        QString type = targetIndex.data(CategoryModel::TypeRole).toString();
        
        if (type == "category") {
            int catId = targetIndex.data(CategoryModel::IdRole).toInt();
            DatabaseManager::instance().moveNotesToCategory(ids, catId);
            ActionRecorder::instance().recordMoveToCategory(catId);
            StringUtils::recordRecentCategory(catId);
        } else if (type == "uncategorized") {
            DatabaseManager::instance().moveNotesToCategory(ids, -1);
            ActionRecorder::instance().recordMoveToCategory(-1);
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

    m_listStack->addWidget(m_listView);
    m_listStack->addWidget(m_lockWidget);

    // [NEW] 2026-04-xx 按照用户要求：集成高级筛选器包装器
    m_filterWrapper = new QFrame();
    m_filterWrapper->setObjectName("FilterWrapper");
    // [MODIFIED] 2026-04-xx 按照用户要求：直接照搬侧边栏分类宽度的参数 (163px)，但不锁定宽度
    m_filterWrapper->setMinimumWidth(163);
    m_filterWrapper->setStyleSheet("QFrame#FilterWrapper { background-color: #1e1e1e; border: none; }");
    auto* fwLayout = new QVBoxLayout(m_filterWrapper);
    fwLayout->setContentsMargins(0, 0, 0, 0);
    fwLayout->setSpacing(0);

    m_filterPanel = new FilterPanel(this);
    m_filterPanel->setStyleSheet("background: transparent; border: none;");
    connect(m_filterPanel, &FilterPanel::filterChanged, this, &QuickWindow::refreshData);
    fwLayout->addWidget(m_filterPanel);
    m_filterWrapper->hide();

    // [CRITICAL] 2026-04-05 按照用户要求：立即恢复原始物理布局顺序，并保持筛选器在中间
    // 笔记列表 (Index 0) -> 高级筛选 (Index 1) -> 侧边栏 (Index 2)
    m_splitter->addWidget(listWrapper);
    m_splitter->addWidget(m_filterWrapper);
    m_splitter->addWidget(m_sidebarWrapper);

    // 2026-03-13 修复逻辑：监听 Splitter 移动，实时更新焦点线状态
    connect(m_splitter, &QSplitter::splitterMoved, this, &QuickWindow::updateFocusLines);
    m_splitter->setCollapsible(0, false); // 禁止折叠列表区域
    m_splitter->setCollapsible(1, false); // 禁止折叠筛选器
    m_splitter->setCollapsible(2, false); // 禁止折叠侧边栏区域
    m_splitter->setStretchFactor(0, 1);   // 确保左侧列表区域随窗口拉伸
    m_splitter->setStretchFactor(1, 0);
    m_splitter->setStretchFactor(2, 0); 
    m_splitter->setSizes({550, 163, 163});
    leftLayout->addWidget(m_splitter);

    // --- 底部状态栏与标签输入框 ---
    auto* bottomLayout = new QHBoxLayout();
    bottomLayout->setContentsMargins(2, 0, 10, 5);
    bottomLayout->setSpacing(10);

    m_statusLabel = new QLabel("当前分类: 全部数据");
    m_statusLabel->setStyleSheet("font-size: 11px; color: #888;");
    m_statusLabel->setFixedHeight(32);
    bottomLayout->addWidget(m_statusLabel);

    // 动态堆栈管理两个输入框
    m_bottomStackedWidget = new QStackedWidget();
    m_bottomStackedWidget->setFixedHeight(32);

    // 1. 分类过滤输入框
    m_catSearchEdit = new SearchLineEdit();
    m_catSearchEdit->setHistoryKey("CategoryFilterHistory");
    m_catSearchEdit->setHistoryTitle("分类筛选历史");
    m_catSearchEdit->setPlaceholderText("筛选分类...");
    m_catSearchEdit->setClearButtonEnabled(true);

    // 应用漏斗过滤图标
    QAction* filterIconAction = new QAction(IconHelper::getIcon("filter_funnel", "#888"), "", m_catSearchEdit);
    m_catSearchEdit->addAction(filterIconAction, QLineEdit::LeadingPosition);

    m_catSearchEdit->setStyleSheet(
        "QLineEdit { background-color: rgba(255, 255, 255, 0.05); "
        "border: 1px solid rgba(255, 255, 255, 0.1); "
        "border-radius: 6px; "
        "padding: 4px 12px 4px 0px; " // 同步手动修改，图标文字零间距
        "font-size: 12px; "
        "color: #EEE; } "
        "QLineEdit:focus { border-color: #4FACFE; background-color: rgba(255, 255, 255, 0.08); }"
    );
    connect(m_catSearchEdit, &QLineEdit::textChanged, this, [this](const QString& text){
        // 仅对“我的分类”执行过滤，固定分类保持常驻显示
        m_partitionProxyModel->setFilterFixedString(text);
        safeExpandPartitionTree();
    });

    connect(m_catSearchEdit, &QLineEdit::returnPressed, this, [this](){
        QString text = m_catSearchEdit->text().trimmed();
        if (!text.isEmpty()) {
            m_catSearchEdit->addHistoryEntry(text);
        }
    });

    // 2. 标签绑定输入框
    m_tagEdit = new ClickableLineEdit();
    m_tagEdit->setPlaceholderText("输入标签添加... (双击显示历史)");
    m_tagEdit->setStyleSheet(
        "QLineEdit { background-color: rgba(255, 255, 255, 0.05); "
        "border: 1px solid rgba(255, 255, 255, 0.1); "
        "border-radius: 6px; "
        "padding: 6px 12px; "
        "font-size: 12px; "
        "color: #EEE; } "
        "QLineEdit:focus { border-color: #4a90e2; background-color: rgba(255, 255, 255, 0.08); } "
        "QLineEdit:disabled { background-color: transparent; border: 1px solid #333; color: #666; }"
    );
    connect(m_tagEdit, &QLineEdit::returnPressed, this, &QuickWindow::handleTagInput);
    connect(m_tagEdit, &ClickableLineEdit::doubleClicked, this, [this](){
        this->openTagSelector();
    });

    m_bottomStackedWidget->addWidget(m_catSearchEdit); // Index 0: 分类筛选
    m_bottomStackedWidget->addWidget(m_tagEdit);       // Index 1: 标签绑定
    
    bottomLayout->addWidget(m_bottomStackedWidget, 1);
    leftLayout->addLayout(bottomLayout);

    containerLayout->addWidget(leftContent);

    // --- 右侧垂直工具栏 (Custom Toolbar Implementation) ---
    // 【核心修正】根据图二 1:1 还原，压缩宽度，修正图标名，重构分页布局
    
    QWidget* customToolbar = new QWidget(this);
    // 2026-03-xx 按照用户要求：补全工具栏鼠标追踪属性，解决焦点流转时的“傻逼高亮逻辑”
    customToolbar->setObjectName("customToolbar");
    customToolbar->setMouseTracking(true);
    customToolbar->setAttribute(Qt::WA_Hover);
    customToolbar->setFixedWidth(36); // 压缩至 36px，提升精致感
    customToolbar->setStyleSheet(
        "QWidget { background-color: #252526; border-top-right-radius: 10px; border-bottom-right-radius: 10px; border-left: 1px solid #333; }"
        "QPushButton { border: none; border-radius: 4px; background: transparent; padding: 0px; margin: 0px; outline: none; }"
        "QPushButton:hover { background-color: #3e3e42; }"
        "QPushButton#btnClose { background-color: #E81123; }"
        "QPushButton#btnClose:hover { background-color: #D71520; }"
        "QPushButton:pressed { background-color: #2d2d2d; }"
        "QLabel { color: #888; font-size: 11px; }"
        "QLineEdit { background: transparent; border: 1px solid #444; border-radius: 4px; color: white; font-size: 11px; font-weight: bold; padding: 0; }"
    );
    
    QVBoxLayout* toolLayout = new QVBoxLayout(customToolbar);
    toolLayout->setContentsMargins(4, 8, 4, 8); // 对齐 Python 版边距
    // 2026-03-xx 按照用户要求，工具栏间距统一设定为 6px。
    // 为确保精准性，采用 setSpacing(0) 配合显式 addSpacing(6) 模式，规避布局器默认间距叠加风险。
    toolLayout->setSpacing(0); 

    // 辅助函数：从 ShortcutManager 获取格式化后的快捷键字符串 (例如: " （Alt + Q）")
    auto getScHint = [](const QString& id) -> QString {
        QKeySequence seq = ShortcutManager::instance().getShortcut(id);
        if (seq.isEmpty()) return "";
        // 优化显示格式，确保符号间有空格
        QString keyText = seq.toString(QKeySequence::NativeText);
        keyText.replace("+", " + ");
        return QString(" （%1）").arg(keyText);
    };

    // 辅助函数：创建图标按钮，支持旋转
    auto createToolBtn = [this, getScHint](QString iconName, QString color, QString tooltip, QString scId = "", int rotate = 0) {
        QPushButton* btn = new QPushButton();
        QIcon icon = IconHelper::getIcon(iconName, color);
        if (rotate != 0) {
            QPixmap pix = icon.pixmap(28, 28);
            QTransform trans;
            trans.rotate(rotate);
            btn->setIcon(QIcon(pix.transformed(trans, Qt::SmoothTransformation)));
        } else {
            btn->setIcon(icon);
        }
        btn->setIconSize(QSize(18, 18)); // 统一标准化为 18px 图标，增强呼吸感与精致度
        btn->setFixedSize(24, 24); // [MODIFIED] 按照用户要求：按钮高亮区域缩小至 24px
        
        // 动态合并快捷键提示
        QString fullTip = tooltip;
        if (!scId.isEmpty()) fullTip += getScHint(scId);
        btn->setProperty("tooltipText", fullTip); btn->installEventFilter(this);
        
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFocusPolicy(Qt::NoFocus);
        // 2026-03-xx 按照用户要求：按钮强制开启全域追踪，确保悬停高亮即时响应
        btn->setMouseTracking(true);
        btn->setAttribute(Qt::WA_Hover);
        btn->installEventFilter(this);
        return btn;
    };

    // 1. 顶部按钮组
    // 2026-03-xx 按照用户要求，严格执行“关闭 → 最大化 → 最小化 → 置顶 → 编辑”从上到下的物理顺序。
    
    // [1] 关闭
    // 2026-04-xx 按照用户要求：关闭按钮常驻红底白字
    QPushButton* btnClose = createToolBtn("close", "#FFFFFF", "关闭", "qw_close");
    btnClose->setObjectName("btnClose");
    connect(btnClose, &QPushButton::clicked, this, &QuickWindow::hide);

    // [2] 最大化/还原 (2026-04-xx 按照宪法补全按钮链)
    m_btnMax = createToolBtn("maximize", "#aaaaaa", "最大化", "qw_maximize");
    m_btnMax->setObjectName("btnMax");
    connect(m_btnMax, &QPushButton::clicked, this, &QuickWindow::toggleMaximize);

    // [3] 最小化
    QPushButton* btnMin = createToolBtn("minimize", "#aaaaaa", "最小化");
    btnMin->setProperty("tooltipText", "最小化"); btnMin->installEventFilter(this);
    btnMin->setObjectName("btnMin");
    connect(btnMin, &QPushButton::clicked, this, &QuickWindow::showMinimized);

    // [4] 置顶
    QPushButton* btnPin = createToolBtn("pin_tilted", "#aaaaaa", "置顶", "qw_stay_on_top");
    btnPin->setCheckable(true);
    btnPin->setObjectName("btnPin");
    // 2026-03-xx 按照用户要求，修改置顶按钮样式：置顶后背景为浅灰色。
    btnPin->setStyleSheet("QPushButton:checked { background-color: rgba(255, 255, 255, 0.1); }");
    if (windowFlags() & Qt::WindowStaysOnTopHint) {
        btnPin->setChecked(true);
        btnPin->setIcon(IconHelper::getIcon("pin_vertical", "#FF551C"));
    }
    connect(btnPin, &QPushButton::toggled, this, &QuickWindow::toggleStayOnTop);

    // [5] 编辑/新建 (回归扁平灰色风格，隐藏菜单箭头并强制居中)
    QPushButton* btnAdd = createToolBtn("add", "#aaaaaa", "新建数据", "qw_new_idea");
    btnAdd->setObjectName("btnAdd");
    btnAdd->setStyleSheet("QPushButton::menu-indicator { width: 0px; image: none; }");
    
    QMenu* addMenu = new QMenu(this);
    IconHelper::setupMenu(addMenu);
    addMenu->setStyleSheet("QMenu { background-color: #2D2D2D; color: #EEE; border: 1px solid #444; padding: 4px; } "
                           "QMenu::item { padding: 6px 10px 6px 10px; border-radius: 3px; } "
                           "QMenu::icon { margin-left: 6px; } "
                           "QMenu::item:selected { background-color: #3E3E42; }");
    
    addMenu->addAction(IconHelper::getIcon("add", "#aaaaaa", 18), "新建数据", [this](){
        this->doNewIdea();
    });
    
    QMenu* createByLineMenu = addMenu->addMenu(IconHelper::getIcon("list_ul", "#aaaaaa", 18), "按行创建数据");
    createByLineMenu->setStyleSheet(addMenu->styleSheet());
    createByLineMenu->addAction("从复制的内容创建", [this](){
        this->doCreateByLine(true);
    });
    createByLineMenu->addAction("从选中数据创建", [this](){
        this->doCreateByLine(false);
    });
    
    btnAdd->setMenu(addMenu);
    connect(btnAdd, &QPushButton::clicked, [btnAdd](){
        btnAdd->showMenu();
    });

    // 其余功能按钮
    // 2026-03-13 按照用户要求：eye 图标颜色统一为 #41F2F2
    // 2026-04-xx 按照用户要求：修改按钮 ID 以便正确关联 Alt + W 快捷键提示
    QPushButton* btnSidebar = createToolBtn("eye", "#41F2F2", "显示/隐藏侧边栏", "qw_sidebar");
    btnSidebar->setObjectName("btnSidebar");
    btnSidebar->setCheckable(true);
    btnSidebar->setChecked(true);
    btnSidebar->setStyleSheet("QPushButton:checked { background-color: rgba(255, 255, 255, 0.1); }");
    // [MODIFIED] 2026-03-xx 恢复左键单击切换逻辑
    connect(btnSidebar, &QPushButton::clicked, this, &QuickWindow::toggleSidebar);

    // [NEW] 2026-04-xx 按照用户要求：高级筛选器切换按钮 (Ctrl+E)
    QPushButton* btnFilter = createToolBtn("filter", "#f1c40f", "显示/隐藏高级筛选", "qw_filter");
    btnFilter->setObjectName("btnFilter");
    btnFilter->setCheckable(true);
    btnFilter->setChecked(false);
    btnFilter->setStyleSheet("QPushButton:checked { background-color: rgba(255, 255, 255, 0.1); }");
    connect(btnFilter, &QPushButton::clicked, this, &QuickWindow::toggleFilter);

    // 用户要求：为刷新按钮添加 F5 快捷键提示
    QPushButton* btnRefresh = createToolBtn("refresh", "#aaaaaa", "刷新", "qw_refresh");
    btnRefresh->setObjectName("btnRefresh");
    connect(btnRefresh, &QPushButton::clicked, this, &QuickWindow::refreshData);

    // [NEW] 2026-04-xx 按照用户要求：新增全面板联动折叠按钮，支持选中态锁定高亮。锁定常驻识别色为蓝色 #3A90FF。
    m_btnToggleAll = createToolBtn("sidebar_open_filled", "#3A90FF", "联动展开/隐藏侧边栏与高级筛选", "qw_toggle_all_panels");
    m_btnToggleAll->setObjectName("btnToggleAll");
    m_btnToggleAll->setCheckable(true);
    m_btnToggleAll->setStyleSheet("QPushButton:checked { background-color: rgba(255, 255, 255, 0.1); }");
    connect(m_btnToggleAll, &QPushButton::clicked, this, &QuickWindow::toggleAllPanels);

    m_btnAutoCat = createToolBtn("clipboard_auto", "#aaaaaa", "剪贴板自动归档到当前分类");
    m_btnAutoCat->setObjectName("btnAutoCat");
    m_btnAutoCat->setCheckable(true);
    m_btnAutoCat->setChecked(DatabaseManager::instance().isAutoCategorizeEnabled());
    updateAutoCategorizeButton();
    connect(m_btnAutoCat, &QPushButton::clicked, this, [this](bool checked){
        auto& db = DatabaseManager::instance();
        db.setAutoCategorizeEnabled(checked);
        if (checked) {
            int catId = db.extensionTargetCategoryId();
            QString catName = (catId > 0) ? db.getCategoryNameById(catId) : "未分类";
            ToolTipOverlay::instance()->showText(QCursor::pos(), QString("<b style='color: #00A650;'>[OK] 自动归档已开启 （%1）</b>").arg(catName));
        } else {
            ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #aaaaaa;'>[OFF] 自动归档已关闭</b>");
        }
    });



    QPushButton* btnLock = createToolBtn("lock_secure", "#aaaaaa", "锁定应用", "qw_lock_app");
    btnLock->setObjectName("btnLock");
    connect(btnLock, &QPushButton::clicked, this, &QuickWindow::doGlobalLock);

    // 2026-03-xx 按照用户要求，严格执行“关闭 → 最大化 → 最小化 → 置顶 → 编辑”从上到下的物理顺序。
    toolLayout->addWidget(btnClose, 0, Qt::AlignHCenter);
    toolLayout->addSpacing(4);
    toolLayout->addWidget(m_btnMax, 0, Qt::AlignHCenter);
    toolLayout->addSpacing(4);
    toolLayout->addWidget(btnMin, 0, Qt::AlignHCenter);
    toolLayout->addSpacing(4);
    toolLayout->addWidget(btnPin, 0, Qt::AlignHCenter);
    toolLayout->addSpacing(4);
    toolLayout->addWidget(btnAdd, 0, Qt::AlignHCenter);
    toolLayout->addSpacing(4);

    // 2. 其余功能按钮区
    toolLayout->addWidget(btnSidebar, 0, Qt::AlignHCenter);
    toolLayout->addSpacing(4);
    toolLayout->addWidget(btnFilter, 0, Qt::AlignHCenter);
    toolLayout->addSpacing(4);
    toolLayout->addWidget(m_btnToggleAll, 0, Qt::AlignHCenter); // 插入位置：btnFilter 下方
    toolLayout->addSpacing(4);
    toolLayout->addWidget(btnRefresh, 0, Qt::AlignHCenter);
    toolLayout->addSpacing(4);
    toolLayout->addWidget(m_btnAutoCat, 0, Qt::AlignHCenter);
    toolLayout->addSpacing(4);
    toolLayout->addSpacing(4);
    toolLayout->addWidget(btnLock, 0, Qt::AlignHCenter);
    toolLayout->addSpacing(4);

    toolLayout->addStretch();

    // 3. 分页区 (完全复刻图二布局：箭头+输入框+下方总数)
    QPushButton* btnPrev = createToolBtn("nav_prev", "#aaaaaa", "上一页", "qw_prev_page", 90);
    btnPrev->setObjectName("btnPrev");
    btnPrev->setFixedSize(28, 20);
    connect(btnPrev, &QPushButton::clicked, [this](){
        if (m_currentPage > 1) { m_currentPage--; refreshData(); }
    });

    m_pageInput = new QLineEdit("1");
    m_pageInput->setObjectName("pageInput");
    m_pageInput->setAlignment(Qt::AlignCenter);
    m_pageInput->setFixedSize(24, 20);
    m_pageInput->installEventFilter(this);
    connect(m_pageInput, &QLineEdit::returnPressed, [this](){
        int p = m_pageInput->text().toInt();
        if (p > 0 && p <= m_totalPages) { m_currentPage = p; refreshData(); }
    });

    QLabel* totalLabel = new QLabel("1");
    totalLabel->setObjectName("totalLabel");
    totalLabel->setAlignment(Qt::AlignCenter);
    totalLabel->setStyleSheet("color: #666; font-size: 10px; border: none; background: transparent;");

    QPushButton* btnNext = createToolBtn("nav_next", "#aaaaaa", "下一页", "qw_next_page", 90);
    btnNext->setObjectName("btnNext");
    btnNext->setFixedSize(28, 20);
    connect(btnNext, &QPushButton::clicked, [this](){
        if (m_currentPage < m_totalPages) { m_currentPage++; refreshData(); }
    });

    toolLayout->addWidget(btnPrev, 0, Qt::AlignHCenter);
    toolLayout->addSpacing(6);
    toolLayout->addWidget(m_pageInput, 0, Qt::AlignHCenter);
    toolLayout->addSpacing(6);
    toolLayout->addWidget(totalLabel, 0, Qt::AlignHCenter);
    toolLayout->addSpacing(6);
    toolLayout->addWidget(btnNext, 0, Qt::AlignHCenter);

    toolLayout->addSpacing(20); // 增加分页与标题间距

    // 4. 垂直标题 "快速笔记"
    QLabel* verticalTitle = new QLabel("快\n速\n笔\n记");
    verticalTitle->setAlignment(Qt::AlignCenter);
    verticalTitle->setStyleSheet("color: #444; font-size: 11px; font-weight: bold; border: none; background: transparent; line-height: 1.1;");
    toolLayout->addWidget(verticalTitle, 0, Qt::AlignHCenter);

    toolLayout->addSpacing(12);

    // 6. 底部 Logo (修正为 zap 图标以匹配图二蓝闪电)
    QPushButton* btnLogo = createToolBtn("zap", "#3A90FF", "RapidNotes");
    // [MODIFIED] 按照用户要求：圈选区域外（Logo）保持原有的 28x28 规格不变
    btnLogo->setFixedSize(28, 28);
    btnLogo->setCursor(Qt::ArrowCursor);
    btnLogo->setStyleSheet("background: transparent; border: none;");
    toolLayout->addWidget(btnLogo, 0, Qt::AlignHCenter);

    containerLayout->addWidget(customToolbar);
    
    // m_toolbar = new QuickToolbar(this); // 移除旧代码
    // containerLayout->addWidget(m_toolbar); // 移除旧代码
    
    mainLayout->addWidget(container);
    
    // [PERFORMANCE] 预热预览窗单例，消除首次通过空格键打开时的构造延迟
    QTimer::singleShot(500, []() {
        QuickPreview::instance();
    });
    
    // 初始大小和最小大小
    setMinimumSize(400, 300);

    auto* preview = QuickPreview::instance();
    connect(preview, &QuickPreview::editRequested, this, [this](int id){
        auto* preview = QuickPreview::instance();
        if (!preview->caller() || preview->caller()->window() != this) return;
        this->doEditNote(id);
    });
    connect(preview, &QuickPreview::prevRequested, this, [this](){
        auto* preview = QuickPreview::instance();
        if (!preview->caller() || preview->caller()->window() != this) return;
        QModelIndex current = m_listView->currentIndex();
        if (!current.isValid() || m_model->rowCount() == 0) return;

        int catId = current.data(NoteModel::CategoryIdRole).toInt();
        int row = current.row();
        int count = m_model->rowCount();
        
        for (int i = 1; i <= count; ++i) {
            int prevRow = (row - i + count) % count;
            QModelIndex idx = m_model->index(prevRow, 0);
            if (idx.data(NoteModel::CategoryIdRole).toInt() == catId) {
                m_listView->setCurrentIndex(idx);
                m_listView->scrollTo(idx);
                updatePreviewContent();
                if (prevRow > row) {
                    ToolTipOverlay::instance()->showText(QCursor::pos(), "已回环至列表末尾相同分类");
                }
                return;
            }
        }
    });
    connect(preview, &QuickPreview::nextRequested, this, [this](){
        auto* preview = QuickPreview::instance();
        if (!preview->caller() || preview->caller()->window() != this) return;
        QModelIndex current = m_listView->currentIndex();
        if (!current.isValid() || m_model->rowCount() == 0) return;

        int catId = current.data(NoteModel::CategoryIdRole).toInt();
        int row = current.row();
        int count = m_model->rowCount();

        for (int i = 1; i <= count; ++i) {
            int nextRow = (row + i) % count;
            QModelIndex idx = m_model->index(nextRow, 0);
            if (idx.data(NoteModel::CategoryIdRole).toInt() == catId) {
                m_listView->setCurrentIndex(idx);
                m_listView->scrollTo(idx);
                updatePreviewContent();
                if (nextRow < row) {
                    ToolTipOverlay::instance()->showText(QCursor::pos(), "已回环至列表起始相同分类");
                }
                return;
            }
        }
    });
    connect(preview, &QuickPreview::historyNavigationRequested, this, [this](int id){
        auto* preview = QuickPreview::instance();
        if (!preview->caller() || preview->caller()->window() != this) return;
        for (int i = 0; i < m_model->rowCount(); ++i) {
            QModelIndex idx = m_model->index(i, 0);
            if (idx.data(NoteModel::IdRole).toInt() == id) {
                m_listView->setCurrentIndex(idx);
                m_listView->scrollTo(idx);
                return;
            }
        }
        QVariantMap note = DatabaseManager::instance().getNoteById(id);
        if (!note.isEmpty()) {
            preview->showPreview(note, preview->pos(), "", m_listView);
        }
    });
    m_listView->installEventFilter(this);
    m_systemTree->installEventFilter(this);
    m_partitionTree->installEventFilter(this);
    m_searchEdit->installEventFilter(this);
    m_catSearchEdit->installEventFilter(this);
    m_tagEdit->installEventFilter(this);

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

    // 监听列表选择变化，动态切换输入框状态及显示内容
    connect(m_listView->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this](){
        auto selected = m_listView->selectionModel()->selectedIndexes();
        if (selected.isEmpty()) {
            // 切换到分类筛选页
            m_bottomStackedWidget->setCurrentIndex(0);
            m_tagEdit->setEnabled(false);
        } else {
            // [CRITICAL] 只有当列表具有焦点（用户主动操作）时，才在选中笔记后切换到“标签绑定”页
            // 这可以防止 refreshData() 自动恢复选中状态时导致的非预期 UI 切换
            if (m_listView->hasFocus()) {
                m_bottomStackedWidget->setCurrentIndex(1);
            }

            m_tagEdit->setEnabled(true);
            m_tagEdit->setPlaceholderText(selected.size() == 1 ? "标签... (双击显示历史)" : "批量添加标签... (双击显示历史)");
            
            // [CRITICAL] 全局预览联动逻辑
            auto* preview = QuickPreview::instance();
            if (preview->isVisible()) {
                updatePreviewContent();
            }
        }
    });

    setupShortcuts();
    connect(&ShortcutManager::instance(), &ShortcutManager::shortcutsChanged, this, &QuickWindow::updateShortcuts);
    updateAppLockStatus();
    restoreState();
    // 2026-04-xx 按照用户要求：恢复状态后立即强制校准紧凑型布局宽度，避免默认开启过宽
    updateLayoutWidth();
    refreshData();
    applyListTheme(""); // 【核心修复】初始化时即应用深色主题
    updateShortcuts();  // [MODIFIED] 按照用户要求：强制同步一次 ToolTip，确保显示包含 (Alt + W) 的完整提示
    setupAppLock();

    // [NEW] 2026-03-20 按照用户要求：初始化系统闲置锁定定时器，用于执行 30 秒自动上锁功能
    m_idleLockTimer = new QTimer(this);
    m_idleLockTimer->setInterval(5000); // 每 5 秒检查一次闲置状态
    connect(m_idleLockTimer, &QTimer::timeout, this, &QuickWindow::checkIdleLock);
    m_idleLockTimer->start();
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
            updateAppLockStatus();
            // 2026-03-xx 按照用户要求：解锁后焦点自动锁定到列表，支持即刻回车发送的高效流转
            m_listView->setFocus();
            if (m_model->rowCount() > 0 && !m_listView->currentIndex().isValid()) {
                m_listView->setCurrentIndex(m_model->index(0, 0));
            }
        });
        
        lock->show();
        lock->raise();
    }
}

void QuickWindow::saveState() {
    QSettings settings("RapidNotes", "QuickWindow");
    settings.setValue("geometry", saveGeometry());
    settings.setValue("splitter", m_splitter->saveState());
    settings.setValue("sidebarHidden", m_sidebarWrapper->isHidden());
    settings.setValue("stayOnTop", m_isStayOnTop);
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
        m_sidebarWrapper->setHidden(hidden);
        
        // 同步刷新眼睛图标状态
        auto* btnSidebar = findChild<QPushButton*>("btnSidebar");
        if (btnSidebar) {
            bool visible = !hidden;
            btnSidebar->setChecked(visible);
            // 2026-03-13 按照用户要求：eye 图标颜色统一为 #41F2F2
            btnSidebar->setIcon(IconHelper::getIcon("eye", "#41F2F2"));
        }
    }
    if (settings.contains("stayOnTop")) {
        toggleStayOnTop(settings.value("stayOnTop").toBool());
    }
}

void QuickWindow::setupShortcuts() {
    auto add = [&](const QString& id, std::function<void()> func) {
        auto* sc = new QShortcut(ShortcutManager::instance().getShortcut(id), this, func);
        sc->setProperty("id", id);
        m_shortcuts.append(sc);
    };

    add("qw_search", [this](){ m_searchEdit->setFocus(); m_searchEdit->selectAll(); });

    // [PROFESSIONAL] 将删除快捷键绑定到列表，允许侧边栏通过 eventFilter 独立处理 Del 键
    auto* delSoftSc = new QShortcut(ShortcutManager::instance().getShortcut("qw_delete_soft"), m_listView, [this](){ doDeleteSelected(false); }, Qt::WidgetShortcut);
    delSoftSc->setProperty("id", "qw_delete_soft");
    m_shortcuts.append(delSoftSc);
    auto* delHardSc = new QShortcut(ShortcutManager::instance().getShortcut("qw_delete_hard"), m_listView, [this](){ doDeleteSelected(true); }, Qt::WidgetShortcut);
    delHardSc->setProperty("id", "qw_delete_hard");
    m_shortcuts.append(delHardSc);

    add("qw_favorite", [this](){ doToggleFavorite(); });
    // [PROFESSIONAL] 使用 WidgetShortcut 并绑定到列表，防止预览窗打开后发生快捷键回环触发
    auto* previewSc = new QShortcut(ShortcutManager::instance().getShortcut("qw_preview"), m_listView, [this](){ doPreview(); }, Qt::WidgetShortcut);
    previewSc->setProperty("id", "qw_preview");
    m_shortcuts.append(previewSc);

    add("qw_pin", [this](){ doTogglePin(); });
    add("qw_close", [this](){ hide(); });
    add("qw_new_idea", [this](){ doNewIdea(); });
    add("qw_select_all", [this](){ m_listView->selectAll(); });
    add("qw_extract", [this](){ doExtractContent(); });
    
    // 2026-03-xx 按照用户要求，彻底移除 QShortcut 形式的 Alt+Up/Down 绑定，改在 eventFilter 中统一分发处理，
    // 以解决侧边栏分类排序逻辑被列表快捷键抢占冲突的问题。

    add("qw_lock_cat", [this](){
        int catId = -1;
        // 1. 优先获取侧边栏当前选中的分类
        QModelIndex sidebarIdx = m_partitionTree->currentIndex();
        if (sidebarIdx.isValid() && sidebarIdx.data(CategoryModel::TypeRole).toString() == "category") {
            catId = sidebarIdx.data(CategoryModel::IdRole).toInt();
        }
        // 2. 若侧边栏未选中具体分类，则回退到当前视图对应的分类
        if (catId == -1 && m_currentFilterType == "category" && m_currentFilterValue != -1) {
            catId = m_currentFilterValue.toInt();
        }

        if (catId != -1) {
            DatabaseManager::instance().lockCategory(catId);
            // 锁定后若处于该分类视图，强制切出
            if (m_currentFilterType == "category" && m_currentFilterValue == catId) {
                m_currentFilterType = "all";
                m_currentFilterValue = -1;
            }
            refreshSidebar();
            refreshData();
            ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #f39c12;'>[OK] 分类已立即锁定</b>");
        }
    });

    add("qw_lock_all_cats", [this](){
        // 2026-03-xx 按照用户要求：Ctrl+Shift+S 闪速锁定所有分类
        DatabaseManager::instance().lockAllCategories();
        refreshSidebar();
        refreshData();
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #2ecc71;'>[OK] 所有分类已闪速锁定</b>");
    });
    // [MODIFIED] 2026-03-xx 切换加锁分类显示/隐藏逻辑已迁移至 eventFilter 物理层，避免被快捷键系统抢占。
    // add("qw_toggle_locked_visibility", ...);
    add("qw_stay_on_top", [this](){ toggleStayOnTop(!m_isStayOnTop); });

    add("qw_edit", [this](){ doEditSelected(); });
    add("qw_sidebar", [this](){ 
        // 2026-03-xx 按照用户指令：Alt+W 触发，执行显隐切换
        toggleSidebar(); 
    });
    add("qw_maximize", [this](){ toggleMaximize(); });
    add("qw_filter_toggle_groups", [this](){
        // 2026-04-xx 按照用户指令：Ctrl+G 用于切换高级筛选器各组的折叠/展开
        if (m_filterWrapper && m_filterWrapper->isVisible() && m_filterPanel) {
            m_filterPanel->toggleAllGroups();
        }
    });

    // 2026-04-xx 按照用户指令：新增 Ctrl+R 快捷键，用于一键联动展开/折叠所有侧面板
    new QShortcut(QKeySequence("Ctrl+R"), this, [this](){
        this->toggleAllPanels();
    });

    add("qw_prev_page", [this](){ if(m_currentPage > 1) { m_currentPage--; refreshData(); } });
    add("qw_next_page", [this](){ if(m_currentPage < m_totalPages) { m_currentPage++; refreshData(); } });
    // 用户要求：绑定刷新快捷键逻辑
    add("qw_refresh", [this](){ refreshData(); });
    add("qw_copy_tags", [this](){ doCopyTags(); });
    add("qw_paste_tags", [this](){ doPasteTags(); });
    add("qw_repeat_action", [this](){ doRepeatAction(); }); // [USER_REQUEST] 2026-03-14 F4重复上一次操作
    add("qw_show_all", [this](){
        m_currentFilterType = "all";
        m_currentFilterValue = -1;
        m_currentPage = 1;
        
        // 清除侧边栏选中状态
        m_systemTree->selectionModel()->clearSelection();
        m_systemTree->setCurrentIndex(QModelIndex());
        m_partitionTree->selectionModel()->clearSelection();
        m_partitionTree->setCurrentIndex(QModelIndex());
        
        updatePartitionStatus("");
        applyListTheme("");
        refreshData();
        
        ToolTipOverlay::instance()->showText(QCursor::pos(), "[OK] 已切换至全部数据");
    });
    
    for (int i = 0; i <= 5; ++i) {
        add(QString("qw_rating_%1").arg(i), [this, i](){ doSetRating(i); });
    }
}



void QuickWindow::updateAutoCategorizeButton() {
    if (!m_btnAutoCat) return;
    auto& db = DatabaseManager::instance();
    bool enabled = db.isAutoCategorizeEnabled();
    m_btnAutoCat->setChecked(enabled);
    m_btnAutoCat->setIcon(IconHelper::getIcon(enabled ? "switch_on" : "switch_off", enabled ? "#00A650" : "#aaaaaa"));
    
    if (enabled) {
        // [MODIFIED] 使用 extensionTargetCategoryId (即右键菜单指定的分类) 而不是当前选中的分类
        int catId = db.extensionTargetCategoryId();
        QString catName = (catId > 0) ? db.getCategoryNameById(catId) : "未分类";
        m_btnAutoCat->setProperty("tooltipText", QString("自动归档：开启 （%1）").arg(catName)); m_btnAutoCat->installEventFilter(this);
    } else {
        m_btnAutoCat->setProperty("tooltipText", "自动归档：关闭"); m_btnAutoCat->installEventFilter(this);
    }
}

void QuickWindow::updateAppLockStatus() {
    QPushButton* btnLock = findChild<QPushButton*>("btnLock");
    if (!btnLock) return;

    QSettings settings("RapidNotes", "QuickWindow");
    bool hasPwd = !settings.value("appPassword").toString().isEmpty();

    if (hasPwd) {
        // 已设置密码：绿色 unlock_secure
        btnLock->setIcon(IconHelper::getIcon("unlock_secure", "#00A650"));
    } else {
        // 未设置密码：灰色 lock_secure
        btnLock->setIcon(IconHelper::getIcon("lock_secure", "#aaaaaa"));
    }
}

void QuickWindow::updateShortcuts() {
    for (auto* sc : m_shortcuts) {
        QString id = sc->property("id").toString();
        sc->setKey(ShortcutManager::instance().getShortcut(id));
    }

    // [NEW] 同步更新工具栏按钮的 ToolTip
    auto getScHint = [](const QString& id) -> QString {
        QKeySequence seq = ShortcutManager::instance().getShortcut(id);
        if (seq.isEmpty()) return "";
        QString keyText = seq.toString(QKeySequence::NativeText);
        keyText.replace("+", " + ");
        return QString(" （%1）").arg(keyText);
    };

    auto updateBtnTip = [&](const QString& objName, const QString& baseTip, const QString& scId) {
        QPushButton* btn = findChild<QPushButton*>(objName);
        if (btn) { btn->setProperty("tooltipText", baseTip + getScHint(scId)); btn->installEventFilter(this); }
    };

    updateBtnTip("btnClose", "关闭", "qw_close");
    updateBtnTip("btnMax", "最大化/还原", "qw_maximize");
    updateBtnTip("btnPin", "置顶", "qw_stay_on_top");
    // 2026-04-xx 按照用户要求：简化侧边栏提示文本，使生成的 ToolTip 为“显示/隐藏侧边栏 （Alt + W）”
    updateBtnTip("btnSidebar", "显示/隐藏侧边栏", "qw_sidebar");
    updateBtnTip("btnFilter", "显示/隐藏高级筛选", "qw_filter");

    // 2026-04-xx 按照用户要求：补全联动按钮的快捷键提示文本
    QPushButton* btnToggleAll = findChild<QPushButton*>("btnToggleAll");
    if (btnToggleAll) {
        btnToggleAll->setProperty("tooltipText", "联动显示/隐藏面板 （Ctrl + R）");
        btnToggleAll->installEventFilter(this);
    }

    updateBtnTip("btnLock", "锁定应用", "qw_lock_app");
    // 用户要求：同步更新刷新按钮提示
    updateBtnTip("btnRefresh", "刷新", "qw_refresh");
    updateBtnTip("btnPrev", "上一页", "qw_prev_page");
    updateBtnTip("btnNext", "下一页", "qw_next_page");
}

void QuickWindow::scheduleRefresh() {
    m_refreshTimer->start();
}

void QuickWindow::onNoteAdded(const QVariantMap& note) {
    // 1. 基础状态检查
    if (note.value("is_deleted").toInt() == 1) return; // 刚添加的不应该是已删除，但严谨起见

    // 2. 检查是否符合当前过滤条件
    bool matches = true;
    if (m_currentFilterType == "category") {
        matches = (note.value("category_id").toInt() == m_currentFilterValue.toInt());
    } else if (m_currentFilterType == "untagged") {
        matches = note.value("tags").toString().isEmpty();
    } else if (m_currentFilterType == "bookmark") {
        matches = (note.value("is_favorite").toInt() == 1);
    } else if (m_currentFilterType == "trash") {
        matches = false; // 新产生的笔记不可能在回收站视图下出现
    }
    // "today", "yesterday", "all" 等时间/全局类型通常匹配新笔记

    // 3. 关键词匹配检查 (如果有搜索)
    QString keyword = m_searchEdit->text().trimmed();
    if (matches && !keyword.isEmpty()) {
        QString title = note.value("title").toString();
        QString content = note.value("content").toString();
        QString tags = note.value("tags").toString();
        if (!title.contains(keyword, Qt::CaseInsensitive) && 
            !content.contains(keyword, Qt::CaseInsensitive) && 
            !tags.contains(keyword, Qt::CaseInsensitive)) {
            matches = false;
        }
    }

    // [NEW] 2026-04-xx 按照用户要求：高级筛选器活跃时，采取全量刷新策略以保证精准
    if (matches && m_filterWrapper && !m_filterWrapper->isHidden()) {
        matches = false;
    }
    
    if (matches && m_currentPage == 1) {
        m_model->prependNote(note);
    }
    
    // 依然需要触发侧边栏计数刷新 (节流执行)
    scheduleRefresh();
}

void QuickWindow::refreshData() {
    qDebug() << "[QuickWindow] 开始执行 refreshData()...";
    if (!isVisible()) return;

    // 记忆当前选中的 ID 列表，以便在刷新后恢复多选状态
    QSet<int> selectedIds;
    auto selectedIndices = m_listView->selectionModel()->selectedIndexes();
    for (const auto& idx : selectedIndices) {
        selectedIds.insert(idx.data(NoteModel::IdRole).toInt());
    }
    int lastCurrentId = m_listView->currentIndex().data(NoteModel::IdRole).toInt();

    QString keyword = m_searchEdit->text();
    
    // [NEW] 2026-04-xx 按照用户要求：获取高级筛选器条件
    QVariantMap criteria = m_filterPanel->getCheckedCriteria();
    int totalCount = DatabaseManager::instance().getNotesCount(keyword, m_currentFilterType, m_currentFilterValue, criteria);
    
    const int pageSize = DatabaseManager::DEFAULT_PAGE_SIZE; 
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

    m_listStack->setCurrentWidget(isLocked ? static_cast<QWidget*>(m_lockWidget) : static_cast<QWidget*>(m_listView));

    auto* preview = QuickPreview::instance();
    if (isLocked && preview->isVisible() && preview->caller() && preview->caller()->window() == this) {
        preview->hide();
    }

    m_model->setNotes(isLocked ? QList<QVariantMap>() : DatabaseManager::instance().searchNotes(keyword, m_currentFilterType, m_currentFilterValue, m_currentPage, pageSize, criteria));
    
    // 恢复选中状态 (支持多选恢复)
    if (!selectedIds.isEmpty()) {
        QItemSelection selection;
        for (int i = 0; i < m_model->rowCount(); ++i) {
            QModelIndex idx = m_model->index(i, 0);
            int id = idx.data(NoteModel::IdRole).toInt();
            if (selectedIds.contains(id)) {
                selection.select(idx, idx);
            }
            if (id == lastCurrentId) {
                m_listView->setCurrentIndex(idx);
            }
        }
        if (!selection.isEmpty()) {
            m_listView->selectionModel()->select(selection, QItemSelectionModel::Select | QItemSelectionModel::Rows);
        }
    }

    // 更新工具栏页码 (对齐新版 1:1 布局)
    auto* pageInput = findChild<QLineEdit*>("pageInput");
    if (pageInput) pageInput->setText(QString::number(m_currentPage));
    
    auto* totalLabel = findChild<QLabel*>("totalLabel");
    if (totalLabel) totalLabel->setText(QString::number(m_totalPages));

    // [NEW] 2026-04-xx 按照用户要求：同步更新高级筛选器状态记录
    if (!m_filterWrapper->isHidden()) {
        m_filterPanel->updateStats(keyword, m_currentFilterType, m_currentFilterValue);
    }
}

void QuickWindow::updatePartitionStatus(const QString& name) {
    m_statusLabel->setText(QString("当前分类: %1").arg(name.isEmpty() ? "全部数据" : name));
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
        // [CRITICAL] 锁定：过滤“我的分类”标题行，防止 IdRole=0 引起的异常调用链
        if (partIdx.data(CategoryModel::NameRole).toString() != "我的分类") {
            selectedType = partIdx.data(CategoryModel::TypeRole).toString();
            selectedValue = partIdx.data(CategoryModel::IdRole);
        }
    }

    m_systemModel->refresh();
    m_partitionModel->refresh();
    safeExpandPartitionTree();

    // 恢复选中 (需考虑 ProxyModel 映射)
    if (!selectedType.isEmpty()) {
        if (selectedType != "category") {
            for (int i = 0; i < m_systemModel->rowCount(); ++i) {
                QModelIndex idx = m_systemModel->index(i, 0);
                if (idx.data(CategoryModel::TypeRole).toString() == selectedType &&
                    idx.data(CategoryModel::NameRole) == selectedValue) {
                    m_systemTree->setCurrentIndex(m_systemProxyModel->mapFromSource(idx));
                    break;
                }
            }
        } else {
            std::function<void(const QModelIndex&)> findAndSelect = [&](const QModelIndex& parent) {
                for (int i = 0; i < m_partitionModel->rowCount(parent); ++i) {
                    QModelIndex idx = m_partitionModel->index(i, 0, parent);
                    if (idx.data(CategoryModel::IdRole) == selectedValue) {
                        m_partitionTree->setCurrentIndex(m_partitionProxyModel->mapFromSource(idx));
                        return;
                    }
                    if (m_partitionModel->rowCount(idx) > 0) findAndSelect(idx);
                }
            };
            findAndSelect(QModelIndex());
        }
    }
}

void QuickWindow::safeExpandPartitionTree() {
    // [USER_REQUEST] 2026-03-xx 物理级预防上锁分类展开逻辑
    // 递归遍历代理模型，仅对未上锁的项执行展开。
    std::function<void(const QModelIndex&)> expandRec = [&](const QModelIndex& parent) {
        for (int i = 0; i < m_partitionProxyModel->rowCount(parent); ++i) {
            QModelIndex idx = m_partitionProxyModel->index(i, 0, parent);
            int catId = idx.data(CategoryModel::IdRole).toInt();
            
            // 判定逻辑：必须是有效分类 ID，且数据库判定为未上锁
            if (catId > 0) {
                if (!DatabaseManager::instance().isCategoryLocked(catId)) {
                    m_partitionTree->setExpanded(idx, true);
                    // 只有展开了当前项，才继续递归子项
                    if (m_partitionProxyModel->rowCount(idx) > 0) expandRec(idx);
                } else {
                    // 已上锁，物理上确保其处于折叠状态
                    m_partitionTree->setExpanded(idx, false);
                }
            } else {
                // 非分类项（如标题行），默认允许展开以显示其下的分类
                m_partitionTree->setExpanded(idx, true);
                if (m_partitionProxyModel->rowCount(idx) > 0) expandRec(idx);
            }
        }
    };
    expandRec(QModelIndex());
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
    
    // [CRITICAL] 锁定：激活/打开笔记视为实际操作，必须显式记录访问。严禁移除。
    DatabaseManager::instance().recordAccess(id);

    QString itemType = note.value("item_type").toString();
    QString content = note.value("content").toString();
    QByteArray blob = note.value("data_blob").toByteArray();
    
    if (itemType == "image") {
        QImage img;
        img.loadFromData(blob);
        ClipboardMonitor::instance().skipNext();
        QApplication::clipboard()->setImage(img);
    } else if (itemType == "local_file" || itemType == "local_folder" || itemType == "local_batch" || 
               (QFileInfo(StringUtils::htmlToPlainText(content).trimmed()).exists() && QFileInfo(StringUtils::htmlToPlainText(content).trimmed()).isAbsolute())) {
        
        // [CRITICAL] 锁定：双击/回车智能打开逻辑。支持托管路径及文本中蕴含的绝对路径。
        // 文件系统托管模式或普通文本绝对路径
        QString plainContent = StringUtils::htmlToPlainText(content).trimmed();
        bool isExplicitPath = (itemType == "local_file" || itemType == "local_folder" || itemType == "local_batch");
        
        QString path = isExplicitPath ? content : plainContent;
        QString fullPath = path;
        if (path.startsWith("attachments/")) {
            fullPath = QCoreApplication::applicationDirPath() + "/" + path;
        }

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
            ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #e67e22;'>[!] 文件已丢失或被移动</b>");
        }
    } else if (!blob.isEmpty() && (itemType == "file" || itemType == "folder")) {
        // 2026-03-20 按照用户要求：双击打开存放在数据库中的文件也属于导出行为，必须验证
        if (!verifyExportPermission()) return;

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
                // 2026-03-13 按照用户要求：提示时长缩短为 700ms
                ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #e67e22;'>[!] 原文件已丢失，已复制路径文本</b>", 700);
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
        
        FramelessMessageBox msg(title, text, this);
        
        // 提取 ID 列表以备删除
        QList<int> idsToDelete;
        for (const auto& index : std::as_const(selected)) idsToDelete << index.data(NoteModel::IdRole).toInt();
        
        if (msg.exec() == QDialog::Accepted) {
            if (!idsToDelete.isEmpty()) {
                DatabaseManager::instance().deleteNotesBatch(idsToDelete);
                refreshData();
                refreshSidebar();
                ToolTipOverlay::instance()->showText(QCursor::pos(), QString("<b style='color: #2ecc71;'>[OK] 已永久删除 %1 条数据</b>").arg(idsToDelete.size()));
            }
        }
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
    QWidget* focus = QApplication::focusWidget();
    
    // [USER_REQUEST] 统一快捷键 Alt+D: 焦点在侧边栏则置顶分类，焦点在列表则置顶数据
    if (focus == m_systemTree || focus == m_partitionTree) {
        QModelIndex index = (focus == m_systemTree) ? m_systemTree->currentIndex() : m_partitionTree->currentIndex();
        if (index.isValid()) {
            // [CRITICAL] 处理 ProxyModel 映射，确保在搜索过滤状态下依然能准确获取分类 ID
            QModelIndex srcIdx;
            if (focus == m_systemTree) srcIdx = m_systemProxyModel->mapToSource(index);
            else srcIdx = m_partitionProxyModel->mapToSource(index);
            
            if (srcIdx.isValid() && srcIdx.data(CategoryModel::TypeRole).toString() == "category") {
                int catId = srcIdx.data(CategoryModel::IdRole).toInt();
                DatabaseManager::instance().toggleCategoryPinned(catId);
                refreshSidebar();
                return;
            }
        }
    }

    // 默认执行列表项置顶逻辑
    auto selected = m_listView->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;
    for (const auto& index : std::as_const(selected)) {
        int id = index.data(NoteModel::IdRole).toInt();
        DatabaseManager::instance().toggleNoteState(id, "is_pinned");
    }
    refreshData();
}

void QuickWindow::doNewIdea() {
    // [USER_REQUEST] 新建数据自动归类到当前选中分类
    NoteEditWindow* win = new NoteEditWindow();
    int catId = getCurrentCategoryId();
    if (catId > 0) {
        win->setDefaultCategory(catId);
    }
    connect(win, &NoteEditWindow::noteSaved, this, &QuickWindow::refreshData);
    win->show();
}

void QuickWindow::doCreateByLine(bool fromClipboard) {
    QString text;
    if (fromClipboard) {
        text = QApplication::clipboard()->text();
    } else {
        auto selected = m_listView->selectionModel()->selectedIndexes();
        QStringList contents;
        for (const auto& index : selected) {
            contents << StringUtils::htmlToPlainText(index.data(NoteModel::ContentRole).toString());
        }
        text = contents.join("\n");
    }

    if (text.trimmed().isEmpty()) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #e67e22;'>[!] 没有有效内容可供拆分</b>");
        return;
    }

    QStringList lines = text.split(QRegularExpression("[\\r\\n]+"), Qt::SkipEmptyParts);
    int catId = getCurrentCategoryId();
    
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
    
    refreshData();
    ToolTipOverlay::instance()->showText(QCursor::pos(), QString("<b style='color: #2ecc71;'>[OK] 已成功按行创建 %1 条数据</b>").arg(count));
}

void QuickWindow::doExtractContent() {
    // [MODIFIED] 2026-03-11 按照用户要求，重构复制逻辑：复制内容优先策略，排除标题，支持多类型，不显示提示反馈。
    auto selected = m_listView->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;

    QList<QVariantMap> notes;
    for (const auto& index : std::as_const(selected)) {
        int id = index.data(NoteModel::IdRole).toInt();
        // [CRITICAL] 锁定：内容提取视为实际操作，必须显式记录访问。严禁移除。
        DatabaseManager::instance().recordAccess(id); 
        notes << DatabaseManager::instance().getNoteById(id);
    }
    
    StringUtils::copyNotesToClipboard(notes);
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
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #e74c3c;'>[ERR] 尚未设定应用密码，请先进行设定</b>");
        return;
    }

    // 1. 隐藏所有其它顶级业务窗口 (排除自身、悬浮球)
    for (QWidget* widget : QApplication::topLevelWidgets()) {
        if (widget == this) continue;
        if (widget->inherits("QSystemTrayIcon")) continue; 
        
        // 排除某些特定窗口类或对象名 (可选)
        if (widget->isVisible()) {
            widget->hide();
        }
    }

    // 2. 强制显示应用锁
    setupAppLock();

    // 3. 弹出快速笔记窗口并聚焦
    showAuto();
    
    ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #2ecc71;'>[OK] 应用已锁定</b>");
}

void QuickWindow::updatePreviewContent() {
    QModelIndex index = m_listView->currentIndex();
    if (!index.isValid()) return;
    
    // [PERFORMANCE] 构造笔记快照包，直接传递给预览窗，消除其内部查库逻辑
    QVariantMap note;
    note["id"] = index.data(NoteModel::IdRole);
    note["title"] = index.data(NoteModel::TitleRole);
    note["content"] = index.data(NoteModel::ContentRole);
    note["item_type"] = index.data(NoteModel::TypeRole);
    note["data_blob"] = index.data(NoteModel::BlobRole);
    note["tags"] = index.data(NoteModel::TagsRole);
    note["rating"] = index.data(NoteModel::RatingRole);
    note["is_pinned"] = index.data(NoteModel::PinnedRole);
    note["is_favorite"] = index.data(NoteModel::FavoriteRole);
    // 2026-03-xx 按照用户要求：不再传递废弃的 is_locked 状态
    note["created_at"] = index.data(NoteModel::TimeRole);
    note["updated_at"] = index.data(NoteModel::TimeRole); // Model 暂未提供 UpdatedRole，暂用 TimeRole 占位
    note["remark"] = index.data(NoteModel::RemarkRole);

    QString catName = index.data(NoteModel::CategoryNameRole).toString();
    
    auto* preview = QuickPreview::instance();
    QPoint pos = preview->isVisible() ? preview->pos() : m_listView->mapToGlobal(m_listView->rect().center()) - QPoint(250, 300);

    preview->showPreview(note, pos, catName, m_listView);
}

void QuickWindow::doPreview() {
    // 增加防抖保护，防止双重触发
    static QElapsedTimer timer;
    if (timer.isValid() && timer.elapsed() < 200) {
        return;
    }
    timer.restart();

    auto* preview = QuickPreview::instance();

    QWidget* focusWidget = QApplication::focusWidget();
    // [OPTIMIZED] 精准判定输入状态。
    if (focusWidget) {
        bool isInput = qobject_cast<QLineEdit*>(focusWidget) || 
                       qobject_cast<QTextEdit*>(focusWidget) ||
                       qobject_cast<QPlainTextEdit*>(focusWidget);
        
        if (isInput) {
            bool isReadOnly = focusWidget->property("readOnly").toBool();
            if (auto* le = qobject_cast<QLineEdit*>(focusWidget)) isReadOnly = le->isReadOnly();
            
            if (!isReadOnly) return;
        }
    }

    // [PROFESSIONAL] 如果预览窗已打开且归属权在我，按空格关闭
    if (preview->isVisible() && preview->caller() && preview->caller()->window() == this) {
        preview->hide();
        return;
    }
    
    updatePreviewContent();
    
    preview->raise();
    preview->activateWindow();
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
        // 2026-03-xx 按照用户要求，切换图标样式 (选中时橙色垂直，未选中时灰色倾斜)
        btnPin->setIcon(IconHelper::getIcon(checked ? "pin_vertical" : "pin_tilted", checked ? "#FF551C" : "#aaaaaa", 20));
    }
}


void QuickWindow::toggleSidebar() {
    // 2026-04-05 按照用户要求：开启/关闭侧边栏时，触发极致紧凑型布局刷新
    if (!m_sidebarWrapper) return;
    
    bool visible = !m_sidebarWrapper->isVisible();
    m_sidebarWrapper->setVisible(visible);
    
    // 更新按钮状态
    auto* btnSidebar = findChild<QPushButton*>("btnSidebar");
    if (btnSidebar) {
        btnSidebar->setChecked(visible);
        // 2026-04-xx 按照用户要求：锁定特征识别色，侧边栏 eye 图标固定使用青色
        btnSidebar->setIcon(IconHelper::getIcon("eye", "#41F2F2"));
    }

    // 触发全局宽度重算
    updateLayoutWidth();
    updateToggleAllIcon(); // [NEW] 同步更新联动按钮图标

    QString name;
    if (m_systemTree->currentIndex().isValid()) name = m_systemTree->currentIndex().data().toString();
    else name = m_partitionTree->currentIndex().data().toString();
    
    updatePartitionStatus(name);
    updateFocusLines();
}

void QuickWindow::toggleFilter() {
    // 2026-04-05 按照用户要求：开启/关闭高级筛选时，触发极致紧凑型布局刷新
    if (!m_filterWrapper) return;
    
    bool visible = !m_filterWrapper->isVisible();
    m_filterWrapper->setVisible(visible);
    
    // 触发全局宽度重算
    updateLayoutWidth();
    updateToggleAllIcon(); // [NEW] 同步更新联动按钮图标

    // 更新按钮状态
    QPushButton* btnFilter = findChild<QPushButton*>("btnFilter");
    if (btnFilter) {
        btnFilter->setChecked(visible);
        // 2026-04-xx 按照用户要求：锁定特征识别色，高级筛选 filter 图标固定使用黄色
        btnFilter->setIcon(IconHelper::getIcon("filter", "#f1c40f"));
    }

    if (visible && m_filterPanel) {
        m_filterPanel->updateStats(m_searchEdit->text(), m_currentFilterType, m_currentFilterValue);
    }
}

void QuickWindow::toggleMaximize() {
    // 2026-04-xx 按照共识方案：支持在紧凑宽度与扩展宽度之间循环切换
    bool sideVisible = m_sidebarWrapper && m_sidebarWrapper->isVisible();
    bool filterVisible = m_filterWrapper && m_filterWrapper->isVisible();
    int activeCount = (sideVisible ? 1 : 0) + (filterVisible ? 1 : 0);
    int compactWidth = (activeCount == 2) ? 563 : 400;

    if (width() <= compactWidth) {
        // 执行“最大化”：扩展至 900 像素以支持全内容展示
        this->setMinimumWidth(compactWidth);
        this->resize(900, height());
        if (m_btnMax) m_btnMax->setIcon(IconHelper::getIcon("restore", "#aaaaaa"));
    } else {
        // 执行“还原”：回退到最紧凑状态
        updateLayoutWidth();
        if (m_btnMax) m_btnMax->setIcon(IconHelper::getIcon("maximize", "#aaaaaa"));
    }
}

void QuickWindow::toggleAllPanels() {
    // 2026-04-xx 按照用户要求：一键联动逻辑。若任一面板可见，则全部收起；若全隐藏，则全部展开。
    bool sidebarVisible = m_sidebarWrapper && m_sidebarWrapper->isVisible();
    bool filterVisible = m_filterWrapper && m_filterWrapper->isVisible();
    bool targetVisible = !(sidebarVisible || filterVisible);

    if (m_sidebarWrapper) m_sidebarWrapper->setVisible(targetVisible);
    if (m_filterWrapper) m_filterWrapper->setVisible(targetVisible);

    // 同步更新子按钮的 Checked 状态
    QPushButton* btnSidebar = findChild<QPushButton*>("btnSidebar");
    if (btnSidebar) btnSidebar->setChecked(targetVisible);
    QPushButton* btnFilter = findChild<QPushButton*>("btnFilter");
    if (btnFilter) btnFilter->setChecked(targetVisible);

    updateLayoutWidth();
    updateToggleAllIcon();

    if (targetVisible && m_filterPanel) {
        m_filterPanel->updateStats(m_searchEdit->text(), m_currentFilterType, m_currentFilterValue);
    }
}

void QuickWindow::updateToggleAllIcon() {
    // 2026-04-xx 按照用户要求：联动状态图标与高亮背景切换逻辑
    if (!m_btnToggleAll) return;

    bool sidebarVisible = m_sidebarWrapper && m_sidebarWrapper->isVisible();
    bool filterVisible = m_filterWrapper && m_filterWrapper->isVisible();

    bool anyVisible = (sidebarVisible || filterVisible);

    // 1. 物理图标与识别色同步
    // 2026-04-xx 按照用户最新截图规范：图标颜色常驻蓝色 #3A90FF，不再随显隐状态变灰
    QString iconName = !anyVisible ? "sidebar_open_filled" : "panel_right_filled";
    m_btnToggleAll->setIcon(IconHelper::getIcon(iconName, "#3A90FF"));

    // 2. 高亮背景切换：若任一面板开启，则按钮保持 Checked 高亮态，确保视觉逻辑一致
    m_btnToggleAll->setChecked(anyVisible);
}

void QuickWindow::updateLayoutWidth() {
    // [CRITICAL] 2026-04-05 极致紧凑布局核心逻辑
    bool sideVisible = m_sidebarWrapper->isVisible();
    bool filterVisible = m_filterWrapper->isVisible();
    int activeCount = (sideVisible ? 1 : 0) + (filterVisible ? 1 : 0);
    
    int targetWidth = 400; 
    
    if (activeCount == 2) {
        targetWidth = 563; // 双开状态锁定底线为 563 像素
    }
    
    // 物理锁定最小值
    this->setMinimumWidth(targetWidth);
    
    // [MODIFIED] 2026-04-xx 按照用户最新指令：修正自动缩放逻辑。
    // 之前因误判“手动拉大”优先级导致切换时无法自动缩短。
    // 现统一为：只要触发面板切换，窗口立即自动调整到推荐的最紧凑宽度（400px 或 563px）。
    // 这不影响用户在切换完成后再次手动通过边缘拉大窗口。
    this->resize(targetWidth, this->height());
    
    // [REFINED] 2026-04-xx 同步精确计算分栏器尺寸权重
    int listSize = targetWidth - 36;
    int filterSize = 0;
    int sideSize = 0;

    if (filterVisible) {
        filterSize = 163;
        listSize -= 163;
    }
    if (sideVisible) {
        sideSize = 163;
        listSize -= 163;
    }
    
    // 强制执行 Splitter 尺寸分配，确保面板显示比例正确
    m_splitter->setSizes({listSize, filterSize, sideSize});
}

void QuickWindow::showListContextMenu(const QPoint& pos) {
    auto selected = m_listView->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) {
        QModelIndex index = m_listView->indexAt(pos);
        if (index.isValid()) {
            m_listView->setCurrentIndex(index);
            selected << index;
        }
    }

    int selCount = selected.size();
    QMenu menu(this);
    IconHelper::setupMenu(&menu);
    menu.setStyleSheet("QMenu { background-color: #2D2D2D; color: #EEE; border: 1px solid #444; padding: 4px; } "
                       /* 10px 间距规范：padding-left 10px + icon margin-left 6px */
                       "QMenu::item { padding: 6px 10px 6px 10px; border-radius: 3px; } "
                       "QMenu::icon { margin-left: 6px; } "
                       "QMenu::item:selected { background-color: #3E3E42; color: white; }"); // 2026-03-13 修改悬停色为灰色，防止与蓝色图标视觉重合

    auto getHint = [](const QString& id) {
        QKeySequence seq = ShortcutManager::instance().getShortcut(id);
        return seq.isEmpty() ? "" : " (" + seq.toString(QKeySequence::NativeText).replace("+", " + ") + ")";
    };

    // [USER_REQUEST] 列表空白处右键弹出“新建数据”
    if (selCount == 0) {
        menu.addAction(IconHelper::getIcon("add", "#3498db", 18), " 新建数据" + getHint("qw_new_idea"), this, &QuickWindow::doNewIdea);
        menu.exec(m_listView->mapToGlobal(pos));
        return;
    }

    if (selCount == 1) {
        // 2026-03-13 按照用户要求：eye 图标颜色统一为 #41F2F2
        menu.addAction(IconHelper::getIcon("eye", "#41F2F2", 18), "预览" + getHint("qw_preview"), this, &QuickWindow::doPreview);
        
        QString content = selected.first().data(NoteModel::ContentRole).toString();
        QString type = selected.first().data(NoteModel::TypeRole).toString();
        
        // 智能检测网址并显示打开菜单
        QString firstUrl = StringUtils::extractFirstUrl(content);
        if (!firstUrl.isEmpty()) {
            menu.addAction(IconHelper::getIcon("link", "#17B345", 18), "打开链接", [firstUrl]() {
                QDesktopServices::openUrl(QUrl(firstUrl));
            });
        }

        // [CRITICAL] 锁定：智能路径检测逻辑。支持托管项目（attachments/）及磁盘绝对路径的智能识别。
        // 即使类型为 text，若内容指向有效物理路径，也必须显示“在资源管理器中显示”菜单。严禁移除。
        bool isPath = (type == "file" || type == "local_file" || type == "local_folder" || type == "local_batch");
        QString plainContent = StringUtils::htmlToPlainText(content).trimmed();
        QString path = content;

        if (!isPath) {
            // [USER_REQUEST] 智能路径检测：即使类型不是文件，如果内容本身是一个有效的绝对路径，也支持定位
            if (QFileInfo(plainContent).exists() && QFileInfo(plainContent).isAbsolute()) {
                isPath = true;
                path = plainContent;
            }
        }

        if (isPath) {
            if (path.startsWith("attachments/")) {
                path = QCoreApplication::applicationDirPath() + "/" + path;
            }

            // [UX] 增加路径有效性检查：如果物理文件已丢失，菜单显示为置灰的“无效项目”
            if (QFileInfo::exists(path)) {
                menu.addAction(IconHelper::getIcon("folder", "#3A90FF", 18), "在资源管理器中显示", [path]() {
                    StringUtils::locateInExplorer(path, true);
                });
            } else {
                QAction* invalidAction = menu.addAction(IconHelper::getIcon("folder", "#555555", 18), "无效项目");
                invalidAction->setEnabled(false);
                invalidAction->setProperty("tooltipText", "该数据对应的原始文件已在磁盘中丢失或被移动");
            }
        }
    }
    
    menu.addAction(IconHelper::getIcon("copy", "#1abc9c", 18), QString("复制 (%1)").arg(selCount), this, &QuickWindow::doExtractContent);
    menu.addSeparator();

    if (selCount == 1) {
        menu.addAction(IconHelper::getIcon("edit", "#4a90e2", 18), "编辑" + getHint("qw_edit"), this, &QuickWindow::doEditSelected);
        
        // [USER_REQUEST] 2026-03-14 右键菜单新增：复制/粘贴标签
        QString tags = selected.first().data(NoteModel::TagsRole).toString();
        if (!tags.trimmed().isEmpty()) {
            menu.addAction(IconHelper::getIcon("copy_tags", "#9b59b6", 18), "复制标签" + getHint("qw_copy_tags"), this, &QuickWindow::doCopyTags);
        }
        
        // [USER_REQUEST] 傻逼逻辑修复：仅当标签剪贴板不为空（即已执行 Ctrl+Alt+C）时，才显示“粘贴标签”选项
        if (!DatabaseManager::getTagClipboard().isEmpty()) {
            menu.addAction(IconHelper::getIcon("paste_tags", "#e67e22", 18), "粘贴标签" + getHint("qw_paste_tags"), this, &QuickWindow::doPasteTags);
        }
        
        menu.addSeparator();
    }

    auto* ratingMenu = menu.addMenu(IconHelper::getIcon("star", "#f39c12", 18), QString("标记星级 (%1)").arg(selCount));
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
    // 2026-03-xx 按照用户要求：未收藏使用灰色 bookmark，已收藏使用黄色 bookmark_filled
    menu.addAction(IconHelper::getIcon(isFavorite ? "bookmark_filled" : "bookmark", isFavorite ? "#F2B705" : "#aaaaaa", 18), 
                   isFavorite ? "取消收藏" : "添加收藏" + getHint("qw_favorite"), this, &QuickWindow::doToggleFavorite);

    bool isPinned = selected.first().data(NoteModel::PinnedRole).toBool();
    // 2026-03-12 按照用户要求，统一置顶图标颜色为橙色 (#FF551C)
    menu.addAction(IconHelper::getIcon(isPinned ? "pin_vertical" : "pin_tilted", isPinned ? "#FF551C" : "#aaaaaa", 18), 
                   isPinned ? "取消置顶" : "置顶选中项" + getHint("qw_pin"), this, &QuickWindow::doTogglePin);
    
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
        /* [MODIFIED] 按照用户要求：更精细化恢复文案与逻辑，区分“恢复选中”与“全部恢复” */
        QString restoreText = selected.size() > 1 ? QString("恢复选中项 (%1)").arg(selected.size()) : "恢复";
        menu.addAction(IconHelper::getIcon("refresh", "#2ecc71", 18), restoreText, [this, selected](){
            QList<int> noteIds;
            QList<int> catIds;
            for (const auto& index : selected) {
                QString type = index.data(NoteModel::TypeRole).toString();
                int id = index.data(NoteModel::IdRole).toInt();
                if (type == "deleted_category") catIds << id;
                else noteIds << id;
            }
            if (!noteIds.isEmpty()) DatabaseManager::instance().updateNoteStateBatch(noteIds, "is_deleted", 0);
            if (!catIds.isEmpty()) DatabaseManager::instance().restoreCategories(catIds);
            refreshData();
            refreshSidebar();
            QString successMsg = selected.size() > 1 ? QString("[OK] 已恢复选中的 %1 个项目").arg(selected.size()) : "[OK] 已恢复 1 个项目";
            ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #2ecc71;'>" + successMsg + "</b>");
        });

        menu.addAction(IconHelper::getIcon("refresh", "#3498db", 18), "全部恢复 (还原所有)", [this](){
            if (DatabaseManager::instance().restoreAllFromTrash()) {
                refreshData();
                refreshSidebar();
                ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #2ecc71;'>[OK] 已将回收站内容全量还原</b>");
            }
        });

        menu.addAction(IconHelper::getIcon("trash", "#e74c3c", 18), "彻底删除 (不可逆)", [this](){ doDeleteSelected(true); });
    } else {
        menu.addAction(IconHelper::getIcon("trash", "#e74c3c", 18), "移至回收站" + getHint("qw_delete_soft"), [this](){ doDeleteSelected(false); });
    }

    if (m_currentFilterType != "recently_visited") {
        menu.addSeparator();
        auto* sortMenu = menu.addMenu(IconHelper::getIcon("list_ol", "#aaaaaa", 18), "排列");
        sortMenu->setStyleSheet(menu.styleSheet());
        
        sortMenu->addAction("上移" + getHint("qw_move_up"), [this](){ doMoveNote(DatabaseManager::Up); });
        sortMenu->addAction("下移" + getHint("qw_move_down"), [this](){ doMoveNote(DatabaseManager::Down); });
        sortMenu->addAction("移至顶部", [this](){ doMoveNote(DatabaseManager::Top); });
        sortMenu->addAction("移至底部", [this](){ doMoveNote(DatabaseManager::Bottom); });
        sortMenu->addSeparator();
        sortMenu->addAction("按标题 A→Z 排列", [this](){
            DatabaseManager::instance().reorderNotes(m_currentFilterType, m_currentFilterValue, true);
        });
        sortMenu->addAction("按标题 Z→A 排列", [this](){
            DatabaseManager::instance().reorderNotes(m_currentFilterType, m_currentFilterValue, false);
        });
    }

    menu.exec(m_listView->mapToGlobal(pos));
}

void QuickWindow::showSidebarMenu(const QPoint& pos) {
    auto* tree = qobject_cast<QTreeView*>(sender());
    if (!tree) return;

    QModelIndexList selected = tree->selectionModel()->selectedIndexes();
    QModelIndex index = tree->indexAt(pos);

    if (index.isValid() && !selected.contains(index)) {
        tree->setCurrentIndex(index);
        selected.clear();
        selected << index;
    }

    QMenu menu(this);
    IconHelper::setupMenu(&menu);
    menu.setStyleSheet("QMenu { background-color: #2D2D2D; color: #EEE; border: 1px solid #444; padding: 4px; } "
                       /* 10px 间距规范：padding-left 10px + icon margin-left 6px */
                       "QMenu::item { padding: 6px 10px 6px 10px; border-radius: 3px; } "
                       "QMenu::icon { margin-left: 6px; } "
                       "QMenu::item:selected { background-color: #3E3E42; color: white; }"); // 2026-03-13 修改悬停色为灰色，防止与蓝色图标视觉重合

    QString type = index.data(CategoryModel::TypeRole).toString();
    QString idxName = index.data(CategoryModel::NameRole).toString();

    // 2026-03-22 [MODIFIED] 按照用户要求：支持特殊分类（全部、收藏、今日等）的导出菜单
    static const QStringList silentTypes = {"recently_visited", "untagged"};
    if (silentTypes.contains(type)) {
        return;
    }

    if (type == "all") {
        menu.addAction(IconHelper::getIcon("file_export", "#3498db", 18), "导出完整结构数据", [this]() {
            if (!verifyExportPermission()) return;
            FileStorageHelper::exportFullStructure(this);
        });
        menu.exec(tree->mapToGlobal(pos));
        return;
    }

    if (type == "today" || type == "yesterday" || type == "bookmark") {
        menu.addAction(IconHelper::getIcon("file_export", "#3498db", 18), QString("导出 [%1]").arg(idxName), [this, type, idxName]() {
            if (!verifyExportPermission()) return;
            FileStorageHelper::exportByFilter(type, QVariant(), idxName, this);
        });
        menu.exec(tree->mapToGlobal(pos));
        return;
    }
    
    // [CRITICAL] 锁定：通过 NameRole 匹配“我的分类”来判定右键菜单弹出逻辑，支持新建分类
    if (!index.isValid() || idxName == "我的分类") {
        menu.addAction(IconHelper::getIcon("add", "#3498db", 18), "新建分类", [this]() {
            FramelessInputDialog dlg("新建分类", "组名称:", "", this);
            if (dlg.exec() == QDialog::Accepted) {
                QString text = dlg.text();
                if (!text.isEmpty()) {
                    DatabaseManager::instance().addCategory(text);
                    refreshSidebar();
                }
            }
        });
        auto* importMenu = menu.addMenu(IconHelper::getIcon("file_import", "#1abc9c", 18), "导入数据");
        importMenu->setStyleSheet(menu.styleSheet());
        importMenu->addAction(IconHelper::getIcon("file", "#1abc9c", 18), "导入文件(s)...", [this]() {
            doImportCategory(-1);
        });
        importMenu->addAction(IconHelper::getIcon("folder", "#1abc9c", 18), "导入文件夹...", [this]() {
            doImportFolder(-1);
        });
        menu.exec(tree->mapToGlobal(pos));
        return;
    }

    if (type == "category") {
        int catId = index.data(CategoryModel::IdRole).toInt();
        QString currentName = index.data(CategoryModel::NameRole).toString();

        menu.addAction(IconHelper::getIcon("add", "#3498db", 18), "新建数据", [this, catId]() {
            auto* win = new NoteEditWindow();
            win->setDefaultCategory(catId);
            connect(win, &NoteEditWindow::noteSaved, this, &QuickWindow::refreshData);
            win->show();
        });
        menu.addAction(IconHelper::getIcon("branch", "#3498db", 18), "归类到此分类", [this, catId, currentName]() {
            DatabaseManager::instance().setExtensionTargetCategoryId(catId);
            ToolTipOverlay::instance()->showText(QCursor::pos(), QString("<b style='color: #3498db;'>[OK] 已指定插件归类到: %1</b>").arg(currentName));
            // [USER_REQUEST] 归类后立即更新工具栏按钮状态
            this->updateAutoCategorizeButton();
        });
        menu.addSeparator();
        auto* importMenu = menu.addMenu(IconHelper::getIcon("file_import", "#1abc9c", 18), "导入数据");
        importMenu->setStyleSheet(menu.styleSheet());
        importMenu->addAction(IconHelper::getIcon("file", "#1abc9c", 18), "导入文件(s)...", [this, catId]() {
            doImportCategory(catId);
        });
        importMenu->addAction(IconHelper::getIcon("folder", "#1abc9c", 18), "导入文件夹...", [this, catId]() {
            doImportFolder(catId);
        });

        // 2026-03-xx 按照用户要求：新增专属安装包 (.rnp) 导入，使用专用加密包图标
        importMenu->addAction(IconHelper::getIcon("package_rnp", "#9b59b6", 18), "导入专属安装包 (.rnp)", [this]() {
            FileStorageHelper::importFromPackage(this);
            this->refreshSidebar();
            this->refreshData();
        });
        
        // [任务1] 将右键菜单的“导出此分类”改为二级“导出”菜单
        auto* exportMenu = menu.addMenu(IconHelper::getIcon("file_export", "#3498db", 18), "导出");
        exportMenu->setStyleSheet(menu.styleSheet());
        
        QVariantMap rootCat = DatabaseManager::instance().getRootCategory(catId);
        QString rootName = rootCat.value("name").toString();
        int rootId = rootCat.value("id").toInt();
        
        if (rootId == catId) {
            // 2026-03-xx 按照用户最新要求：主分类菜单项仅执行单分类导出
            exportMenu->addAction(IconHelper::getIcon("folder", "#3498db", 18), rootName, [this, rootId, rootName]() {
                doExportCategory(rootId, rootName);
            });
            
            auto children = DatabaseManager::instance().getChildCategories(rootId);
            for (const auto& child : std::as_const(children)) {
                int childId = child.value("id").toInt();
                QString childName = child.value("name").toString();
                exportMenu->addAction(IconHelper::getIcon("branch", "#3498db", 18), childName, [this, childId, childName]() {
                    doExportCategory(childId, childName);
                });
            }
        } else {
            // 2026-03-xx 按照用户要求：子选项显示名称且仅执行单分类导出
            exportMenu->addAction(IconHelper::getIcon("branch", "#3498db", 18), currentName, [this, catId, currentName]() {
                doExportCategory(catId, currentName);
            });
            if (!rootName.isEmpty()) {
                exportMenu->addAction(IconHelper::getIcon("folder", "#3498db", 18), rootName, [this, rootId, rootName]() {
                    doExportCategory(rootId, rootName);
                });
            }
        }
        
        // 2026-03-xx 新增“整分类”选项，执行包含主分类及子分类的递归导出
        exportMenu->addSeparator();
        exportMenu->addAction(IconHelper::getIcon("folder", "#3498db", 18), "整分类", [this, rootId, rootName]() {
            if (!verifyExportPermission()) return;
            FileStorageHelper::exportCategoryRecursive(rootId, rootName, this);
        });

        // 2026-03-xx 按照用户要求：新增专属安装包 (.rnp) 导出，使用专用加密包图标
        exportMenu->addAction(IconHelper::getIcon("package_rnp", "#9b59b6", 18), "导出为数据包 (.rnp)", [this, catId, currentName]() {
            if (!verifyExportPermission()) return;
            FileStorageHelper::exportToPackage(catId, currentName, this);
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
            FramelessInputDialog dlg("设置预设标签", "标签 (逗号分隔):", currentTags, this);
            if (dlg.exec() == QDialog::Accepted) {
                DatabaseManager::instance().setCategoryPresetTags(catId, dlg.text());
            }
        });
        menu.addSeparator();

        // 重新加入遗漏的分支：新建/删除/排序/密码
        menu.addAction(IconHelper::getIcon("add", "#3498db", 18), "新建分类", [this]() {
            FramelessInputDialog dlg("新建分类", "组名称:", "", this);
            if (dlg.exec() == QDialog::Accepted) {
                QString text = dlg.text();
                if (!text.isEmpty()) {
                    DatabaseManager::instance().addCategory(text);
                    refreshSidebar();
                }
            }
        });
        menu.addAction(IconHelper::getIcon("add", "#3498db", 18), "新建子分类", [this, catId]() {
            FramelessInputDialog dlg("新建子分类", "区名称:", "", this);
            if (dlg.exec() == QDialog::Accepted) {
                QString text = dlg.text();
                if (!text.isEmpty()) {
                    DatabaseManager::instance().addCategory(text, catId);
                    refreshSidebar();
                }
            }
        });
        menu.addSeparator();

        if (selected.size() == 1) {
            bool isPinned = index.data(CategoryModel::PinnedRole).toBool();
            // 2026-03-12 按照用户要求，统一置顶图标颜色为橙色
            menu.addAction(IconHelper::getIcon(isPinned ? "pin_vertical" : "pin_tilted", isPinned ? "#FF551C" : "#aaaaaa", 18), 
                           isPinned ? "取消置顶" : "置顶分类", [this, catId]() {
                DatabaseManager::instance().toggleCategoryPinned(catId);
                refreshSidebar();
            });
            
            menu.addAction(IconHelper::getIcon("edit", "#aaaaaa", 18), "重命名", [this, index]() {
                m_partitionTree->edit(index);
            });
        }

        QString delText = selected.size() > 1 ? QString("删除选中的 %1 个分类").arg(selected.size()) : "删除";
        menu.addAction(IconHelper::getIcon("trash", "#e74c3c", 18), delText, [this, selected]() {
            // 2026-03-xx 按照用户要求：改为混合删除（分类物理删除，内容移至回收站）
            QString msg = selected.size() > 1 ? "确定要删除选中的分类吗？\n(分类将永久消失，内容将移至回收站)" : "确定要删除此分类吗？\n(分类将永久消失，内容将移至回收站)";
            FramelessMessageBox dlg("确认删除", msg, this);
            if (dlg.exec() == QDialog::Accepted) {
                QList<int> ids;
                for (const auto& idx : selected) {
                    if (idx.data(CategoryModel::TypeRole).toString() == "category") {
                        ids << idx.data(CategoryModel::IdRole).toInt();
                    }
                }
                qDebug() << "[QuickWindow] 准备物理删除分类，提取到的 IDs:" << ids;
                DatabaseManager::instance().hardDeleteCategories(ids);
                refreshSidebar();
                refreshData();
            }
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
                ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#2ecc71;'>[OK] 排列已完成</b>");
        });
        sortMenu->addAction("标题(当前层级) (Z→A)", [this, parentId]() {
            if (DatabaseManager::instance().reorderCategories(parentId, false))
                ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#2ecc71;'>[OK] 排列已完成</b>");
        });
        sortMenu->addAction("标题(全部) (A→Z)", [this]() {
            if (DatabaseManager::instance().reorderAllCategories(true))
                ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#2ecc71;'>[OK] 全部排列已完成</b>");
        });
        sortMenu->addAction("标题(全部) (Z→A)", [this]() {
            if (DatabaseManager::instance().reorderAllCategories(false))
                ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#2ecc71;'>[OK] 全部排列已完成</b>");
        });

        menu.addSeparator();
        auto* pwdMenu = menu.addMenu(IconHelper::getIcon("lock", "#aaaaaa", 18), "密码保护");
        pwdMenu->setStyleSheet(menu.styleSheet());

        pwdMenu->addAction("设置", [this, catId]() {
            QTimer::singleShot(0, [this, catId]() {
                CategoryPasswordDialog dlg("设置密码", this);
                if (dlg.exec() == QDialog::Accepted) {
                    DatabaseManager::instance().setCategoryPassword(catId, dlg.password(), dlg.passwordHint());
                    refreshSidebar();
                    refreshData();
                }
            });
        });
        pwdMenu->addAction("修改", [this, catId]() {
            QTimer::singleShot(0, [this, catId]() {
                FramelessInputDialog verifyDlg("验证旧密码", "请输入当前密码:", "", this);
                verifyDlg.setEchoMode(QLineEdit::Password);
                if (verifyDlg.exec() == QDialog::Accepted) {
                    if (DatabaseManager::instance().verifyCategoryPassword(catId, verifyDlg.text())) {
                        CategoryPasswordDialog dlg("修改密码", this);
                        QString currentHint;
                        auto cats = DatabaseManager::instance().getAllCategories();
                        for(const auto& c : std::as_const(cats)) if(c.value("id").toInt() == catId) currentHint = c.value("password_hint").toString();
                        dlg.setInitialData(currentHint);
                        if (dlg.exec() == QDialog::Accepted) {
                            DatabaseManager::instance().setCategoryPassword(catId, dlg.password(), dlg.passwordHint());
                            refreshSidebar();
                            refreshData();
                        }
                    } else {
                        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #e74c3c;'>[ERR] 旧密码验证失败</b>");
                    }
                }
            });
        });
        pwdMenu->addAction("移除", [this, catId]() {
            QTimer::singleShot(0, [this, catId]() {
                FramelessInputDialog dlg("验证密码", "请输入当前密码以移除保护:", "", this);
                dlg.setEchoMode(QLineEdit::Password);
                if (dlg.exec() == QDialog::Accepted) {
                    if (DatabaseManager::instance().verifyCategoryPassword(catId, dlg.text())) {
                        DatabaseManager::instance().removeCategoryPassword(catId);
                        refreshSidebar();
                        refreshData();
                    } else {
                        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #e74c3c;'>[ERR] 密码错误</b>");
                    }
                }
            });
        });
        pwdMenu->addAction("立即锁定", [this, catId]() {
            DatabaseManager::instance().lockCategory(catId);
            refreshSidebar();
            refreshData();
        })->setShortcut(QKeySequence("Ctrl+S"));
    } else if (idxName == "未分类" || type == "uncategorized") {
        menu.addAction(IconHelper::getIcon("add", "#3498db", 18), "新建数据", [this]() {
            auto* win = new NoteEditWindow();
            connect(win, &NoteEditWindow::noteSaved, this, &QuickWindow::refreshData);
            win->show();
        });
        menu.addAction(IconHelper::getIcon("branch", "#3498db", 18), "归类到此分类", [this]() {
            DatabaseManager::instance().setExtensionTargetCategoryId(-1);
            ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #3498db;'>[OK] 已指定插件归类到: 未分类</b>");
            this->updateAutoCategorizeButton();
        });
        menu.addSeparator();
        menu.addAction(IconHelper::getIcon("file_export", "#3498db", 18), "导出 [未分类]", [this]() {
            if (!verifyExportPermission()) return;
            FileStorageHelper::exportByFilter("uncategorized", -1, "未分类灵感", this);
        });
    }

    if (idxName == "回收站" || type == "trash") {
        menu.addAction(IconHelper::getIcon("refresh", "#2ecc71", 18), "全部恢复 (到未分类)", this, &QuickWindow::doRestoreTrash);
        menu.addSeparator();
        menu.addAction(IconHelper::getIcon("trash", "#e74c3c", 18), "清空回收站", [this]() {
            FramelessMessageBox dlg("确认清空", "确定要永久删除回收站中的所有内容吗？\n(此操作不可逆)", this);
            if (dlg.exec() == QDialog::Accepted) {
                DatabaseManager::instance().emptyTrash();
                refreshData();
                refreshSidebar();
            }
        });
    }

    menu.exec(tree->mapToGlobal(pos));
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
    // [USER_REQUEST] 2026-03-14 记录动作，用于 F4 重复操作
    ActionRecorder::instance().recordMoveToCategory(catId);
    refreshData();
}

void QuickWindow::doMoveNote(DatabaseManager::MoveDirection dir) {
    QModelIndex index = m_listView->currentIndex();
    if (!index.isValid()) return;
    
    int id = index.data(NoteModel::IdRole).toInt();
    if (DatabaseManager::instance().moveNote(id, dir, m_currentFilterType, m_currentFilterValue)) {
        // 刷新后由于 ID 相同，refreshData 会自动恢复选中项
    }
}

void QuickWindow::sendNote(const QVariantMap& note) {
    // 2026-03-20 [NEW] 统一发送逻辑：复制到剪贴板并模拟物理按键发送
    if (note.isEmpty()) return;

    int id = note.value("id").toInt();
    QString itemType = note.value("item_type").toString();
    QString content = note.value("content").toString();
    QByteArray blob = note.value("data_blob").toByteArray();

    // [CRITICAL] 锁定：发送笔记视为实际操作，必须显式记录访问。严禁移除。
    DatabaseManager::instance().recordAccess(id);

    // 执行复制 (复用 activateNote 的核心逻辑)
    if (itemType == "image") {
        QImage img;
        img.loadFromData(blob);
        ClipboardMonitor::instance().skipNext();
        QApplication::clipboard()->setImage(img);
    } else {
        StringUtils::copyNoteToClipboard(content);
    }

#ifdef Q_OS_WIN
    if (m_lastActiveHwnd && IsWindow(m_lastActiveHwnd)) {
        if (IsIconic(m_lastActiveHwnd)) ShowWindow(m_lastActiveHwnd, SW_RESTORE);
        SetForegroundWindow(m_lastActiveHwnd);
        
        QTimer::singleShot(300, [this]() {
            INPUT inputs[4];
            memset(inputs, 0, sizeof(inputs));
            inputs[0].type = INPUT_KEYBOARD; inputs[0].ki.wVk = VK_CONTROL;
            inputs[1].type = INPUT_KEYBOARD; inputs[1].ki.wVk = 'V';
            inputs[2].type = INPUT_KEYBOARD; inputs[2].ki.wVk = 'V'; inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
            inputs[3].type = INPUT_KEYBOARD; inputs[3].ki.wVk = VK_CONTROL; inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(4, inputs, sizeof(INPUT));
        });
    }
#endif

    // 发送后，执行“滚动快照”机制，将新发送项设为快照中心
    updateContextSnapshotById(id);
}

bool QuickWindow::verifyExportPermission() {
    // 2026-03-20 [NEW] 按照用户要求：导出数据前强制进行身份验证。
    QSettings settings("RapidNotes", "QuickWindow");
    QString savedPwd = settings.value("appPassword").toString();

    // 如果未设置应用密码，则拦截并提示去设置，防止通过导出绕过安全机制。
    if (savedPwd.isEmpty()) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), 
            "<b style='color: #e74c3c;'>[安全拦截] 您尚未设置应用密码，请先前往主程序“设置-安全”中设置导出授权密码。</b>", 5000);
        return false;
    }

    PasswordVerifyDialog dlg("导出身份验证", "请输入应用启动密码以授权本次导出操作", this);
    if (dlg.exec() == QDialog::Accepted) {
        if (dlg.password() == savedPwd) {
            ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #2ecc71;'>[OK] 验证成功，正在准备导出...</b>", 1500);
            return true;
        } else {
            ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #e74c3c;'>[ERR] 密码错误，导出已取消。</b>", 3000);
            return false;
        }
    }
    return false;
}

void QuickWindow::updateContextSnapshotById(int noteId) {
    // 2026-03-20 [NEW] 灵感上下文快照：获取当前发送项前后各 5 条，构成共 11 条的快照
    QList<QVariantMap> allNotes = DatabaseManager::instance().searchNotes("", m_currentFilterType, m_currentFilterValue);
    int centerIdx = -1;
    for (int i = 0; i < allNotes.size(); ++i) {
        if (allNotes[i]["id"].toInt() == noteId) {
            centerIdx = i;
            break;
        }
    }

    if (centerIdx != -1) {
        m_contextNotesSnapshot.clear();
        int start = qMax(0, centerIdx - 5);
        int end = qMin(allNotes.size() - 1, centerIdx + 5);
        for (int i = start; i <= end; ++i) {
            m_contextNotesSnapshot.append(allNotes[i]);
        }
    }
}

void QuickWindow::showContextNotesMenu() {
    // 2026-03-20 [NEW] Alt+A 灵感上下文菜单
    if (m_contextNotesSnapshot.isEmpty()) {
        // 若当前快照为空，尝试以当前列表选中项作为中心点进行初始化
        QModelIndex current = m_listView->currentIndex();
        if (current.isValid()) {
            updateContextSnapshotById(current.data(NoteModel::IdRole).toInt());
        }
    }

    if (m_contextNotesSnapshot.isEmpty()) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #aaaaaa;'>[提示] 暂无上下文快照，请先在列表中发送一条灵感</b>");
        return;
    }

    QMenu* menu = new QMenu(this);
    IconHelper::setupMenu(menu);
    menu->setStyleSheet("QMenu { background-color: #1E1E1E; color: #EEE; border: 1px solid #444; padding: 4px; } "
                        "QMenu::item { padding: 8px 20px; border-radius: 4px; } "
                        "QMenu::item:selected { background-color: #3E3E42; }");

    int centerId = -1;
    if (!m_contextNotesSnapshot.isEmpty()) {
        // 逻辑上的中心通常是快照更新时的目标 ID
        // 这里简化处理，直接取列表中间项或通过某个标记（此处假设逻辑中心为展示重点）
        centerId = m_contextNotesSnapshot[m_contextNotesSnapshot.size() / 2].value("id").toInt();
    }

    for (const auto& note : std::as_const(m_contextNotesSnapshot)) {
        QString title = note.value("title").toString();
        if (title.length() > 30) title = title.left(27) + "...";
        
        bool isCenter = (note.value("id").toInt() == centerId);
        
        QAction* action = menu->addAction(title, [this, note]() {
            this->sendNote(note);
        });
        
        if (isCenter) {
            action->setIcon(IconHelper::getIcon("zap", "#3A90FF")); // 蓝色闪电标记快照中心
            action->setFont(QFont("", -1, QFont::Bold));
        }
    }

    menu->popup(QCursor::pos());
}

void QuickWindow::checkIdleLock() {
#ifdef Q_OS_WIN
    // 2026-03-xx 按照用户要求：系统闲置 30 秒自动上锁逻辑
    QSettings settings("RapidNotes", "Security");
    if (!settings.value("idleLockEnabled", false).toBool()) return;

    LASTINPUTINFO lii;
    lii.cbSize = sizeof(LASTINPUTINFO);
    if (GetLastInputInfo(&lii)) {
        DWORD idleTime = (GetTickCount() - lii.dwTime) / 1000;
        if (idleTime >= 30 && !isLocked()) {
            qDebug() << "[QuickWindow] 检测到系统闲置" << idleTime << "秒，触发自动锁定。";
            doGlobalLock();
        }
    }
#endif
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
    ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #2ecc71;'>[OK] 标签已添加</b>");
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

    /* [MODIFIED] 按照用户要求：修正 Lambda 捕获，确保批量标签更新能正确作用于所有选中项 */
    connect(selector, &AdvancedTagSelector::tagsConfirmed, [this, selected](const QStringList& tags){
        for (const auto& index : std::as_const(selected)) {
            int id = index.data(NoteModel::IdRole).toInt();
            DatabaseManager::instance().updateNoteState(id, "tags", tags.join(", "));
        }
        refreshData();
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #2ecc71;'>[OK] 标签已更新</b>");
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
    // 2026-03-13 按照用户要求：提示时长缩短为 700ms
    ToolTipOverlay::instance()->showText(QCursor::pos(), QString("<b style='color: #2ecc71;'>[OK] 已复制 %1 个标签</b>").arg(tags.size()), 700);
}

void QuickWindow::doPasteTags() {
    auto selected = m_listView->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;

    QStringList tagsToPaste = DatabaseManager::getTagClipboard();
    if (tagsToPaste.isEmpty()) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #aaaaaa;'>[!] 标签剪贴板为空</b>");
        return;
    }

    // 直接覆盖标签 (符合粘贴语义)
    QList<int> ids;
    for (const auto& index : std::as_const(selected)) ids << index.data(NoteModel::IdRole).toInt();
    DatabaseManager::instance().updateNoteStateBatch(ids, "tags", tagsToPaste.join(", "));

    refreshData();
    // [USER_REQUEST] 2026-03-14 记录动作，用于 F4 重复操作
    ActionRecorder::instance().recordPasteTags(tagsToPaste);
    ToolTipOverlay::instance()->showText(QCursor::pos(), QString("<b style='color: #2ecc71;'>[OK] 已覆盖粘贴标签至 %1 条数据</b>").arg(selected.size()));
}

void QuickWindow::doRepeatAction() {
    auto selected = m_listView->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #f1c40f;'>[提示] 请先选中一条笔记</b>");
        return;
    }

    auto actionType = ActionRecorder::instance().getLastActionType();
    
    if (actionType == ActionRecorder::ActionType::None) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #aaaaaa;'>[提示] 目前没有可重复的操作记录</b>");
        return;
    }

    QList<int> ids;
    for (const auto& index : std::as_const(selected)) ids << index.data(NoteModel::IdRole).toInt();

    if (actionType == ActionRecorder::ActionType::PasteTags) {
        QStringList tagsList = ActionRecorder::instance().getLastActionData().toStringList();
        if (tagsList.isEmpty()) return;

        QString tagsStr = tagsList.join(", ");
        DatabaseManager::instance().updateNoteStateBatch(ids, "tags", tagsStr);
        refreshData();
        ToolTipOverlay::instance()->showText(QCursor::pos(), QString("<b style='color: #2ecc71;'>[OK] 已重复：%1 条数据粘贴标签</b>").arg(ids.size()));
    } 
    else if (actionType == ActionRecorder::ActionType::MoveToCategory) {
        int catId = ActionRecorder::instance().getLastActionData().toInt();
        DatabaseManager::instance().moveNotesToCategory(ids, catId);
        refreshData();
        ToolTipOverlay::instance()->showText(QCursor::pos(), QString("<b style='color: #2ecc71;'>[OK] 已重复：%1 条数据分类归位</b>").arg(ids.size()));
    }
}

void QuickWindow::focusLockInput() {
    if (m_appLockWidget) {
        static_cast<AppLockWidget*>(m_appLockWidget)->focusInput();
    }
}

void QuickWindow::recordLastActiveWindow(HWND captureHwnd) {
#ifdef Q_OS_WIN
    // [USER_REQUEST] 记录最后一个活跃窗口逻辑优化
    // 当捕获的目标窗口属于当前进程时，会自动遍历 Windows Z 序向下查找第一个可见且不属于本进程的有效窗口（模拟 Ditto 深度搜索）。
    // 这确保了无论通过何种方式唤起应用，都能动态且精准地定位到真正的外部目标窗口。
    
    HWND targetHwnd = captureHwnd;
    if (!targetHwnd) {
        targetHwnd = GetForegroundWindow();
    }

    HWND myHwnd = (HWND)winId();
    DWORD myPid = GetCurrentProcessId();

    auto isTaskbarOrDesktop = [](HWND hwnd) {
        char className[256];
        GetClassNameA(hwnd, className, sizeof(className));
        QString cls = QString::fromLatin1(className);
        return cls == "Shell_TrayWnd" || cls == "Progman" || cls == "WorkerW";
    };

    auto isInternalToolWindow = [](HWND hwnd) {
        char className[256];
        GetClassNameA(hwnd, className, sizeof(className));
        // [USER_REQUEST] 必须显式过滤类名包含 'ToolSaveBits' 的 Qt 内部工具窗口（透明覆盖层）
        return QString::fromLatin1(className).contains("ToolSaveBits");
    };

    // 如果当前窗口是自身，或者无效，则向下搜索 Z 序
    if (targetHwnd == nullptr || targetHwnd == myHwnd || isInternalToolWindow(targetHwnd)) {
        HWND next = GetWindow(GetForegroundWindow(), GW_HWNDNEXT);
        while (next) {
            DWORD pid = 0;
            GetWindowThreadProcessId(next, &pid);
            if (pid != myPid && IsWindowVisible(next) && !isTaskbarOrDesktop(next) && !isInternalToolWindow(next)) {
                targetHwnd = next;
                break;
            }
            next = GetWindow(next, GW_HWNDNEXT);
        }
    }

    // 最终检查，如果不属于自身进程且有效，则记录
    DWORD targetPid = 0;
    GetWindowThreadProcessId(targetHwnd, &targetPid);
    if (targetHwnd && targetHwnd != myHwnd && targetPid != myPid && !isTaskbarOrDesktop(targetHwnd)) {
        m_lastActiveHwnd = targetHwnd;
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
}

void QuickWindow::showAuto() {
#ifdef Q_OS_WIN
    HWND myHwnd = (HWND)winId();
    // [USER_REQUEST] 唤起时立即记录当前目标窗口，作为粘贴兜底
    recordLastActiveWindow(nullptr);
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
        // [USER_REQUEST] 按照用户要求：只要启动后，焦点自动锁定在列表，不可锁定在搜索框
        m_listView->setFocus();
        if (m_model->rowCount() > 0 && !m_listView->currentIndex().isValid()) {
            m_listView->setCurrentIndex(m_model->index(0, 0));
        }
    }
}

void QuickWindow::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);

    // [NEW] 每次显示时刷新锁定状态图标颜色
    updateAppLockStatus();
    
    // [USER_REQUEST] 按照用户要求：只要启动后，焦点自动锁定在列表，不可锁定在搜索框
    // 移除原有的强制清除选择逻辑，改为自动聚焦并选中首项以支持“唤起即回车”
    if (m_listView && !isLocked()) {
        m_listView->setFocus();
        if (m_model->rowCount() > 0 && !m_listView->currentIndex().isValid()) {
            m_listView->setCurrentIndex(m_model->index(0, 0));
        }
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

    // [NEW] 拦截背景擦除，防止缩放闪烁
    if (msg->message == WM_ERASEBKGND) {
        *result = 1;
        return true;
    }

    // [NEW] 拦截 NCCALCSIZE，确保内容填充整个窗口并减少抖动
    if (msg->message == WM_NCCALCSIZE && msg->wParam) {
        *result = 0;
        return true;
    }

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
        else if (right) {
            // 2026-03-xx [CRITICAL] 智能边缘穿透逻辑：
            // 如果鼠标落在工具栏按钮区域，即便在 12px 的缩放边缘内，也强制返回 false 让 Qt 处理为 Client 区域。
            // 这解决了按钮右侧由于被识别为 HTRIGHT 而导致无法显示 Hover 高亮的“傻逼逻辑”问题。
            QWidget* child = childAt(pos);
            if (qobject_cast<QPushButton*>(child)) {
                return false; 
            }
            *result = HTRIGHT;
        }
        else return QWidget::nativeEvent(eventType, message, result);

        return true;
    }
    return QWidget::nativeEvent(eventType, message, result);
}
#endif

bool QuickWindow::event(QEvent* event) {
    if (event->type() == QEvent::WindowActivate) {
        // [CRITICAL] 顶级避让逻辑：快速笔记窗口激活时，强制注销全局 Ctrl+S 采集热键，打通内部锁定通道。
        HotkeyManager::instance().unregisterHotkey(4);
        qDebug() << "[QuickWindow] 窗口激活，已物理注销全局 Ctrl+S 采集热键。";
    }
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

    // [CRITICAL] 拦截内部拖拽逻辑：如果数据包含应用内部笔记 ID，说明是列表内的移动操作，
    // 严禁触发外部导入/新建笔记逻辑，从而彻底根除因拖拽导致的数据重复创建问题。
    if (mime->hasFormat("application/x-note-ids")) {
        event->ignore();
        return;
    }

    int targetId = -1;
    if (m_currentFilterType == "category") {
        targetId = m_currentFilterValue.toInt();
    }

    // [NEW] 智能落点检测：如果拖拽到侧边栏的具体分类上，则将该分类作为目标
    // [COMPAT] 适配 Qt6：使用 event->position().toPoint() 替换已废弃的 event->pos()
    QPoint globalDropPos = mapToGlobal(event->position().toPoint());
    
    auto checkTree = [&](QTreeView* tree) {
        if (tree && tree->isVisible()) {
            // [ROBUST] 使用 viewport 坐标进行索引检测，确保滚动状态下定位准确
            QPoint viewportPos = tree->viewport()->mapFromGlobal(globalDropPos);
            if (tree->viewport()->rect().contains(viewportPos)) {
                QModelIndex idx = tree->indexAt(viewportPos);
                if (idx.isValid() && idx.data(CategoryModel::TypeRole).toString() == "category") {
                    targetId = idx.data(CategoryModel::IdRole).toInt();
                    return true;
                }
            }
        }
        return false;
    };

    if (!checkTree(m_systemTree)) {
        checkTree(m_partitionTree);
    }

    QString itemType = "text";
    QString title;
    QString content;
    QByteArray dataBlob;
    QStringList tags;

    QStringList localPaths = StringUtils::extractLocalPathsFromMime(mime);
    if (!localPaths.isEmpty()) {
        FileStorageHelper::processImport(localPaths, targetId);
        event->acceptProposedAction();
        return;
    }

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
    if (event->key() == Qt::Key_Escape) {
        // [MODIFIED] 两段式退出：如果当前有任何输入框获焦，在 eventFilter 中已处理。
        // 若能走到这里，说明当前没有活跃编辑，则允许隐藏窗口。
        hide();
        return;
    }
    QWidget::keyPressEvent(event);
}

static bool copyRecursively(const QString& srcPath, const QString& dstPath) {
    QFileInfo srcInfo(srcPath);
    if (srcInfo.isDir()) {
        if (!QDir().mkpath(dstPath)) return false;
        QDir srcDir(srcPath);
        QStringList entries = srcDir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString& entry : entries) {
            if (!copyRecursively(srcPath + "/" + entry, dstPath + "/" + entry)) return false;
        }
        return true;
    } else {
        return QFile::copy(srcPath, dstPath);
    }
}

void QuickWindow::doExportCategory(int catId, const QString& catName) {
    if (!verifyExportPermission()) return;
    FileStorageHelper::exportCategory(catId, catName, this);
}

void QuickWindow::doImportCategory(int catId) {
    QStringList files = QFileDialog::getOpenFileNames(this, "选择导入文件", "", "所有文件 (*.*);;CSV文件 (*.csv)");
    if (files.isEmpty()) return;

    int totalCount = FileStorageHelper::processImport(files, catId);
    
    refreshData();
    refreshSidebar();
    ToolTipOverlay::instance()->showText(QCursor::pos(), QString("<b style='color: #2ecc71;'>[OK] 导入完成，共处理 %1 个项目</b>").arg(totalCount));
}

void QuickWindow::doImportFolder(int catId) {
    // 2026-03-xx 按照用户最高要求修复傻逼逻辑：升级原生单选为多选文件夹导入，彻底提升效率
    QFileDialog dialog(this);
    dialog.setWindowTitle("选择导入文件夹 (可多选)");
    dialog.setFileMode(QFileDialog::Directory);
    dialog.setOption(QFileDialog::ShowDirsOnly, true);
    dialog.setOption(QFileDialog::DontUseNativeDialog, true); // 强制使用非原生对话框以支持多选

    // 允许在文件视图中多选
    QListView *listView = dialog.findChild<QListView*>("listView");
    if (listView) listView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    QTreeView *treeView = dialog.findChild<QTreeView*>("treeView");
    if (treeView) treeView->setSelectionMode(QAbstractItemView::ExtendedSelection);

    if (dialog.exec() != QDialog::Accepted) return;
    QStringList dirs = dialog.selectedFiles();
    if (dirs.isEmpty()) return;

    int totalCount = FileStorageHelper::processImport(dirs, catId);
    
    refreshData();
    refreshSidebar();
    ToolTipOverlay::instance()->showText(QCursor::pos(), QString("<b style='color: #2ecc71;'>[OK] 批量导入完成，共处理 %1 个目录共 %2 个项目</b>").arg(dirs.size()).arg(totalCount));
}

void QuickWindow::updateFocusLines() {
    QWidget* focus = QApplication::focusWidget();
    // [USER_REQUEST] 2026-03-13 修复逻辑：只有在侧边栏展开（可见且宽度大于10px）时，才显示焦点线。
    // 这可以确保侧边栏折叠或隐藏模式下界面的绝对纯净，零视觉干扰。
    bool sidebarVisible = m_systemTree->parentWidget() && m_systemTree->parentWidget()->isVisible() && m_systemTree->parentWidget()->width() > 10;
    
    bool listFocus = (focus == m_listView) && sidebarVisible;
    bool sidebarFocus = (focus == m_systemTree || focus == m_partitionTree) && sidebarVisible;

    if (m_listFocusLine) m_listFocusLine->setVisible(listFocus);
    if (m_sidebarFocusLine) m_sidebarFocusLine->setVisible(sidebarFocus);
}

bool QuickWindow::eventFilter(QObject* watched, QEvent* event) {
    // [CRITICAL] 抢占式拦截：在快捷键系统处理前捕获 Ctrl+S 和 Ctrl+Alt+S
    if (event->type() == QEvent::ShortcutOverride) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        // [MODIFIED] 精确抢占：仅拦截 Ctrl+Alt+S 和 纯 Ctrl+S。
        // 确保 Ctrl+Shift+S 能够正常流转至 QShortcut 系统。
        if (keyEvent->key() == Qt::Key_S && (keyEvent->modifiers() & Qt::ControlModifier)) {
            if (keyEvent->modifiers() & Qt::AltModifier || !(keyEvent->modifiers() & Qt::ShiftModifier)) {
                event->accept();
                return true;
            }
        }
    }

    if (event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);

        // [DEBUG] 追踪按键流：打印按键、修饰键以及当前焦点所在的组件名
        qDebug() << "[TRACE-QW] KeyPress:" << QKeySequence(keyEvent->key()).toString() 
                 << "Mods:" << keyEvent->modifiers() 
                 << "FocusWidget:" << (watched ? watched->objectName() : "None");

        // [MODIFIED] 2026-03-xx 顶级物理拦截逻辑：修正 Ctrl+S 与 Ctrl+Alt+S 冲突
        // 确保显示/隐藏逻辑优先级，并严格区分锁定指令。
        if (keyEvent->key() == Qt::Key_S && (keyEvent->modifiers() & Qt::ControlModifier)) {
            auto mods = keyEvent->modifiers();
            
            // 情况 A: Ctrl + Alt + S -> 切换加锁分类显示/隐藏
            if (mods & Qt::AltModifier) {
                qDebug() << "[QuickWindow] 物理拦截捕获到 Ctrl+Alt+S, 切换显示/隐藏。";
                auto& db = DatabaseManager::instance();
                db.toggleLockedCategoriesVisibility();
                bool isHidden = db.isLockedCategoriesHidden();
                
                // 漂移保护：如果当前选中的分类被隐藏了，自动切回“全部数据”
                if (isHidden && m_currentFilterType == "category" && m_currentFilterValue != -1) {
                    int catId = m_currentFilterValue.toInt();
                    if (db.isCategoryLocked(catId) || !db.getCategoryPresetTags(catId).isNull()) { 
                         m_currentFilterType = "all";
                         m_currentFilterValue = -1;
                    }
                }
                
                refreshSidebar();
                refreshData();
                ToolTipOverlay::instance()->showText(QCursor::pos(), isHidden ? 
                    "<b style='color: #e67e22;'>[OK] 已隐藏加锁分类并强制重锁</b>" : 
                    "<b style='color: #2ecc71;'>[OK] 已显示所有分类并强制重锁</b>");
                return true;
            }
            
            // 情况 B: 纯 Ctrl + S -> 立即锁定当前分类 (排除 Shift)
            if (!(mods & Qt::ShiftModifier)) {
                qDebug() << "[QuickWindow] 物理拦截捕获到 Ctrl+S, 准备执行上锁。";
                int catId = -1;
                QModelIndex sidebarIdx = m_partitionTree->currentIndex();
                if (sidebarIdx.isValid() && sidebarIdx.data(CategoryModel::TypeRole).toString() == "category") {
                    catId = sidebarIdx.data(CategoryModel::IdRole).toInt();
                }
                if (catId == -1 && m_currentFilterType == "category" && m_currentFilterValue != -1) {
                    catId = m_currentFilterValue.toInt();
                }

                if (catId != -1) {
                    DatabaseManager::instance().lockCategory(catId);
                    if (m_currentFilterType == "category" && m_currentFilterValue == catId) {
                        m_currentFilterType = "all";
                        m_currentFilterValue = -1;
                    }
                    refreshSidebar();
                    refreshData();
                    ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #f39c12;'>[OK] 分类已物理锁定</b>");
                    return true;
                }
            }
        }
    }

    // [MODIFIED] 2026-03-xx 物理级拦截原生 ToolTip，确保本项目中原生黑色提示彻底绝迹
    if (event->type() == QEvent::ToolTip) {
        QString text = watched->property("tooltipText").toString();
        if (!text.isEmpty()) {
            // 2026-03-xx 按照用户要求，按钮/组件 ToolTip 持续时间设为 2 秒 (2000ms)
            ToolTipOverlay::instance()->showText(QCursor::pos(), text, 2000);
        }
        return true; 
    }

    // [USER_REQUEST] 监听自身的激活事件，确保无论通过何种方式（点击、热键、悬浮球）激活窗口时，
    // 都能实时刷新外部目标窗口句柄，确保后续粘贴功能的动态准确性。
    if (watched == this && event->type() == QEvent::WindowActivate) {
        recordLastActiveWindow(nullptr);
    }

    if (event->type() == QEvent::HoverEnter) {
        QString text = watched->property("tooltipText").toString();
        if (!text.isEmpty()) {
            // 2026-03-xx 按照用户要求，按钮/组件 ToolTip 持续时间设为 2 秒 (2000ms)
            ToolTipOverlay::instance()->showText(QCursor::pos(), text, 2000);
        }
    } else if (event->type() == QEvent::HoverLeave) {
        ToolTipOverlay::hideTip();
    }

    // [NEW] 2026-03-xx 按照用户要求：滚轮触底/触顶自动翻页逻辑
    if (watched == m_listView && event->type() == QEvent::Wheel) {
        QWheelEvent* wheelEvent = static_cast<QWheelEvent*>(event);
        int delta = wheelEvent->angleDelta().y();
        QScrollBar* vb = m_listView->verticalScrollBar();

        // 防抖：限制翻页频率（600ms）
        if (!m_lastWheelPageTimer.isValid() || m_lastWheelPageTimer.elapsed() > 600) {
            // 向下滚动且已到最底部 -> 下一页
            if (delta < 0 && vb->value() >= vb->maximum()) {
                if (m_currentPage < m_totalPages) {
                    m_currentPage++;
                    refreshData();
                    m_lastWheelPageTimer.start();
                    // 2026-03-xx 按照用户要求：滚轮翻页提示时长设为 2s (2000ms)
                    ToolTipOverlay::instance()->showText(QCursor::pos(), 
                        QString("<b style='color: #2ecc71;'>[下一页] 第 %1 / %2 页</b>").arg(m_currentPage).arg(m_totalPages), 2000);
                    return true;
                }
            }
            // 向上滚动且已到最顶部 -> 上一页
            else if (delta > 0 && vb->value() <= vb->minimum()) {
                if (m_currentPage > 1) {
                    m_currentPage--;
                    refreshData();
                    m_lastWheelPageTimer.start();
                    // 2026-03-xx 按照用户要求：滚轮翻页提示时长设为 2s (2000ms)
                    ToolTipOverlay::instance()->showText(QCursor::pos(), 
                        QString("<b style='color: #3498db;'>[上一页] 第 %1 / %2 页</b>").arg(m_currentPage).arg(m_totalPages), 2000);
                    // 翻到上一页后，将滚动条设到最底部，方便连续向上滚动
                    QTimer::singleShot(10, [vb](){ vb->setValue(vb->maximum()); });
                    return true;
                }
            }
        }
    }

    if (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut) {
        updateFocusLines();
        
        // [MODIFIED] 2026-04-xx 按照用户要求：强化临时模式自动收起逻辑，提升响应灵敏度。
        // 当焦点进入搜索框或列表视图，且侧边栏处于“自动收起”状态时，若侧边栏当前可见，则立即将其隐藏。
        // 此处不再直接调用 toggleSidebar 以防逻辑反向触发（如果本身是隐藏态），而是显式判断可见性。
        if (event->type() == QEvent::FocusIn && !m_isSidebarPersistent) {
            if (watched == m_searchEdit || watched == m_listView) {
                if (m_sidebarWrapper && m_sidebarWrapper->isVisible()) {
                    toggleSidebar();
                }
            }
        }
    }

    // [MODIFIED] 2026-03-xx 按照用户最终指令：按钮右键切换模式，左键仅执行显隐切换
    if (watched->objectName() == "btnSidebar") {
        if (event->type() == QEvent::MouseButtonPress) {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::RightButton) {
                QMenu menu(this);
                menu.setStyleSheet(R"(
                    QMenu { background-color: #252526; border: 1px solid #454545; color: #cccccc; padding: 4px; }
                    QMenu::item { padding: 4px 20px; border-radius: 4px; }
                    QMenu::item:selected { background-color: #094771; color: white; }
                    QMenu::item:checked { color: #2ecc71; font-weight: bold; }
                )");
                
                QAction* autoFold = menu.addAction("自动收起");
                autoFold->setCheckable(true);
                autoFold->setChecked(!m_isSidebarPersistent);
                
                QAction* manualFold = menu.addAction("人工收起");
                manualFold->setCheckable(true);
                manualFold->setChecked(m_isSidebarPersistent);
                
                connect(autoFold, &QAction::triggered, this, [this](){
                    m_isSidebarPersistent = false;
                    QSettings settings("RapidNotes", "QuickWindow");
                    settings.setValue("sidebarPersistent", false);

                    // 2026-04-xx 按照用户要求：切换到自动模式后立即检查焦点状态，实现瞬时响应
                    QWidget* focus = QApplication::focusWidget();
                    if ((focus == m_searchEdit || focus == m_listView) && m_sidebarWrapper->isVisible()) {
                        toggleSidebar();
                    }

                    ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #e67e22;'>[OK] 已切换为自动收起模式</b>", 1500);
                });
                
                connect(manualFold, &QAction::triggered, this, [this](){
                    m_isSidebarPersistent = true;
                    QSettings settings("RapidNotes", "QuickWindow");
                    settings.setValue("sidebarPersistent", true);
                    ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #2ecc71;'>[OK] 已切换为人工收起模式</b>", 1500);
                });
                
                menu.exec(mouseEvent->globalPosition().toPoint());
                return true;
            }
        }
        // 屏蔽双击事件，确保交互单一化
        if (event->type() == QEvent::MouseButtonDblClick) return true;
    }

    // 逻辑 1: 鼠标移动到列表或侧边栏范围内，立即恢复正常光标
    // 2026-03-xx 按照用户要求：优化全域光标与重绘触发，解决工具栏空白处点击后的响应滞后
    QWidget* customToolbar = findChild<QWidget*>("customToolbar");
    if (watched == m_listView || watched == m_systemTree || watched == m_partitionTree || watched == customToolbar) {
        if (event->type() == QEvent::MouseMove || event->type() == QEvent::Enter) {
            setCursor(Qt::ArrowCursor);
            // 强制触发一次重绘，消除 CSS Hover 状态“粘连”
            if (watched == customToolbar) update();
        }
    }

    // 逻辑 2: 侧边栏点击分类且不释放左键时，显示手指光标
    if ((watched == m_partitionTree || watched == m_systemTree) && event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        int key = keyEvent->key();
        auto modifiers = keyEvent->modifiers();

        // 【新增需求】波浪键/Backspace 快捷回到全部数据视图
        if (key == Qt::Key_QuoteLeft || key == Qt::Key_Backspace) {
            m_currentFilterType = "all";
            m_currentFilterValue = -1;
            m_currentPage = 1;

            m_systemTree->selectionModel()->clearSelection();
            m_systemTree->setCurrentIndex(QModelIndex());
            m_partitionTree->selectionModel()->clearSelection();
            m_partitionTree->setCurrentIndex(QModelIndex());

            updatePartitionStatus("");
            applyListTheme("");
            refreshData();
            ToolTipOverlay::instance()->showText(QCursor::pos(), "[OK] 已切换至全部数据");
            return true;
        }

        if (key == Qt::Key_F2) {
            if (watched == m_partitionTree) {
                QModelIndex current = m_partitionTree->currentIndex();
                if (current.isValid() && current.data(CategoryModel::TypeRole).toString() == "category") {
                    // [CRITICAL] 锁定：统一使用行内编辑模式，严禁改为弹出对话框，以保持各窗口逻辑一致性
                    m_partitionTree->edit(current);
                }
            }
            return true;
        }

        if (key == Qt::Key_Escape) {
            m_listView->setFocus();
            return true;
        }

        // [USER_REQUEST] 焦点切换快捷键从 Shift 改为 Tab。
        // [MODIFIED] 仅在侧边栏显示时才执行自定义 Tab 切换逻辑
        if (key == Qt::Key_Tab && (watched == m_partitionTree || watched == m_systemTree) && 
            m_systemTree->parentWidget()->isVisible()) {
            // [CRITICAL] 侧边栏 -> 列表焦点切换：自动选中首项或恢复当前选中项
            m_listView->setFocus();
            auto* model = m_listView->model();
            if (model && !m_listView->currentIndex().isValid() && model->rowCount() > 0) {
                m_listView->setCurrentIndex(model->index(0, 0));
            }
            return true;
        }

        if (key == Qt::Key_Delete) {
            if (watched == m_partitionTree) {
                auto selected = m_partitionTree->selectionModel()->selectedIndexes();
                if (!selected.isEmpty()) {
                    QString msg = selected.size() > 1 ? QString("确定要删除选中的 %1 个分类及其下所有内容吗？").arg(selected.size()) : "确定要删除选中的分类及其下所有内容吗？";
                    FramelessMessageBox dlg("确认删除", msg, this);
                    if (dlg.exec() == QDialog::Accepted) {
                        QList<int> ids;
                        for (const auto& idx : selected) {
                            if (idx.data(CategoryModel::TypeRole).toString() == "category") {
                                ids << idx.data(CategoryModel::IdRole).toInt();
                            }
                        }
                        DatabaseManager::instance().softDeleteCategories(ids);
                        refreshSidebar();
                        refreshData();
                    }
                }
            } else if (watched == m_systemTree) {
                QModelIndex index = m_systemTree->currentIndex();
                if (index.isValid()) {
                    QString type = index.data(CategoryModel::TypeRole).toString();
                    if (type == "trash") {
                        FramelessMessageBox dlg("确认清空", "确定要永久删除回收站中的所有内容吗？\n(此操作不可逆)", this);
                        if (dlg.exec() == QDialog::Accepted) {
                            DatabaseManager::instance().emptyTrash();
                            refreshData();
                            refreshSidebar();
                        }
                    }
                }
            }
            return true;
        }

        // 2026-03-xx 按照用户要求，解决快捷键冲突：
        // 侧边栏模式下的 Alt+Up/Down 及其组合键负责分类的逐级/置顶/置底移动。
        if ((key == Qt::Key_Up || key == Qt::Key_Down) && (modifiers & Qt::AltModifier)) {
            QModelIndex current = (watched == m_partitionTree) ? m_partitionTree->currentIndex() : m_systemTree->currentIndex();
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

    if (watched == m_listView && event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        auto modifiers = keyEvent->modifiers();
        int key = keyEvent->key();

        // [MODIFIED] 2026-03-11 强制拦截 Ctrl+C 优先级：锁定在 QListView 内部处理之前
        // 彻底根除系统默认逻辑自动抓取 DisplayRole (标题) 的行为。
        if (key == Qt::Key_C && (modifiers & Qt::ControlModifier)) {
            doExtractContent();
            return true; // 拦截，严禁传递给原生逻辑
        }

        // 【新增需求】波浪键/Backspace 快捷回到全部数据视图
        if (keyEvent->key() == Qt::Key_QuoteLeft || keyEvent->key() == Qt::Key_Backspace) {
            m_currentFilterType = "all";
            m_currentFilterValue = -1;
            m_currentPage = 1;

            m_systemTree->selectionModel()->clearSelection();
            m_systemTree->setCurrentIndex(QModelIndex());
            m_partitionTree->selectionModel()->clearSelection();
            m_partitionTree->setCurrentIndex(QModelIndex());

            updatePartitionStatus("");
            applyListTheme("");
            refreshData();
            ToolTipOverlay::instance()->showText(QCursor::pos(), "[OK] 已切换至全部数据");
            return true;
        }
        
        // 2026-03-xx 按照用户要求，统一快捷键冲突管控：
        // 列表模式下的 Alt+Up/Down 负责移动笔记项目，以此释放侧边栏对该组合键的独占响应。
        if ((key == Qt::Key_Up || key == Qt::Key_Down) && (modifiers & Qt::AltModifier)) {
            DatabaseManager::MoveDirection dir;
            if (modifiers & Qt::ShiftModifier) dir = (key == Qt::Key_Up) ? DatabaseManager::Top : DatabaseManager::Bottom;
            else if (modifiers & Qt::ControlModifier) dir = (key == Qt::Key_Up) ? DatabaseManager::Top : DatabaseManager::Bottom;
            else dir = (key == Qt::Key_Up) ? DatabaseManager::Up : DatabaseManager::Down;
            
            doMoveNote(dir);
            return true;
        }
    }

    if ((watched == m_listView || watched == m_searchEdit || watched == m_catSearchEdit || watched == m_tagEdit || watched == m_pageInput) && event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);

        // [NEW] 单行输入框箭头导航逻辑：↑ 移至首部，↓ 移至尾部
        if (watched == m_searchEdit || watched == m_catSearchEdit || watched == m_tagEdit || watched == m_pageInput) {
            if (keyEvent->key() == Qt::Key_Up) {
                QLineEdit* edit = qobject_cast<QLineEdit*>(watched);
                if (edit) {
                    edit->setCursorPosition(0);
                    return true;
                }
            } else if (keyEvent->key() == Qt::Key_Down) {
                QLineEdit* edit = qobject_cast<QLineEdit*>(watched);
                if (edit) {
                    edit->setCursorPosition(edit->text().length());
                    return true;
                }
            }
        }

        if (keyEvent->key() == Qt::Key_F2 && watched == m_listView) {
            QModelIndex current = m_listView->currentIndex();
            if (current.isValid()) {
                QString oldTitle = current.data(NoteModel::TitleRole).toString();
                int noteId = current.data(NoteModel::IdRole).toInt();
                TitleEditorDialog dlg(oldTitle, this);
                if (dlg.exec() == QDialog::Accepted) {
                    QString newTitle = dlg.getText();
                    if (!newTitle.isEmpty() && newTitle != oldTitle) {
                        DatabaseManager::instance().updateNoteState(noteId, "title", newTitle);
                        refreshData();
                    }
                }
            }
            return true;
        }

        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            if (watched == m_listView) {
                activateNote(m_listView->currentIndex());
                return true;
            }
        }

        // [USER_REQUEST] 焦点切换快捷键从 Shift 改为 Tab
        // [MODIFIED] 仅在侧边栏显示时才执行自定义 Tab 切换逻辑
        if (keyEvent->key() == Qt::Key_Tab && watched == m_listView && m_systemTree->parentWidget()->isVisible()) {
            // [CRITICAL] 列表 -> 侧边栏焦点切换：跳转至当前激活分类或用户分类首项
            if (m_partitionTree->isVisible()) {
                m_partitionTree->setFocus();
                if (!m_partitionTree->currentIndex().isValid()) {
                    m_partitionTree->setCurrentIndex(m_partitionProxyModel->index(0, 0));
                }
            } else if (m_systemTree->isVisible()) {
                m_systemTree->setFocus();
            }
            return true;
        }

        // [MODIFIED] 2026-03-xx 按照用户要求：强化 1/2 数字键翻页与导航逻辑
        if (watched == m_listView && !(keyEvent->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier))) {
            if (keyEvent->key() == Qt::Key_1) {
                if (m_model->rowCount() > 0) {
                    QModelIndex current = m_listView->currentIndex();
                    // 若当前已在第一行，则触发上一页
                    if (current.isValid() && current.row() == 0) {
                        if (m_currentPage > 1) {
                            m_currentPage--;
                        } else {
                            m_currentPage = m_totalPages; // 回环至末页
                        }
                        refreshData();
                        ToolTipOverlay::instance()->showText(QCursor::pos(), 
                            QString("<b style='color: #3498db;'>[上一页] 第 %1 / %2 页</b>").arg(m_currentPage).arg(m_totalPages), 2000);
                        // 定位到新页面的最后一行
                        if (m_model->rowCount() > 0) {
                            QModelIndex lastIdx = m_model->index(m_model->rowCount() - 1, 0);
                            m_listView->setCurrentIndex(lastIdx);
                            m_listView->scrollTo(lastIdx);
                        }
                    } else {
                        // 否则跳转到本页首行
                        QModelIndex firstIdx = m_model->index(0, 0);
                        m_listView->setCurrentIndex(firstIdx);
                        m_listView->scrollTo(firstIdx);
                    }
                }
                return true;
            } else if (keyEvent->key() == Qt::Key_2) {
                if (m_model->rowCount() > 0) {
                    QModelIndex current = m_listView->currentIndex();
                    // 若当前已在最后一行，则触发下一页
                    if (current.isValid() && current.row() == m_model->rowCount() - 1) {
                        if (m_currentPage < m_totalPages) {
                            m_currentPage++;
                        } else {
                            m_currentPage = 1; // 回环至首页
                        }
                        refreshData();
                        ToolTipOverlay::instance()->showText(QCursor::pos(), 
                            QString("<b style='color: #2ecc71;'>[下一页] 第 %1 / %2 页</b>").arg(m_currentPage).arg(m_totalPages), 2000);
                        // 定位到新页面的第一行
                        if (m_model->rowCount() > 0) {
                            QModelIndex firstIdx = m_model->index(0, 0);
                            m_listView->setCurrentIndex(firstIdx);
                            m_listView->scrollTo(firstIdx);
                        }
                    } else {
                        // 否则跳转到本页末行
                        QModelIndex lastIdx = m_model->index(m_model->rowCount() - 1, 0);
                        m_listView->setCurrentIndex(lastIdx);
                        m_listView->scrollTo(lastIdx);
                    }
                }
                return true;
            } else if (keyEvent->key() == Qt::Key_3) { // [NEW] 向上导航
                QModelIndex current = m_listView->currentIndex();
                if (!current.isValid() && m_model->rowCount() > 0) {
                    m_listView->setCurrentIndex(m_model->index(0, 0));
                    return true;
                }
                
                int row = current.row();
                if (row > 0) {
                    QModelIndex prevIdx = m_model->index(row - 1, 0);
                    m_listView->setCurrentIndex(prevIdx);
                    m_listView->scrollTo(prevIdx);
                } else {
                    // [USER_REQUEST] 强化 3/4 键逻辑：支持跨页回环。
                    // 若已是首页首条，则跳往末页末条；否则跳往上一页末条。
        // 这一改进解决了在分页模式下无法利用 3/4 键在全局数据中闭环导航的痛点。
                    if (m_currentPage > 1) {
                        m_currentPage--;
                    } else {
                        m_currentPage = m_totalPages;
                        ToolTipOverlay::instance()->showText(QCursor::pos(), "已回环至末页");
                    }
                    refreshData();
                    if (m_model->rowCount() > 0) {
                        QModelIndex lastIdx = m_model->index(m_model->rowCount() - 1, 0);
                        m_listView->setCurrentIndex(lastIdx);
                        m_listView->scrollTo(lastIdx);
                    }
                }
                return true;
            } else if (keyEvent->key() == Qt::Key_4) { // [NEW] 向下导航
                QModelIndex current = m_listView->currentIndex();
                if (!current.isValid() && m_model->rowCount() > 0) {
                    m_listView->setCurrentIndex(m_model->index(0, 0));
                    return true;
                }

                int row = current.row();
                if (row < m_model->rowCount() - 1) {
                    QModelIndex nextIdx = m_model->index(row + 1, 0);
                    m_listView->setCurrentIndex(nextIdx);
                    m_listView->scrollTo(nextIdx);
                } else {
                    // [USER_REQUEST] 强化 3/4 键逻辑：支持跨页回环。
                    // 若已是末页末条，则跳往首页首条；否则跳往下一页首条。
        // 这一改进确保了用户可以利用单键向下贯穿整个数据库，并在末尾自动跳转回起始点。
                    if (m_currentPage < m_totalPages) {
                        m_currentPage++;
                    } else {
                        m_currentPage = 1;
                        ToolTipOverlay::instance()->showText(QCursor::pos(), "已回环至首页");
                    }
                    refreshData();
                    if (m_model->rowCount() > 0) {
                        QModelIndex firstIdx = m_model->index(0, 0);
                        m_listView->setCurrentIndex(firstIdx);
                        m_listView->scrollTo(firstIdx);
                    }
                }
                return true;
            }
        }


        if (keyEvent->key() == Qt::Key_Escape) {
            if (watched == m_searchEdit || watched == m_catSearchEdit || watched == m_tagEdit || watched == m_pageInput) {
                // [CRITICAL] 锁定：所有输入框按下 Esc 时，采用两段式：不为空则清空，为空则切换焦点
                QLineEdit* edit = qobject_cast<QLineEdit*>(watched);
                if (edit && !edit->text().isEmpty()) {
                    edit->clear();
                } else {
                    m_listView->setFocus();
                }
                return true;
            }
            // [MODIFIED] 非输入框焦点下，事件将冒泡至 keyPressEvent 处理 hide()
        }
    }
    return QWidget::eventFilter(watched, event);
}

#include "QuickWindow.moc"