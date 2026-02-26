#include "ToolTipOverlay.h"
#include "QuickWindow.h"
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
#include <QUrl>
#include <QBuffer>
#include <QToolTip>
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
#include <QToolTip>
#include "FramelessDialog.h"
#include "CategoryPasswordDialog.h"
#include "SettingsWindow.h"
#include "OCRResultWindow.h"
#include "../core/ShortcutManager.h"
#include "../core/OCRManager.h"
#include <QRandomGenerator>
#include <QStyledItemDelegate>
#include <QPainter>
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

#ifdef Q_OS_WIN
    StringUtils::applyTaskbarMinimizeStyle((void*)winId());
#endif

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
        
        scheduleRefresh();
    });

#ifdef Q_OS_WIN
    m_monitorTimer = new QTimer(this);
    m_monitorTimer->setInterval(200);
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

    m_listStack = new QStackedWidget();
    m_listStack->setMinimumWidth(95); // 确保 117px 左边距
    
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
    m_listView->setContextMenuPolicy(Qt::CustomContextMenu);

    m_lockWidget = new CategoryLockWidget(this);
    connect(m_lockWidget, &CategoryLockWidget::unlocked, this, [this](){
        refreshData();
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

    auto* sidebarContainer = new QWidget();
    sidebarContainer->setMinimumWidth(163); // 侧边栏宽度不能小于 163 像素
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
    m_partitionTree->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_partitionTree->setDragEnabled(true);
    m_partitionTree->setAcceptDrops(true);
    m_partitionTree->setDropIndicatorShown(true);
    m_partitionTree->setDragDropMode(QAbstractItemView::InternalMove);
    m_partitionTree->setDefaultDropAction(Qt::MoveAction);
    m_partitionTree->expandAll();
    m_partitionTree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_partitionTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_partitionTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_partitionTree, &QTreeView::customContextMenuRequested, this, &QuickWindow::showSidebarMenu);
    // [CRITICAL] 严禁在此处连接 doubleClicked 手动切换展开/折叠，因为 QTreeView 默认已具备此行为。
    // 手动连接会导致状态切换两次（原生+手动），从而出现双击无响应的逻辑抵消问题。

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

    m_listStack->addWidget(m_listView);
    m_listStack->addWidget(m_lockWidget);

    m_splitter->addWidget(m_listStack);
    m_splitter->addWidget(sidebarContainer);
    m_splitter->setCollapsible(0, false); // 禁止折叠列表区域
    m_splitter->setCollapsible(1, false); // 禁止折叠侧边栏
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 0);
    m_splitter->setSizes({550, 163});
    leftLayout->addWidget(m_splitter);

    // --- 底部状态栏与标签输入框 ---
    auto* bottomLayout = new QHBoxLayout();
    bottomLayout->setContentsMargins(2, 0, 10, 5);
    bottomLayout->setSpacing(10);

    m_statusLabel = new QLabel("当前分区: 全部数据");
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
        // 仅对“我的分区”执行过滤，固定分类保持常驻显示
        m_partitionProxyModel->setFilterFixedString(text);
        m_partitionTree->expandAll();
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
        btn->setToolTip(tooltip);
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
    btnPin->setStyleSheet("QPushButton:checked { background-color: #FF551C; }");
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

    m_pageInput = new QLineEdit("1");
    m_pageInput->setObjectName("pageInput");
    m_pageInput->setAlignment(Qt::AlignCenter);
    m_pageInput->setFixedSize(28, 20);
    m_pageInput->installEventFilter(this);
    connect(m_pageInput, &QLineEdit::returnPressed, [this](){
        int p = m_pageInput->text().toInt();
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
    toolLayout->addWidget(m_pageInput, 0, Qt::AlignHCenter);
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
    
    // [PERFORMANCE] 预热预览窗单例，消除首次通过空格键打开时的构造延迟
    QTimer::singleShot(500, []() {
        QuickPreview::instance();
    });
    
    // 初始大小和最小大小
    resize(900, 630);
    setMinimumSize(400, 300);

    auto* preview = QuickPreview::instance();
    connect(preview, &QuickPreview::editRequested, this, [this, preview](int id){
        if (!preview->caller() || preview->caller()->window() != this) return;
        this->doEditNote(id);
    });
    connect(preview, &QuickPreview::prevRequested, this, [this, preview](){
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
    connect(preview, &QuickPreview::nextRequested, this, [this, preview](){
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
    connect(preview, &QuickPreview::historyNavigationRequested, this, [this, preview](int id){
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
            preview->showPreview(
                id,
                note.value("title").toString(),
                note.value("content").toString(),
                note.value("item_type").toString(),
                note.value("data_blob").toByteArray(),
                preview->pos(),
                "",
                m_listView
            );
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
    restoreState();
    refreshData();
    applyListTheme(""); // 【核心修复】初始化时即应用深色主题
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
    add("qw_lock_item", [this](){ doLockSelected(); });
    add("qw_new_idea", [this](){ doNewIdea(); });
    add("qw_select_all", [this](){ m_listView->selectAll(); });
    add("qw_extract", [this](){ doExtractContent(); });
    add("qw_lock_cat", [this](){
        if (m_currentFilterType == "category" && m_currentFilterValue != -1) {
            DatabaseManager::instance().lockCategory(m_currentFilterValue.toInt());
            refreshSidebar();
            refreshData();
        }
    });
    add("qw_stay_on_top", [this](){ toggleStayOnTop(!m_isStayOnTop); });
    add("qw_toggle_main", [this](){ emit toggleMainWindowRequested(); });
    add("qw_toolbox", [this](){ emit toolboxRequested(); });
    add("qw_edit", [this](){ doEditSelected(); });
    add("qw_sidebar", [this](){ toggleSidebar(); });
    add("qw_prev_page", [this](){ if(m_currentPage > 1) { m_currentPage--; refreshData(); } });
    add("qw_next_page", [this](){ if(m_currentPage < m_totalPages) { m_currentPage++; refreshData(); } });
    add("qw_copy_tags", [this](){ doCopyTags(); });
    add("qw_paste_tags", [this](){ doPasteTags(); });
    
    for (int i = 0; i <= 5; ++i) {
        add(QString("qw_rating_%1").arg(i), [this, i](){ doSetRating(i); });
    }
}

void QuickWindow::updateShortcuts() {
    for (auto* sc : m_shortcuts) {
        QString id = sc->property("id").toString();
        sc->setKey(ShortcutManager::instance().getShortcut(id));
    }
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
    
    if (matches && m_currentPage == 1) {
        m_model->prependNote(note);
    }
    
    // 依然需要触发侧边栏计数刷新 (节流执行)
    scheduleRefresh();
}

void QuickWindow::refreshData() {
    if (!isVisible()) return;

    // 记忆当前选中的 ID 列表，以便在刷新后恢复多选状态
    QSet<int> selectedIds;
    auto selectedIndices = m_listView->selectionModel()->selectedIndexes();
    for (const auto& idx : selectedIndices) {
        selectedIds.insert(idx.data(NoteModel::IdRole).toInt());
    }
    int lastCurrentId = m_listView->currentIndex().data(NoteModel::IdRole).toInt();

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

    m_listStack->setCurrentWidget(isLocked ? static_cast<QWidget*>(m_lockWidget) : static_cast<QWidget*>(m_listView));

    auto* preview = QuickPreview::instance();
    if (isLocked && preview->isVisible() && preview->caller() && preview->caller()->window() == this) {
        preview->hide();
    }

    m_model->setNotes(isLocked ? QList<QVariantMap>() : DatabaseManager::instance().searchNotes(keyword, m_currentFilterType, m_currentFilterValue, m_currentPage, pageSize));
    
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
        // [CRITICAL] 锁定：过滤“我的分区”标题行，防止 IdRole=0 引起的异常调用链
        if (partIdx.data(Qt::DisplayRole).toString() != "我的分区") {
            selectedType = partIdx.data(CategoryModel::TypeRole).toString();
            selectedValue = partIdx.data(CategoryModel::IdRole);
        }
    }

    m_systemModel->refresh();
    m_partitionModel->refresh();
    m_partitionTree->expandAll();

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
            ToolTipOverlay::instance()->showText(QCursor::pos(), "⚠️ 文件已丢失或被移动");
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
                ToolTipOverlay::instance()->showText(QCursor::pos(), "⚠️ 原文件已丢失，已复制路径文本");
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
                ToolTipOverlay::instance()->showText(QCursor::pos(), QString("✔ 已永久删除 %1 条数据").arg(idsToDelete.size()));
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
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #e74c3c;'>✖ 尚未设定应用密码，请先进行设定</b>");
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
    
    ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #2ecc71;'>✔ 应用已锁定</b>");
}

void QuickWindow::updatePreviewContent() {
    QModelIndex index = m_listView->currentIndex();
    if (!index.isValid()) return;
    int id = index.data(NoteModel::IdRole).toInt();
    
    QThreadPool::globalInstance()->start([id]() {
        DatabaseManager::instance().recordAccess(id);
    });

    // [PERFORMANCE] 优先从内存模型中读取内容、标题和分类，避免同步查库导致的帧率抖动
    QString title = index.data(NoteModel::TitleRole).toString();
    QString content = index.data(NoteModel::ContentRole).toString();
    QString catName = index.data(NoteModel::CategoryNameRole).toString();
    QString itemType = index.data(NoteModel::TypeRole).toString();
    
    // 部分特殊类型（如图像大对象）可能仍需模型从库中拉取，但 NoteModel 已具备二级缓存逻辑
    auto* preview = QuickPreview::instance();
    QPoint pos = preview->isVisible() ? preview->pos() : m_listView->mapToGlobal(m_listView->rect().center()) - QPoint(250, 300);

    preview->showPreview(
        id,
        title, 
        content, 
        itemType,
        index.data(NoteModel::BlobRole).toByteArray(), 
        pos,
        catName,
        m_listView
    );
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
    IconHelper::setupMenu(&menu);
    menu.setStyleSheet("QMenu { background-color: #2D2D2D; color: #EEE; border: 1px solid #444; padding: 4px; } "
                       /* 10px 间距规范：padding-left 10px + icon margin-left 6px */
                       "QMenu::item { padding: 6px 10px 6px 10px; border-radius: 3px; } "
                       "QMenu::icon { margin-left: 6px; } "
                       "QMenu::item:selected { background-color: #4a90e2; color: white; }");

    if (selCount == 1) {
        menu.addAction(IconHelper::getIcon("eye", "#1abc9c", 18), "预览 (Space)", this, &QuickWindow::doPreview);
        
        QString content = selected.first().data(NoteModel::ContentRole).toString();
        QString type = selected.first().data(NoteModel::TypeRole).toString();
        
        if (type == "image") {
            menu.addAction(IconHelper::getIcon("screenshot_ocr", "#3498db", 18), "从图提取文字", this, &QuickWindow::doOCR);
        }

        // 智能检测网址并显示打开菜单
        QString firstUrl = StringUtils::extractFirstUrl(content);
        if (!firstUrl.isEmpty()) {
            menu.addAction(IconHelper::getIcon("link", "#3A90FF", 18), "打开网址", [firstUrl]() {
                QDesktopServices::openUrl(QUrl(firstUrl));
            });
        }

        // 如果是文件/文件夹路径，显示定位菜单
        if (type == "file" || type == "local_file" || type == "local_folder" || type == "local_batch") {
            QString path = content;
            if (path.startsWith("attachments/")) {
                path = QCoreApplication::applicationDirPath() + "/" + path;
            }
            menu.addAction(IconHelper::getIcon("folder", "#3A90FF", 18), "在资源管理器中显示", [path]() {
                StringUtils::locateInExplorer(path, true);
            });
        }
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
    menu.addAction(IconHelper::getIcon(isPinned ? "pin_vertical" : "pin_tilted", isPinned ? "#3A90FF" : "#aaaaaa", 18), 
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
        menu.addAction(IconHelper::getIcon("refresh", "#2ecc71", 18), "全部恢复", [this, selected](){
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
                       "QMenu::item:selected { background-color: #4a90e2; color: white; }");

    QString type = index.data(CategoryModel::TypeRole).toString();
    QString idxName = index.data(Qt::DisplayRole).toString();
    
    // [CRITICAL] 锁定：通过文本匹配“我的分区”来判定右键菜单弹出逻辑，支持新建分组
    if (!index.isValid() || idxName == "我的分区") {
        menu.addAction(IconHelper::getIcon("add", "#3498db", 18), "新建分组", [this]() {
            FramelessInputDialog dlg("新建分组", "组名称:", "", this);
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
        menu.addSeparator();
        auto* importMenu = menu.addMenu(IconHelper::getIcon("file_import", "#1abc9c", 18), "导入数据");
        importMenu->setStyleSheet(menu.styleSheet());
        importMenu->addAction(IconHelper::getIcon("file", "#1abc9c", 18), "导入文件(s)...", [this, catId]() {
            doImportCategory(catId);
        });
        importMenu->addAction(IconHelper::getIcon("folder", "#1abc9c", 18), "导入文件夹...", [this, catId]() {
            doImportFolder(catId);
        });
        menu.addAction(IconHelper::getIcon("file_export", "#3498db", 18), "导出此分类", [this, catId, currentName]() {
            doExportCategory(catId, currentName);
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
        menu.addAction(IconHelper::getIcon("add", "#3498db", 18), "新建分组", [this]() {
            FramelessInputDialog dlg("新建分组", "组名称:", "", this);
            if (dlg.exec() == QDialog::Accepted) {
                QString text = dlg.text();
                if (!text.isEmpty()) {
                    DatabaseManager::instance().addCategory(text);
                    refreshSidebar();
                }
            }
        });
        menu.addAction(IconHelper::getIcon("add", "#3498db", 18), "新建子分区", [this, catId]() {
            FramelessInputDialog dlg("新建子分区", "区名称:", "", this);
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
            menu.addAction(IconHelper::getIcon("edit", "#aaaaaa", 18), "重命名", [this, index]() {
                m_partitionTree->edit(index);
            });
        }

        QString delText = selected.size() > 1 ? QString("删除选中的 %1 个分类").arg(selected.size()) : "删除";
        menu.addAction(IconHelper::getIcon("trash", "#e74c3c", 18), delText, [this, selected]() {
            QString msg = selected.size() > 1 ? "确定要删除选中的分类及其子分类和笔记吗？" : "确定要删除此分类吗？其子分类和笔记也将移至回收站。";
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
                ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#2ecc71;'>✔ 排列已完成</b>");
        });
        sortMenu->addAction("标题(当前层级) (Z→A)", [this, parentId]() {
            if (DatabaseManager::instance().reorderCategories(parentId, false))
                ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#2ecc71;'>✔ 排列已完成</b>");
        });
        sortMenu->addAction("标题(全部) (A→Z)", [this]() {
            if (DatabaseManager::instance().reorderAllCategories(true))
                ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#2ecc71;'>✔ 全部排列已完成</b>");
        });
        sortMenu->addAction("标题(全部) (Z→A)", [this]() {
            if (DatabaseManager::instance().reorderAllCategories(false))
                ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#2ecc71;'>✔ 全部排列已完成</b>");
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
                        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #e74c3c;'>✖ 旧密码验证失败</b>");
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
                        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #e74c3c;'>✖ 密码错误</b>");
                    }
                }
            });
        });
        pwdMenu->addAction("立即锁定", [this, catId]() {
            DatabaseManager::instance().lockCategory(catId);
            refreshSidebar();
            refreshData();
        })->setShortcut(QKeySequence("Ctrl+Shift+L"));
    } else if (idxName == "未分类") {
        menu.addAction(IconHelper::getIcon("add", "#3498db", 18), "新建数据", [this]() {
            auto* win = new NoteEditWindow();
            connect(win, &NoteEditWindow::noteSaved, this, &QuickWindow::refreshData);
            win->show();
        });
    } else if (idxName == "回收站" || type == "trash") {
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

void QuickWindow::showToolboxMenu(const QPoint& pos) {
    QMenu menu(this);
    IconHelper::setupMenu(&menu);
    menu.setStyleSheet("QMenu { background-color: #2D2D2D; color: #EEE; border: 1px solid #444; padding: 4px; } "
                       /* 10px 间距规范：padding-left 10px + icon margin-left 6px */
                       "QMenu::item { padding: 6px 10px 6px 10px; border-radius: 3px; } "
                       "QMenu::icon { margin-left: 6px; } "
                       "QMenu::item:selected { background-color: #4a90e2; color: white; }");

    bool autoCat = DatabaseManager::instance().isAutoCategorizeEnabled();
    QString iconName = autoCat ? "switch_on" : "switch_off";
    QString iconColor = autoCat ? "#00A650" : "#000000";
    QAction* autoCatAction = menu.addAction(IconHelper::getIcon(iconName, iconColor, 18), "剪贴板自动归档到当前分类");
    autoCatAction->setCheckable(true);
    autoCatAction->setChecked(autoCat);
    connect(autoCatAction, &QAction::triggered, [this](bool checked){
        DatabaseManager::instance().setAutoCategorizeEnabled(checked);
        ToolTipOverlay::instance()->showText(QCursor::pos(), checked ? "✅ 剪贴板自动归档已开启" : "❌ 剪贴板自动归档已关闭");
    });

    menu.addSeparator();
    
    menu.addAction(IconHelper::getIcon("settings", "#aaaaaa", 18), "更多设置...", [this]() {
        auto* dlg = new SettingsWindow(this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        // 预定位：居中于当前极速窗口
        dlg->move(this->geometry().center() - dlg->rect().center());
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
    ToolTipOverlay::instance()->showText(QCursor::pos(), "✅ 标签已添加");
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
        ToolTipOverlay::instance()->showText(QCursor::pos(), "✅ 标签已更新");
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
    ToolTipOverlay::instance()->showText(QCursor::pos(), QString("✅ 已复制 %1 个标签").arg(tags.size()));
}

void QuickWindow::doOCR() {
    QModelIndex index = m_listView->currentIndex();
    if (!index.isValid()) return;

    int id = index.data(NoteModel::IdRole).toInt();
    QVariantMap note = DatabaseManager::instance().getNoteById(id);
    if (note.value("item_type").toString() != "image") return;

    QByteArray data = note.value("data_blob").toByteArray();
    QImage img;
    img.loadFromData(data);
    if (img.isNull()) return;

    auto* resWin = new OCRResultWindow(img, id);
    connect(&OCRManager::instance(), &OCRManager::recognitionFinished, resWin, &OCRResultWindow::setRecognizedText);
    
    QSettings settings("RapidNotes", "OCR");
    if (settings.value("autoCopy", false).toBool()) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), "⏳ 正在识别文字...");
    } else {
        resWin->show();
    }
    
    OCRManager::instance().recognizeAsync(img, id);
}

void QuickWindow::doPasteTags() {
    auto selected = m_listView->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;

    QStringList tagsToPaste = DatabaseManager::getTagClipboard();
    if (tagsToPaste.isEmpty()) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), "❌ 标签剪贴板为空");
        return;
    }

    // 直接覆盖标签 (符合粘贴语义)
    for (const auto& index : std::as_const(selected)) {
        int id = index.data(NoteModel::IdRole).toInt();
        DatabaseManager::instance().updateNoteState(id, "tags", tagsToPaste.join(", "));
    }

    refreshData();
    ToolTipOverlay::instance()->showText(QCursor::pos(), QString("✅ 已覆盖粘贴标签至 %1 条数据").arg(selected.size()));
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
    
#ifdef Q_OS_WIN
    if (m_monitorTimer) m_monitorTimer->start();
#endif

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
#ifdef Q_OS_WIN
    if (m_monitorTimer) m_monitorTimer->stop();
#endif

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
    QString dir = QFileDialog::getExistingDirectory(this, "选择导出目录", "");
    if (dir.isEmpty()) return;

    // 清理分类名中的非法文件名字符
    QString safeCatName = catName;
    safeCatName.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");
    QString exportPath = dir + "/" + safeCatName;
    QDir().mkpath(exportPath);

    QList<QVariantMap> notes = DatabaseManager::instance().searchNotes("", "category", catId, -1, -1);
    if (notes.isEmpty()) return;

    // 1. 预统计：计算总大小和项目数
    qint64 totalSize = 0;
    int totalCount = notes.size();
    for (const auto& note : notes) {
        QString type = note.value("item_type").toString();
        if (type == "image" || type == "file" || type == "folder") {
            totalSize += note.value("data_blob").toByteArray().size();
        } else if (type == "local_file" || type == "local_folder" || type == "local_batch") {
            QString fullPath = QCoreApplication::applicationDirPath() + "/" + note.value("content").toString();
            QFileInfo fi(fullPath);
            if (fi.exists()) {
                if (fi.isFile()) totalSize += fi.size();
                else totalSize += FileStorageHelper::calculateItemsStats({fullPath}).totalSize;
            }
        }
    }

    // 2. 进度条初始化 (50MB 或 50个笔记触发)
    FramelessProgressDialog* progress = nullptr;
    const qint64 sizeThreshold = 50 * 1024 * 1024;
    const int countThreshold = 50;
    if (totalSize >= sizeThreshold || totalCount >= countThreshold) {
        progress = new FramelessProgressDialog("导出进度", "正在准备导出文件...", 0, totalCount);
        progress->setWindowModality(Qt::WindowModal);
        progress->show();
    }
    
    QFile csvFile(exportPath + "/notes.csv");
    bool csvOpened = false;
    QTextStream out(&csvFile);
    out.setEncoding(QStringConverter::Utf8);

    QSet<QString> usedFileNames;
    int processedCount = 0;

    for (const auto& note : notes) {
        if (progress && progress->wasCanceled()) break;

        QString type = note.value("item_type").toString();
        QString title = note.value("title").toString();
        QString content = note.value("content").toString();
        QByteArray blob = note.value("data_blob").toByteArray();

        if (progress) {
            progress->setValue(processedCount);
            progress->setLabelText(QString("正在导出: %1").arg(title.left(30)));
        }

        if (type == "image" || type == "file" || type == "folder") {
            QString fileName = title;
            if (type == "image" && !QFileInfo(fileName).suffix().isEmpty()) {
                // keep original
            } else if (type == "image") {
                fileName += ".png";
            }
            
            // 确保文件名唯一
            QString base = QFileInfo(fileName).completeBaseName();
            QString suffix = QFileInfo(fileName).suffix();
            QString finalName = fileName;
            int i = 1;
            while (usedFileNames.contains(finalName.toLower())) {
                finalName = suffix.isEmpty() ? base + QString(" (%1)").arg(i++) : base + QString(" (%1)").arg(i++) + "." + suffix;
            }
            usedFileNames.insert(finalName.toLower());

            QFile f(exportPath + "/" + finalName);
            if (f.open(QIODevice::WriteOnly)) {
                f.write(blob);
                f.close();
            }
        } else if (type == "local_file" || type == "local_folder" || type == "local_batch") {
            QString fullPath = QCoreApplication::applicationDirPath() + "/" + content;
            QFileInfo fi(fullPath);
            if (fi.exists()) {
                QString finalName = fi.fileName();
                int i = 1;
                while (usedFileNames.contains(finalName.toLower())) {
                    finalName = fi.suffix().isEmpty() ? fi.completeBaseName() + QString(" (%1)").arg(i++) : fi.completeBaseName() + QString(" (%1)").arg(i++) + "." + fi.suffix();
                }
                usedFileNames.insert(finalName.toLower());
                
                if (fi.isFile()) {
                    QFile::copy(fullPath, exportPath + "/" + finalName);
                } else {
                    copyRecursively(fullPath, exportPath + "/" + finalName);
                }
            }
        } else {
            // 纯文本类写入 CSV
            if (!csvOpened) {
                if (csvFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
                    out << "Title,Content,Tags,Time\n";
                    csvOpened = true;
                }
            }
            if (csvOpened) {
                auto escape = [](QString s) {
                    s.replace("\"", "\"\"");
                    return "\"" + s + "\"";
                };
                out << escape(title) << "," 
                    << escape(content) << "," 
                    << escape(note.value("tags").toString()) << ","
                    << escape(note.value("created_at").toDateTime().toString("yyyy-MM-dd HH:mm:ss")) << "\n";
            }
        }
        processedCount++;
        QApplication::processEvents();
    }

    if (csvOpened) csvFile.close();

    if (progress) {
        bool canceled = progress->wasCanceled();
        delete progress;
        if (canceled) {
            ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #e67e22;'>⚠️ 导出已取消</b>");
            return;
        }
    }
    ToolTipOverlay::instance()->showText(QCursor::pos(), QString("✅ 分类 [%1] 导出完成").arg(catName));
}

void QuickWindow::doImportCategory(int catId) {
    QStringList files = QFileDialog::getOpenFileNames(this, "选择导入文件", "", "所有文件 (*.*);;CSV文件 (*.csv)");
    if (files.isEmpty()) return;

    int totalCount = FileStorageHelper::processImport(files, catId);
    
    refreshData();
    refreshSidebar();
    ToolTipOverlay::instance()->showText(QCursor::pos(), QString("✅ 导入完成，共处理 %1 个项目").arg(totalCount));
}

void QuickWindow::doImportFolder(int catId) {
    QString dir = QFileDialog::getExistingDirectory(this, "选择导入文件夹", "");
    if (dir.isEmpty()) return;

    int totalCount = FileStorageHelper::processImport({dir}, catId);
    
    refreshData();
    refreshSidebar();
    ToolTipOverlay::instance()->showText(QCursor::pos(), QString("✅ 文件夹导入完成，共处理 %1 个项目").arg(totalCount));
}

bool QuickWindow::eventFilter(QObject* watched, QEvent* event) {
    // 逻辑 1: 鼠标移动到列表或侧边栏范围内，立即恢复正常光标
    if (watched == m_listView || watched == m_systemTree || watched == m_partitionTree) {
        if (event->type() == QEvent::MouseMove || event->type() == QEvent::Enter) {
            setCursor(Qt::ArrowCursor);
        }
    }

    // 逻辑 2: 侧边栏点击分类且不释放左键时，显示手指光标
    if ((watched == m_partitionTree || watched == m_systemTree) && event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        int key = keyEvent->key();
        auto modifiers = keyEvent->modifiers();

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
            // [CRITICAL] 锁定：侧边栏按下 Esc 时，仅切换焦点至列表，严禁直接关闭窗口
            // 注意：若处于行内编辑状态，焦点在编辑器上，不会触发此处的树组件 Esc 逻辑，从而确保编辑能正常取消。
            m_listView->setFocus();
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

    if (watched == m_listView) {
        // [DELETED] 移除由于点击或焦点引发的强制切换逻辑，改由 selectionChanged 信号驱动
    }

    if ((watched == m_listView || watched == m_searchEdit || watched == m_catSearchEdit || watched == m_tagEdit || watched == m_pageInput) && event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);

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
        if (keyEvent->key() == Qt::Key_Escape) {
            if (watched == m_searchEdit || watched == m_catSearchEdit || watched == m_tagEdit || watched == m_pageInput) {
                // [CRITICAL] 锁定：所有输入框按下 Esc 时，仅切换焦点至列表，严禁直接关闭窗口
                m_listView->setFocus();
                return true;
            }
            hide();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

#include "QuickWindow.moc"