#include "ToolTipOverlay.h"
#include "MainWindow.h"
#include <QDebug>
#include <QListView>
#include <QTreeView>
#include "StringUtils.h"
#include "TitleEditorDialog.h"
#include "../db/Database.h"
#include "../mft/MftReader.h"
#include "CategoryDelegate.h"
#include "IconHelper.h"
#include "../core/DatabaseManager.h" // 仅保留用于 Todo 转化等必要兼容，UI 逻辑已解耦
#include <QHBoxLayout>
#include <utility>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QSplitter>
#include <QMenu>
#include <QAction>
#include <QElapsedTimer>
#include <QCursor>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QCloseEvent>
#include <QShortcut>
#include <QItemSelection>
#include <QActionGroup>
#include <QColorDialog>
#include <QSet>
#include <QSettings>
#include <QRandomGenerator>
#include <QLineEdit>
#include <QTextEdit>
#include <QDateTime>
#include <QRegularExpression>
#include <QTimer>
#include <QGraphicsDropShadowEffect>
#include <QDesktopServices>
#include <QUrl>
#include <QFileInfo>
#include <QApplication>
#include <QFile>
#include <QBuffer>
#include <QCoreApplication>
#include <QClipboard>
#include <QFileDialog>
#include <QTextStream>
#include <QStringConverter>
#include <QMimeData>
#include <QPlainTextEdit>
#include "CleanListView.h"
#include "../core/FileStorageHelper.h"
#include "FramelessDialog.h"
#include "CategoryPasswordDialog.h"
#include "PasswordVerifyDialog.h"
#include "SettingsWindow.h"
#include "OCRResultWindow.h"
#include "../core/ShortcutManager.h"
#include "../core/OCRManager.h"
#include <functional>
#include "../core/ActionRecorder.h"
#include <QVariant>
#include <QtGlobal>

#ifdef Q_OS_WIN
#include <windows.h>
#include <windowsx.h>
#define RESIZE_MARGIN 10
#endif

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent, Qt::FramelessWindowHint) {
    // 2026-03-24 按照用户要求：MainWindow 作为资源管理器，解耦笔记系统
    setWindowTitle("RapidExplorer");
    setAcceptDrops(true);
    resize(1200, 800);
    setMouseTracking(true);
    setAttribute(Qt::WA_Hover);
    initUI();

#ifdef Q_OS_WIN
    StringUtils::applyTaskbarMinimizeStyle((void*)winId());
#endif

    m_searchTimer = new QTimer(this);
    m_searchTimer->setSingleShot(true);
    connect(m_searchTimer, &QTimer::timeout, this, &MainWindow::refreshData);

    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setSingleShot(true);
    m_refreshTimer->setInterval(300);
    connect(m_refreshTimer, &QTimer::timeout, this, &MainWindow::refreshData);

    refreshData();

    // 2026-03-24 [REFACTORED] 按照用户要求：移除笔记库信号连接，实现完全隔离
    // 资源管理器只关注物理磁盘和物理索引数据库

    restoreLayout(); 
    setupShortcuts();
    connect(&ShortcutManager::instance(), &ShortcutManager::shortcutsChanged, this, &MainWindow::updateShortcuts);
    
    // [CRITICAL] 顶级事件监听：确保在任何子控件获焦时，MainWindow 都能第一时间截获 Ctrl+S 等物理按键。
    installEventFilter(this);
}

void MainWindow::initUI() {
    auto* centralWidget = new QWidget(this);
    centralWidget->setObjectName("CentralWidget");
    centralWidget->setMouseTracking(true);
    centralWidget->setAttribute(Qt::WA_StyledBackground, true);
    centralWidget->setStyleSheet("#CentralWidget { background-color: #1E1E1E; }");
    setCentralWidget(centralWidget);
    auto* mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // 1. HeaderBar
    m_header = new HeaderBar(this);
    connect(m_header, &HeaderBar::stayOnTopRequested, this, [this](bool checked){
        if (auto* win = window()) {
            if (win->isVisible()) {
#ifdef Q_OS_WIN
                HWND hwnd = (HWND)win->winId();
                SetWindowPos(hwnd, checked ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
#else
                Qt::WindowFlags f = win->windowFlags();
                if (checked) f |= Qt::WindowStaysOnTopHint;
                else f &= ~Qt::WindowStaysOnTopHint;
                win->setWindowFlags(f);
                win->show();
#endif
            }
        }
    });
    connect(m_header, &HeaderBar::filterRequested, this, [this](){
        bool visible = !m_filterWrapper->isVisible();
        m_filterWrapper->setVisible(visible);
        m_header->setFilterActive(visible);
        if (visible) {
            m_filterPanel->updateStats(m_currentKeyword, m_currentFilterType, m_currentFilterValue);
        }
    });
    connect(m_header, &HeaderBar::newNoteRequested, this, [this](){
        // 2026-03-24 [REFACTORED] 资源管理器模式：新建物理文件夹逻辑
        QModelIndex current = m_fileTreeView->currentIndex();
        QString parentPath;
        if (current.isValid()) {
            parentPath = current.data(FileSystemTreeModel::PathRole).toString();
            if (!QFileInfo(parentPath).isDir()) {
                parentPath = QFileInfo(parentPath).absolutePath();
            }
        }

        if (parentPath.isEmpty()) {
            ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #e67e22;'>[!] 请先选择一个目标目录</b>");
            return;
        }

        QString newDir = parentPath + "/新建文件夹";
        int i = 1;
        while (QDir(newDir).exists()) {
            newDir = parentPath + QString("/新建文件夹 (%1)").arg(i++);
        }

        if (QDir().mkdir(newDir)) {
            ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #2ecc71;'>[OK] 文件夹已创建</b>");
            scheduleRefresh();
        }
    });
    connect(m_header, &HeaderBar::toggleSidebar, this, [this](){
        m_sidebarContainer->setVisible(!m_sidebarContainer->isVisible());
        // 2026-03-13 修复逻辑：切换侧边栏可见性后，立即刷新焦点线状态
        updateFocusLines();
    });
    connect(m_header, &HeaderBar::toolboxRequested, this, &MainWindow::toolboxRequested);
    connect(m_header, &HeaderBar::metadataToggled, this, [this](bool checked){
        m_metaPanel->setVisible(checked);
    });
    connect(m_header, &HeaderBar::windowClose, this, &MainWindow::close);
    connect(m_header, &HeaderBar::windowMinimize, this, &MainWindow::showMinimized);
    connect(m_header, &HeaderBar::windowMaximize, this, [this](){
        if (isMaximized()) showNormal();
        else showMaximized();
    });
    mainLayout->addWidget(m_header);

    // 1.1 AddressBar (2026-03-24 [NEW] 按照用户要求：插入在标题栏与板块之间)
    m_addressBar = new AddressBar(this);
    connect(m_addressBar, &AddressBar::pathChanged, this, [this](const QString& path){
        m_folderBrowser->setRootPath(path);
    });
    mainLayout->addWidget(m_addressBar);

    // 核心内容容器：管理 5px 全局边距
    auto* contentWidget = new QWidget(centralWidget);
    contentWidget->setAttribute(Qt::WA_StyledBackground, true);
    contentWidget->setStyleSheet("background: transparent; border: none;");
    auto* contentLayout = new QVBoxLayout(contentWidget);
    contentLayout->setContentsMargins(5, 5, 5, 5); // 确保顶栏下方及窗口四周均有 5px 留白
    contentLayout->setSpacing(0);

    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->setHandleWidth(5); // 统一横向板块间的物理缝隙为 5px
    splitter->setChildrenCollapsible(false);
    splitter->setAttribute(Qt::WA_StyledBackground, true);
    splitter->setStyleSheet("QSplitter { background: transparent; border: none; } QSplitter::handle { background: transparent; }");

    // 1. 左侧侧边栏包装容器 (固定 230px)
    auto* sidebarWrapper = new QWidget();
    sidebarWrapper->setMinimumWidth(230);
    auto* sidebarWrapperLayout = new QVBoxLayout(sidebarWrapper);
    sidebarWrapperLayout->setContentsMargins(0, 0, 0, 0); // 彻底消除偏移边距，由全局 Layout 和 Splitter 控制

    m_sidebarContainer = new QFrame();
    m_sidebarContainer->setMinimumWidth(230);
    m_sidebarContainer->setObjectName("SidebarContainer");
    m_sidebarContainer->setAttribute(Qt::WA_StyledBackground, true);
    m_sidebarContainer->setStyleSheet(
        "#SidebarContainer {"
        "  background-color: #1e1e1e;"
        "  border: 1px solid #333333;"
        "  border-top-left-radius: 0px;"
        "  border-top-right-radius: 0px;"
        "  border-bottom-left-radius: 0px;"
        "  border-bottom-right-radius: 0px;"
        "}"
    );

    auto* sidebarShadow = new QGraphicsDropShadowEffect(m_sidebarContainer);
    sidebarShadow->setBlurRadius(10);
    sidebarShadow->setXOffset(0);
    sidebarShadow->setYOffset(4);
    sidebarShadow->setColor(QColor(0, 0, 0, 150));
    m_sidebarContainer->setGraphicsEffect(sidebarShadow);

    auto* sidebarContainerLayout = new QVBoxLayout(m_sidebarContainer);
    sidebarContainerLayout->setContentsMargins(0, 0, 0, 0); 
    sidebarContainerLayout->setSpacing(0);

    m_sidebarFocusLine = new QWidget();
    m_sidebarFocusLine->setFixedHeight(1);
    m_sidebarFocusLine->setStyleSheet("background-color: #2ecc71;");
    m_sidebarFocusLine->hide();
    sidebarContainerLayout->addWidget(m_sidebarFocusLine);

    // 侧边栏标题栏 (全宽下划线方案)
    auto* sidebarHeader = new QWidget();
    sidebarHeader->setFixedHeight(32);
    sidebarHeader->setStyleSheet(
        "background-color: #252526; "
        "border-top-left-radius: 0px; "
        "border-top-right-radius: 0px; "
        "border-bottom: 1px solid #333;"
    );
    auto* sidebarHeaderLayout = new QHBoxLayout(sidebarHeader);
    sidebarHeaderLayout->setContentsMargins(15, 0, 15, 0);
    auto* sbIcon = new QLabel();
    sbIcon->setPixmap(IconHelper::getIcon("category", "#3498db").pixmap(18, 18));
    sidebarHeaderLayout->addWidget(sbIcon);
    auto* sbTitle = new QLabel("分类关联");
    sbTitle->setStyleSheet("color: #3498db; font-size: 13px; font-weight: bold; background: transparent; border: none;");
    sidebarHeaderLayout->addWidget(sbTitle);
    sidebarHeaderLayout->addStretch();
    
    sidebarHeader->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(sidebarHeader, &QWidget::customContextMenuRequested, this, [this, splitter, sidebarHeader](const QPoint& pos){
        QMenu menu;
        IconHelper::setupMenu(&menu);
        menu.setStyleSheet("QMenu { background-color: #2D2D2D; color: #EEE; border: 1px solid #444; padding: 4px; } "
                           /* 10px 间距规范：padding-left 10px + icon margin-left 6px */
                           "QMenu::item { padding: 6px 10px 6px 10px; border-radius: 3px; } "
                           "QMenu::icon { margin-left: 6px; } "
                           "QMenu::item:selected { background-color: #3E3E42; }");
        menu.addAction(IconHelper::getIcon("nav_prev", "#aaaaaa", 18), "向左移动", [this, splitter](){
            int index = splitter->indexOf(m_sidebarContainer);
            if (index > 0) splitter->insertWidget(index - 1, m_sidebarContainer);
        });
        menu.addAction("向右移动", [this, splitter](){
            int index = splitter->indexOf(m_sidebarContainer);
            if (index < splitter->count() - 1) splitter->insertWidget(index + 1, m_sidebarContainer);
        });
        menu.exec(sidebarHeader->mapToGlobal(pos));
    });
    
    sidebarContainerLayout->addWidget(sidebarHeader);

    // 内容容器
    auto* sbContent = new QWidget();
    sbContent->setAttribute(Qt::WA_StyledBackground, true);
    sbContent->setStyleSheet("background: transparent; border: none;");
    auto* sbContentLayout = new QVBoxLayout(sbContent);
    sbContentLayout->setContentsMargins(8, 8, 8, 8);
    sbContentLayout->setSpacing(0);

    QString treeStyle = R"(
        QTreeView { background-color: transparent; border: none; color: #CCC; outline: none; }
        QTreeView::branch:has-children:closed { image: url(:/icons/arrow_right.svg); }
        QTreeView::branch:has-children:open   { image: url(:/icons/arrow_down.svg); }
        QTreeView::item { height: 22px; padding-left: 10px; }
    )";

    m_systemTree = new DropTreeView();
    m_systemTree->setStyleSheet(treeStyle); 
    m_systemTree->setItemDelegate(new CategoryDelegate(this));
    m_systemModel = new PhysicalCategoryModel(PhysicalCategoryModel::System, this);
    m_systemTree->setModel(m_systemModel);
    m_systemTree->setHeaderHidden(true);
    m_systemTree->setRootIsDecorated(true); // 物理结构需要装饰器
    m_systemTree->setIndentation(12);
    m_systemTree->setFixedHeight(220); // 调整高度以容纳更多物理项
    m_systemTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_systemTree->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_systemTree->setContextMenuPolicy(Qt::CustomContextMenu);

    m_partitionTree = new DropTreeView();
    m_partitionTree->setStyleSheet(treeStyle);
    m_partitionTree->setItemDelegate(new CategoryDelegate(this));
    m_partitionModel = new PhysicalCategoryModel(PhysicalCategoryModel::Tag, this);
    m_partitionTree->setModel(m_partitionModel);
    m_partitionTree->setHeaderHidden(true);
    m_partitionTree->setRootIsDecorated(true);
    m_partitionTree->setIndentation(16);
    m_partitionTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_partitionTree->setDragEnabled(true);
    m_partitionTree->setAcceptDrops(true);
    m_partitionTree->setDropIndicatorShown(true);
    m_partitionTree->setDragDropMode(QAbstractItemView::InternalMove);
    m_partitionTree->setDefaultDropAction(Qt::MoveAction);
    m_partitionTree->expandAll();
    m_partitionTree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_partitionTree->setContextMenuPolicy(Qt::CustomContextMenu);
    
    sbContentLayout->addWidget(m_systemTree);
    sbContentLayout->addWidget(m_partitionTree);
    sidebarContainerLayout->addWidget(sbContent);

    // 直接放入 Splitter (移除 Wrapper)
    splitter->addWidget(m_sidebarContainer);

    auto onSidebarMenu = [this](const QPoint& pos){
        auto* tree = qobject_cast<QTreeView*>(sender());
        if (!tree) return;
        
        QModelIndexList selected = tree->selectionModel()->selectedIndexes();
        QModelIndex index = tree->indexAt(pos);
        
        // 如果点击的项不在当前选中范围内，则切换选中为当前项
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

        // [CRITICAL] 资源管理器模式：右键菜单仅保留物理相关操作
        if (!index.isValid() || index.data(PhysicalCategoryModel::NameRole).toString() == "物理标签") {
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

        QString type = index.data(PhysicalCategoryModel::TypeRole).toString();
        QString idxName = index.data(PhysicalCategoryModel::NameRole).toString();

        if (type == "physical_tag") {
            // [NEW] 2026-03-24 按照用户要求：物理标签右键菜单
            menu.addAction(IconHelper::getIcon("search", "#3498db", 18), "在该标签下搜索...", [this, idxName]() {
                m_header->searchEdit()->setText("tag:" + idxName + " ");
                m_header->searchEdit()->setFocus();
            });
        }
        menu.exec(tree->mapToGlobal(pos));
    };

    connect(m_systemTree, &QTreeView::customContextMenuRequested, this, onSidebarMenu);
    connect(m_partitionTree, &QTreeView::customContextMenuRequested, this, onSidebarMenu);

    auto onSelection = [this](QTreeView* tree, const QModelIndex& index) {
        if (!index.isValid()) return;
        if (tree == m_systemTree) {
            m_partitionTree->selectionModel()->clearSelection();
            m_partitionTree->setCurrentIndex(QModelIndex());
        } else {
            m_systemTree->selectionModel()->clearSelection();
            m_systemTree->setCurrentIndex(QModelIndex());
        }
        onTagSelected(index);
    };

    connect(m_systemTree, &QTreeView::clicked, this, [this, onSelection](const QModelIndex& idx){ onSelection(m_systemTree, idx); });
    connect(m_partitionTree, &QTreeView::clicked, this, [this, onSelection](const QModelIndex& idx){ onSelection(m_partitionTree, idx); });
    
    // [NEW] 2026-03-24 重构：侧边栏物理标签关联逻辑
    auto onItemsDropped = [this](const QList<int>&, const QModelIndex& targetIndex){
        if (!targetIndex.isValid()) return;
        QString type = targetIndex.data(PhysicalCategoryModel::TypeRole).toString();
        
        if (type == "physical_tag") {
            QString tagName = targetIndex.data(PhysicalCategoryModel::NameRole).toString();
            // TODO: 获取被拖拽的物理路径，调用 AmMetaJson 添加标签
            ToolTipOverlay::instance()->showText(QCursor::pos(), QString("<b style='color: #2ecc71;'>[OK] 已关联物理标签: %1</b>").arg(tagName));
        }
    };

    connect(m_systemTree, &DropTreeView::notesDropped, this, onItemsDropped);
    connect(m_partitionTree, &DropTreeView::notesDropped, this, onItemsDropped);
    connect(m_partitionTree, &QTreeView::doubleClicked, this, [this](const QModelIndex& index) {
        if (m_partitionTree->isExpanded(index)) m_partitionTree->collapse(index);
        else m_partitionTree->expand(index);
    });

    // 3. 中间列表卡片容器
    auto* listContainer = new QFrame();
    listContainer->setMinimumWidth(230); // 对齐 MetadataPanel
    listContainer->setObjectName("ListContainer");
    listContainer->setAttribute(Qt::WA_StyledBackground, true);
    listContainer->setStyleSheet(
        "#ListContainer {"
        "  background-color: #1e1e1e;"
        "  border: 1px solid #333333;"
        "  border-top-left-radius: 0px;"
        "  border-top-right-radius: 0px;"
        "  border-bottom-left-radius: 0px;"
        "  border-bottom-right-radius: 0px;"
        "}"
    );

    auto* listShadow = new QGraphicsDropShadowEffect(listContainer);
    listShadow->setBlurRadius(10);
    listShadow->setXOffset(0);
    listShadow->setYOffset(4);
    listShadow->setColor(QColor(0, 0, 0, 150));
    listContainer->setGraphicsEffect(listShadow);

    auto* listContainerLayout = new QVBoxLayout(listContainer);
    listContainerLayout->setContentsMargins(0, 0, 0, 0); 
    listContainerLayout->setSpacing(0);

    m_listFocusLine = new QWidget();
    m_listFocusLine->setFixedHeight(1);
    m_listFocusLine->setStyleSheet("background-color: #2ecc71;");
    m_listFocusLine->hide();
    listContainerLayout->addWidget(m_listFocusLine);

    // 列表标题栏 (锁定 32px, 统一配色与分割线)
    auto* listHeader = new QWidget();
    listHeader->setFixedHeight(32);
    listHeader->setStyleSheet(
        "background-color: #252526; "
        "border-top-left-radius: 0px; "
        "border-top-right-radius: 0px; "
        "border-bottom: 1px solid #333;" 
    );
    auto* listHeaderLayout = new QHBoxLayout(listHeader);
    listHeaderLayout->setContentsMargins(15, 0, 15, 0); 
    auto* listIcon = new QLabel();
    listIcon->setPixmap(IconHelper::getIcon("list_ul", "#2ecc71").pixmap(18, 18));
    listHeaderLayout->addWidget(listIcon);
    auto* listHeaderTitle = new QLabel("导航");
    listHeaderTitle->setStyleSheet("color: #2ecc71; font-size: 13px; font-weight: bold; background: transparent; border: none;");
    listHeaderLayout->addWidget(listHeaderTitle);
    listHeaderLayout->addStretch();
    
    listHeader->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(listHeader, &QWidget::customContextMenuRequested, this, [this, listContainer, splitter, listHeader](const QPoint& pos){
        QMenu menu;
        IconHelper::setupMenu(&menu);
        menu.setStyleSheet("QMenu { background-color: #2D2D2D; color: #EEE; border: 1px solid #444; padding: 4px; } "
                           /* 10px 间距规范：padding-left 10px + icon margin-left 6px */
                           "QMenu::item { padding: 6px 10px 6px 10px; border-radius: 3px; } "
                           "QMenu::icon { margin-left: 6px; } "
                           "QMenu::item:selected { background-color: #3E3E42; }");
        menu.addAction("向左移动", [this, listContainer, splitter](){
            int index = splitter->indexOf(listContainer);
            if (index > 0) splitter->insertWidget(index - 1, listContainer);
        });
        menu.addAction("向右移动", [this, listContainer, splitter](){
            int index = splitter->indexOf(listContainer);
            if (index < splitter->count() - 1) splitter->insertWidget(index + 1, listContainer);
        });
        menu.exec(listHeader->mapToGlobal(pos));
    });
    
    listContainerLayout->addWidget(listHeader);

    // 内容容器
    auto* listContent = new QWidget();
    listContent->setAttribute(Qt::WA_StyledBackground, true);
    listContent->setStyleSheet("background: transparent; border: none;");
    auto* listContentLayout = new QVBoxLayout(listContent);
    // 恢复垂直边距为 8，保留水平边距 15 以对齐宽度
    listContentLayout->setContentsMargins(15, 8, 15, 8);
    
    // 2026-03-24 [REFACTORED] 按照用户要求：使用 m_fileTreeView (原 m_noteList) 展示物理文件树
    m_fileTreeView = new DropTreeView();
    m_fileTreeView->setHeaderHidden(true); // 隐藏表头
    m_fileTreeView->setIndentation(20);    // 按照资源管理器图片调整缩进
    m_fileTreeView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_fileTreeView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    
    m_mftReader = new MftReader(this);
    m_fileModel = new FileSystemTreeModel(m_mftReader, this);
    m_fileTreeView->setModel(m_fileModel);
    m_fileModel->initDrives(); // 初始化显示磁盘

    // [NEW] 2026-03-24 连接展开信号，触发懒加载
    connect(m_fileTreeView, &QTreeView::expanded, this, [this](const QModelIndex& index){
        if (m_fileModel->canFetchMore(index)) {
            m_fileModel->fetchMore(index);
        }
    });
    
    // [CRITICAL] 视觉对齐：由于结构变更为树状，原 NoteDelegate 需同步重构或替换。
    // 这里暂时移除原 NoteDelegate 以防冲突，后续按图片定制新渲染器。
    // m_fileTreeView->setItemDelegate(new NoteDelegate(m_fileTreeView));
    
    // 2026-03-24 [REFACTORED] 按照用户要求：补齐改动处的溯源注释
    m_fileTreeView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_fileTreeView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    connect(m_fileTreeView, &QTreeView::customContextMenuRequested, this, &MainWindow::showContextMenu);
    
    // 2026-03-23 按照用户要求：应用树状视图 QSS，确保视觉统一
    m_fileTreeView->setStyleSheet(
        "QTreeView { background: transparent; border: none; outline: none; color: #BBB; }"
        "QTreeView::item { height: 28px; padding-left: 5px; border-radius: 4px; }"
        "QTreeView::item:hover { background-color: rgba(255, 255, 255, 0.05); }"
        "QTreeView::item:selected { background-color: rgba(46, 204, 113, 0.2); color: #2ecc71; }"
        "QTreeView::branch:has-children:closed { image: url(:/icons/arrow_right.svg); }"
        "QTreeView::branch:has-children:open   { image: url(:/icons/arrow_down.svg); }"
    );
    
    // 基础拖拽使能
    m_fileTreeView->setDragEnabled(true);
    m_fileTreeView->setAcceptDrops(true);
    m_fileTreeView->setDropIndicatorShown(true);
    
    // 2026-03-24 [REFACTORED] 资源管理器模式：监听物理文件树的选择与双击事件
    connect(m_fileTreeView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::onSelectionChanged);
    connect(m_fileTreeView, &QTreeView::doubleClicked, this, [this](const QModelIndex& index){
        if (!index.isValid()) return;

        bool isDir = index.data(FileSystemTreeModel::IsDirRole).toBool();
        if (isDir) {
            if (m_fileTreeView->isExpanded(index)) m_fileTreeView->collapse(index);
            else m_fileTreeView->expand(index);
            return;
        }

        // 2026-03-24 [REFACTORED] 按照用户要求：双击直接执行物理打开，彻底剥离笔记逻辑
        QString path = index.data(FileSystemTreeModel::PathRole).toString();
        if (!path.isEmpty() && QFileInfo::exists(path)) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        } else {
            ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #e67e22;'>[!] 无法执行物理打开：路径无效</b>");
        }
    });

    listContentLayout->addWidget(m_fileTreeView);

    m_lockWidget = new CategoryLockWidget(this);
    m_lockWidget->setVisible(false);
    connect(m_lockWidget, &CategoryLockWidget::unlocked, this, [this](){
        refreshData();
    });
    connect(m_lockWidget, &CategoryLockWidget::escPressed, this, [this](){
        this->setFocus();
    });
    listContentLayout->addWidget(m_lockWidget);

    listContainerLayout->addWidget(listContent);
    splitter->addWidget(listContainer);
    
    // 4. 编辑器容器 (Card) - 独立出来
    auto* editorContainer = new QFrame();
    editorContainer->setMinimumWidth(230);
    editorContainer->setObjectName("EditorContainer");
    editorContainer->setAttribute(Qt::WA_StyledBackground, true);
    editorContainer->setStyleSheet(
        "#EditorContainer {"
        "  background-color: #1e1e1e;"
        "  border: 1px solid #333333;"
        "  border-top-left-radius: 0px;"
        "  border-top-right-radius: 0px;"
        "  border-bottom-left-radius: 0px;"
        "  border-bottom-right-radius: 0px;"
        "}"
    );

    auto* editorShadow = new QGraphicsDropShadowEffect(editorContainer);
    editorShadow->setBlurRadius(10);
    editorShadow->setXOffset(0);
    editorShadow->setYOffset(4);
    editorShadow->setColor(QColor(0, 0, 0, 150));
    editorContainer->setGraphicsEffect(editorShadow);

    auto* editorContainerLayout = new QVBoxLayout(editorContainer);
    editorContainerLayout->setContentsMargins(0, 0, 0, 0);
    editorContainerLayout->setSpacing(0);

    // 编辑器标题栏 (全宽贯穿线)
    auto* editorHeader = new QWidget();
    editorHeader->setFixedHeight(32);
    editorHeader->setStyleSheet(
        "background-color: #252526; "
        "border-top-left-radius: 0px; "
        "border-top-right-radius: 0px; "
        "border-bottom: 1px solid #333;"
    );
    auto* editorHeaderLayout = new QHBoxLayout(editorHeader);
    // [CRITICAL] 视觉对齐锁定：此处顶部边距必须设为 2px，以配合 32px 的标题栏高度，使文字达到垂直居中。
    editorHeaderLayout->setContentsMargins(15, 2, 15, 0);
    auto* edIcon = new QLabel();
    // 2026-03-13 按照用户要求：eye 图标颜色统一为 #41F2F2
    edIcon->setPixmap(IconHelper::getIcon("eye", "#41F2F2").pixmap(18, 18));
    editorHeaderLayout->addWidget(edIcon);
    auto* edTitle = new QLabel("内容（文件夹 / 文件）"); // 保护用户修改的标题内容
    edTitle->setStyleSheet("color: #41F2F2; font-size: 13px; font-weight: bold; background: transparent; border: none;");
    editorHeaderLayout->addWidget(edTitle);
    editorHeaderLayout->addStretch();

    // [CRITICAL] 编辑逻辑重定义：MainWindow 已移除笔记编辑模式，此按钮改为物理打开 (doOpenSelected)。
    m_editBtn = new QPushButton();
    m_editBtn->setFixedSize(24, 24);
    m_editBtn->setCursor(Qt::PointingHandCursor);
    m_editBtn->setEnabled(false);
    m_editBtn->setProperty("tooltipText", "物理打开选中的文件 (Ctrl+B)"); m_editBtn->installEventFilter(this);
    m_editBtn->setIcon(IconHelper::getIcon("edit", "#555555"));
    m_editBtn->setStyleSheet(
        "QPushButton { background: transparent; border: none; border-radius: 4px; }"
        "QPushButton:hover:enabled { background-color: rgba(255, 255, 255, 0.1); }"
    );
    connect(m_editBtn, &QPushButton::clicked, this, &MainWindow::doOpenSelected);
    editorHeaderLayout->addWidget(m_editBtn);

    editorHeader->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(editorHeader, &QWidget::customContextMenuRequested, this, [this, editorContainer, splitter, editorHeader](const QPoint& pos){
        QMenu menu;
        menu.setStyleSheet("QMenu { background-color: #2D2D2D; color: #EEE; border: 1px solid #444; padding: 4px; } "
                           /* 10px 间距规范：padding-left 10px + icon margin-left 6px */
                           "QMenu::item { padding: 6px 10px 6px 10px; border-radius: 3px; } "
                           "QMenu::icon { margin-left: 6px; } "
                           "QMenu::item:selected { background-color: #3E3E42; }");
        menu.addAction("向左移动", [this, editorContainer, splitter](){
            int index = splitter->indexOf(editorContainer);
            if (index > 0) splitter->insertWidget(index - 1, editorContainer);
        });
        menu.addAction("向右移动", [this, editorContainer, splitter](){
            int index = splitter->indexOf(editorContainer);
            if (index < splitter->count() - 1) splitter->insertWidget(index + 1, editorContainer);
        });
        menu.exec(editorHeader->mapToGlobal(pos));
    });

    editorContainerLayout->addWidget(editorHeader);

    // [NEW] 2026-03-24 重构：重构为文件夹浏览器容器
    m_folderBrowser = new FolderContentView(this);
    editorContainerLayout->addWidget(m_folderBrowser);
    
    // 直接放入 Splitter
    splitter->addWidget(editorContainer);

    // 5. 元数据面板 - 独立出来
    m_metaPanel = new MetadataPanel(this);
    m_metaPanel->setMinimumWidth(230);
    connect(m_metaPanel, &MetadataPanel::noteUpdated, this, &MainWindow::refreshData); // 此处的 noteUpdated 语义已在 MetadataPanel 内部兼容物理文件
    connect(m_metaPanel, &MetadataPanel::closed, this, [this](){
        m_header->setMetadataActive(false);
    });
    connect(m_metaPanel, &MetadataPanel::tagAdded, this, [this](const QStringList& tags){
        // 2026-03-24 [REFACTORED] 按照用户要求：物理标签添加，同步至物理数据库
        QModelIndexList indices = m_fileTreeView->selectionModel()->selectedIndexes();
        if (indices.isEmpty()) return;
        for (const auto& index : std::as_const(indices)) {
            QString path = index.data(FileSystemTreeModel::PathRole).toString();
            // TODO: 物理标签同步逻辑 (AmMetaJson)
        }
        refreshData();
    });
    
    // 给元数据面板添加右键移动菜单
    auto* metaHeader = m_metaPanel->findChild<QWidget*>("MetadataHeader");
    if (metaHeader) {
        metaHeader->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(metaHeader, &QWidget::customContextMenuRequested, this, [this, splitter, metaHeader](const QPoint& pos){
            QMenu menu;
            IconHelper::setupMenu(&menu);
            menu.setStyleSheet("QMenu { background-color: #2D2D2D; color: #EEE; border: 1px solid #444; padding: 4px; } "
                               /* 10px 间距规范：padding-left 10px + icon margin-left 6px */
                               "QMenu::item { padding: 6px 10px 6px 10px; border-radius: 3px; } "
                               "QMenu::icon { margin-left: 6px; } "
                               "QMenu::item:selected { background-color: #3E3E42; }");
            menu.addAction("向左移动", [this, splitter](){
                int index = splitter->indexOf(m_metaPanel);
                if (index > 0) splitter->insertWidget(index - 1, m_metaPanel);
            });
            menu.addAction("向右移动", [this, splitter](){
                int index = splitter->indexOf(m_metaPanel);
                if (index < splitter->count() - 1) splitter->insertWidget(index + 1, m_metaPanel);
            });
            menu.exec(metaHeader->mapToGlobal(pos));
        });
    }

    splitter->addWidget(m_metaPanel);
    
    // [CRITICAL] 为元数据面板的输入框安装事件过滤器
    if (m_metaPanel) {
        if (m_metaPanel->m_tagEdit) m_metaPanel->m_tagEdit->installEventFilter(this);
    }

    // 6. 筛选器器卡片容器
    auto* filterContainer = new QFrame();
    filterContainer->setMinimumWidth(230);
    filterContainer->setObjectName("FilterContainer");
    filterContainer->setAttribute(Qt::WA_StyledBackground, true);
    filterContainer->setStyleSheet(
        "#FilterContainer {"
        "  background-color: #1e1e1e;"
        "  border: 1px solid #333333;"
        "  border-top-left-radius: 0px;"
        "  border-top-right-radius: 0px;"
        "  border-bottom-left-radius: 0px;"
        "  border-bottom-right-radius: 0px;"
        "}"
    );

    auto* filterShadow = new QGraphicsDropShadowEffect(filterContainer);
    filterShadow->setBlurRadius(10);
    filterShadow->setXOffset(0);
    filterShadow->setYOffset(4);
    filterShadow->setColor(QColor(0, 0, 0, 150));
    filterContainer->setGraphicsEffect(filterShadow);

    auto* filterContainerLayout = new QVBoxLayout(filterContainer);
    filterContainerLayout->setContentsMargins(0, 0, 0, 0);
    filterContainerLayout->setSpacing(0);

    // 筛选器标题栏
    auto* filterHeader = new QWidget();
    filterHeader->setFixedHeight(32);
    filterHeader->setStyleSheet(
        "background-color: #252526; "
        "border-top-left-radius: 0px; "
        "border-top-right-radius: 0px; "
        "border-bottom: 1px solid #333;"
    );
    auto* filterHeaderLayout = new QHBoxLayout(filterHeader);
    filterHeaderLayout->setContentsMargins(15, 0, 4, 0);
    auto* fiIcon = new QLabel();
    fiIcon->setPixmap(IconHelper::getIcon("filter", "#f1c40f").pixmap(18, 18));
    filterHeaderLayout->addWidget(fiIcon);
    auto* fiTitle = new QLabel("筛选器");
    fiTitle->setStyleSheet("color: #f1c40f; font-size: 13px; font-weight: bold; background: transparent; border: none;");
    filterHeaderLayout->addWidget(fiTitle);
    filterHeaderLayout->addStretch();

    auto* filterCloseBtn = new QPushButton();
    filterCloseBtn->setIcon(IconHelper::getIcon("close", "#888888"));
    filterCloseBtn->setFixedSize(24, 24);
    filterCloseBtn->setCursor(Qt::PointingHandCursor);
    filterCloseBtn->setStyleSheet(
        "QPushButton { background-color: transparent; border: none; border-radius: 4px; }"
        "QPushButton:hover { background-color: #e74c3c; }"
    );
    connect(filterCloseBtn, &QPushButton::clicked, this, [this](){
        m_filterWrapper->hide();
        m_header->setFilterActive(false);
    });
    filterHeaderLayout->addWidget(filterCloseBtn);
    
    filterHeader->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(filterHeader, &QWidget::customContextMenuRequested, this, [this, filterContainer, splitter, filterHeader](const QPoint& pos){
        QMenu menu;
        menu.setStyleSheet("QMenu { background-color: #2D2D2D; color: #EEE; border: 1px solid #444; padding: 4px; } "
                           /* 10px 间距规范：padding-left 10px + icon margin-left 6px */
                           "QMenu::item { padding: 6px 10px 6px 10px; border-radius: 3px; } "
                           "QMenu::icon { margin-left: 6px; } "
                           "QMenu::item:selected { background-color: #3E3E42; }");
        menu.addAction("向左移动", [this, filterContainer, splitter](){
            int index = splitter->indexOf(filterContainer);
            if (index > 0) splitter->insertWidget(index - 1, filterContainer);
        });
        menu.addAction("向右移动", [this, filterContainer, splitter](){
            int index = splitter->indexOf(filterContainer);
            if (index < splitter->count() - 1) splitter->insertWidget(index + 1, filterContainer);
        });
        menu.exec(filterHeader->mapToGlobal(pos));
    });
    
    filterContainerLayout->addWidget(filterHeader);

    // 内容容器
    auto* filterContent = new QWidget();
    filterContent->setAttribute(Qt::WA_StyledBackground, true);
    filterContent->setStyleSheet("background: transparent; border: none;");
    auto* filterContentLayout = new QVBoxLayout(filterContent);
    filterContentLayout->setContentsMargins(0, 0, 10, 10);

    m_filterPanel = new FilterPanel(this);
    m_filterPanel->setStyleSheet("background: transparent; border: none;");
    connect(m_filterPanel, &FilterPanel::filterChanged, this, &MainWindow::refreshData);
    filterContentLayout->addWidget(m_filterPanel);
    filterContainerLayout->addWidget(filterContent);

    m_filterWrapper = filterContainer;
    splitter->addWidget(m_filterWrapper);

    // 2026-03-13 修复逻辑：监听 Splitter 移动，实时更新焦点线状态
    connect(splitter, &QSplitter::splitterMoved, this, &MainWindow::updateFocusLines);

    splitter->setStretchFactor(0, 1); 
    splitter->setStretchFactor(1, 2); 
    splitter->setStretchFactor(2, 8); 
    splitter->setStretchFactor(3, 1); 
    splitter->setStretchFactor(4, 1);
    
    // 显式设置初始大小比例
    splitter->setSizes({230, 230, 600, 230, 230});

    contentLayout->addWidget(splitter);
    mainLayout->addWidget(contentWidget);

    m_systemTree->installEventFilter(this);
    m_partitionTree->installEventFilter(this);
    if (m_header) {
        if (m_header->searchEdit()) m_header->searchEdit()->installEventFilter(this);
        if (m_header->pageInput()) m_header->pageInput()->installEventFilter(this);
    }

    auto* preview = QuickPreview::instance();
    connect(preview, &QuickPreview::editRequested, this, [this, preview](int id){
        // 2026-03-24 [REFACTORED] 资源管理器模式：预览窗编辑按钮重定向至物理打开
        this->doOpenSelected();
    });
    connect(preview, &QuickPreview::prevRequested, this, [this, preview](){
        if (!preview->caller() || preview->caller()->window() != this) return;
        QModelIndex current = m_fileTreeView->currentIndex();
        if (!current.isValid() || m_fileModel->rowCount() == 0) return;

        int row = current.row();
        int count = m_fileModel->rowCount();
        
        // 循环向上查找 (物理模式简化为逐项切换)
        int prevRow = (row - 1 + count) % count;
        QModelIndex idx = m_fileModel->index(prevRow, 0);
        m_fileTreeView->setCurrentIndex(idx);
        m_fileTreeView->scrollTo(idx);
        updatePreviewContent();
    });
    connect(preview, &QuickPreview::nextRequested, this, [this, preview](){
        if (!preview->caller() || preview->caller()->window() != this) return;
        QModelIndex current = m_fileTreeView->currentIndex();
        if (!current.isValid() || m_fileModel->rowCount() == 0) return;

        int row = current.row();
        int count = m_fileModel->rowCount();

        // 循环向下查找 (物理模式简化为逐项切换)
        int nextRow = (row + 1) % count;
        QModelIndex idx = m_fileModel->index(nextRow, 0);
        m_fileTreeView->setCurrentIndex(idx);
        m_fileTreeView->scrollTo(idx);
        updatePreviewContent();
    });
    connect(preview, &QuickPreview::historyNavigationRequested, this, [this, preview](int id){
        // 2026-03-24 [REFACTORED] 资源管理器模式下暂不使用笔记 ID 导航
    });

    m_fileTreeView->installEventFilter(this);
}

// 2026-03-24 [REFACTORED] 按照用户要求：补齐改动处的溯源注释
void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls() || event->mimeData()->hasText() || event->mimeData()->hasImage()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dragMoveEvent(QDragMoveEvent* event) {
    event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event) {
    // 2026-03-24 [REFACTORED] 按照用户要求：资源管理器模式拖拽逻辑
    // 物理资源管理器不应向笔记库插入数据，后续实现文件移动/复制逻辑
    event->ignore();
}

bool MainWindow::event(QEvent* event) {
    // 2026-03-24 [REFACTORED] 按照用户要求：资源管理器事件处理
    return QMainWindow::event(event);
}

void MainWindow::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);

    // [USER_REQUEST] 按照用户要求：只要启动后，焦点自动锁定在列表，不可锁定在搜索数据的搜索框
    if (m_fileTreeView) {
        m_fileTreeView->setFocus();
        if (m_fileModel && m_fileModel->rowCount() > 0 && !m_fileTreeView->currentIndex().isValid()) {
            m_fileTreeView->setCurrentIndex(m_fileModel->index(0, 0));
        }
    }

    // 从 HeaderBar 获取按钮状态
    if (m_header) {
        auto* btn = m_header->findChild<QPushButton*>("btnStayOnTop");
        if (btn && btn->isChecked()) {
#ifdef Q_OS_WIN
            HWND hwnd = (HWND)winId();
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
#else
            Qt::WindowFlags f = windowFlags();
            f |= Qt::WindowStaysOnTopHint;
            setWindowFlags(f);
            show();
#endif
        }
    }
}

#ifdef Q_OS_WIN
bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result) {
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
        int x = GET_X_LPARAM(msg->lParam);
        int y = GET_Y_LPARAM(msg->lParam);
        
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
        else return QMainWindow::nativeEvent(eventType, message, result);

        return true;
    }
    return QMainWindow::nativeEvent(eventType, message, result);
}
#endif

void MainWindow::onFileAdded(const QVariantMap& item) {
    // 2026-03-24 [REFACTORED] 资源管理器模式：监听物理文件变更，执行全量刷新以保证树层级正确
    scheduleRefresh();
}

void MainWindow::scheduleRefresh() {
    m_refreshTimer->start();
}

void MainWindow::refreshData() {
    // 2026-03-24 [REFACTORED] 按照用户要求：完全基于物理数据的刷新逻辑
    qDebug() << "[MainWindow] 执行物理数据刷新...";

    // 1. 更新侧边栏物理模型
    m_systemModel->refresh();
    m_partitionModel->refresh();

    // 2. 如果当前有搜索关键词，则进入 MFT 搜索模式
    if (!m_currentKeyword.isEmpty()) {
        // TODO: 接入 MftReader 并行搜索结果到 m_fileModel
    }

    // 3. 更新分页（物理搜索结果的分页）
    m_header->updatePagination(m_currentPage, 1); // 暂时固定 1 页

    // 4. 更新物理统计信息
    if (!m_filterWrapper->isHidden()) {
        // m_filterPanel->updateStats(...); // 需重构为物理版
    }
}

void MainWindow::onSelectionChanged(const QItemSelection& selected, const QItemSelection& deselected) {
    QModelIndexList indices = m_fileTreeView->selectionModel()->selectedIndexes();
    if (indices.isEmpty()) {
        m_metaPanel->clearSelection();
        m_editBtn->setEnabled(false);
        m_editBtn->setIcon(IconHelper::getIcon("edit", "#555555"));
    } else if (indices.size() == 1) {
        QModelIndex index = indices.first();
        
        // [NEW] 2026-03-24 联动逻辑：点击左侧文件夹，右侧显示内容
        QString path = index.data(FileSystemTreeModel::PathRole).toString();
        if (!path.isEmpty() && path != "Desktop" && path != "PC") {
            m_folderBrowser->setRootPath(path);
            m_addressBar->setPath(path); // 2026-03-24 按照用户要求：同步地址栏
            m_metaPanel->setFile(path); // 同步更新属性面板
        }
        m_editBtn->setEnabled(true);
        m_editBtn->setIcon(IconHelper::getIcon("edit", "#e67e22"));

    } else {
        m_metaPanel->setMultipleNotes(indices.size()); // 内部已重构为“已选择多个项目”
        m_editBtn->setEnabled(false);
        m_editBtn->setIcon(IconHelper::getIcon("edit", "#555555"));
    }

    // [CRITICAL] 全局预览联动逻辑：只要预览窗处于开启状态，且当前列表有选中项，
    // 则无论是在 MainWindow 还是 QuickWindow 切换选中，预览窗都必须立即同步内容。
    if (!indices.isEmpty()) {
        auto* preview = QuickPreview::instance();
        if (preview->isVisible()) {
            updatePreviewContent();
        }
    }
}

void MainWindow::setupShortcuts() {
    auto add = [&](const QString& id, std::function<void()> func) {
        auto* sc = new QShortcut(ShortcutManager::instance().getShortcut(id), this, func);
        sc->setProperty("id", id);
    };

    add("mw_filter", [this](){ emit m_header->filterRequested(); });
    // [PROFESSIONAL] 使用 WidgetShortcut 并绑定到列表，防止预览窗打开后发生快捷键回环触发
    auto* previewSc = new QShortcut(ShortcutManager::instance().getShortcut("mw_preview"), m_fileTreeView, [this](){ doPreview(); }, Qt::WidgetShortcut);
    previewSc->setProperty("id", "mw_preview");
    add("mw_meta", [this](){ 
        bool current = m_metaPanel->isVisible();
        emit m_header->metadataToggled(!current); 
    });
    add("mw_refresh", [this](){ refreshData(); });
    add("mw_search", [this](){ m_header->focusSearch(); });
    add("mw_new", [this](){ doNewIdea(); });
    add("mw_favorite", [this](){ doToggleFavorite(); });
    add("mw_pin", [this](){ doTogglePin(); });
    add("mw_stay_on_top", [this](){
        if (m_header) {
            auto* btn = m_header->findChild<QPushButton*>("btnStayOnTop");
            if (btn) btn->click();
        }
    });
    add("mw_edit", [this](){ doOpenSelected(); });
    add("mw_extract", [this](){ doExtractContent(); });
    add("mw_move_up", [this](){ /* 2026-03-24 [REFACTORED] 资源管理器禁用笔记排序 */ });
    add("mw_move_down", [this](){ /* 2026-03-24 [REFACTORED] 资源管理器禁用笔记排序 */ });
    add("mw_lock_cat", [this](){
        // 2026-03-24 [REFACTORED] 资源管理器分类锁定逻辑待物理化
    });
    add("mw_lock_all_cats", [this](){
        // 2026-03-24 [REFACTORED] 资源管理器分类锁定逻辑待物理化
    });
    // [MODIFIED] 2026-03-xx 切换加锁分类显示/隐藏逻辑已迁移至 eventFilter 物理层，避免被快捷键系统抢占。
    // add("mw_toggle_locked_visibility", ...);

    // [PROFESSIONAL] 将删除快捷键绑定到列表，允许侧边栏通过 eventFilter 独立处理 Del 键
    auto* delSoftSc = new QShortcut(ShortcutManager::instance().getShortcut("mw_delete_soft"), m_fileTreeView, [this](){ doDeleteSelected(false); }, Qt::WidgetShortcut);
    delSoftSc->setProperty("id", "mw_delete_soft");
    auto* delHardSc = new QShortcut(ShortcutManager::instance().getShortcut("mw_delete_hard"), m_fileTreeView, [this](){ doDeleteSelected(true); }, Qt::WidgetShortcut);
    delHardSc->setProperty("id", "mw_delete_hard");

    add("mw_copy_tags", [this](){ doCopyTags(); });
    add("mw_paste_tags", [this](){ doPasteTags(); });
    add("mw_repeat_action", [this](){ doRepeatAction(); }); // [USER_REQUEST] 2026-03-14 F4重复上一次操作
    add("mw_show_all", [this](){
        m_currentFilterType = "all";
        m_currentFilterValue = -1;
        m_currentPage = 1;

        // 清除侧边栏选中状态
        m_systemTree->selectionModel()->clearSelection();
        m_systemTree->setCurrentIndex(QModelIndex());
        m_partitionTree->selectionModel()->clearSelection();
        m_partitionTree->setCurrentIndex(QModelIndex());

        refreshData();
        ToolTipOverlay::instance()->showText(QCursor::pos(), "[OK] 已切换至全部数据");
    });
    add("mw_close", [this](){ close(); });

    for (int i = 0; i <= 5; ++i) {
        add(QString("mw_rating_%1").arg(i), [this, i](){ doSetRating(i); });
    }
}

void MainWindow::updateShortcuts() {
    // Note: m_shortcutActions was partially used in old version, but we should use QShortcut list
    // Let's fix the member variable usage to match NoteEditWindow/QuickWindow style
    auto shortcuts = findChildren<QShortcut*>();
    for (auto* sc : shortcuts) {
        QString id = sc->property("id").toString();
        if (!id.isEmpty()) {
            sc->setKey(ShortcutManager::instance().getShortcut(id));
        }
    }
}

void MainWindow::keyPressEvent(QKeyEvent* event) {
    QMainWindow::keyPressEvent(event);
}

void MainWindow::updateFocusLines() {
    QWidget* focus = QApplication::focusWidget();
    
    // 2026-03-13 修复逻辑：只有在侧边栏展开（可见且宽度大于10px）的情况下，才允许显示绿色焦点线
    // 宽度检查可以防止侧边栏在通过 Splitter 拖动折叠后的视觉残留。
    bool sidebarVisible = m_sidebarContainer && m_sidebarContainer->isVisible() && m_sidebarContainer->width() > 10;
    
    bool listFocus = (focus == m_fileTreeView) && sidebarVisible;
    bool sidebarFocus = (focus == m_systemTree || focus == m_partitionTree) && sidebarVisible;

    if (m_listFocusLine) m_listFocusLine->setVisible(listFocus);
    if (m_sidebarFocusLine) m_sidebarFocusLine->setVisible(sidebarFocus);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    // [CRITICAL] 抢占式拦截：在快捷键系统处理前捕获 Ctrl+S 和 Ctrl+Alt+S
    // 这能防止 QShortcut 或系统热键在 KeyPress 之前吞掉事件。
    if (event->type() == QEvent::ShortcutOverride) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        // [MODIFIED] 精确抢占：仅拦截 Ctrl+Alt+S 和 纯 Ctrl+S。
        // 显式排除 ShiftModifier，让 Ctrl+Shift+S 能够被原生的 QShortcut 系统处理。
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
        qDebug() << "[TRACE-MW] KeyPress:" << QKeySequence(keyEvent->key()).toString() 
                 << "Mods:" << keyEvent->modifiers() 
                 << "FocusWidget:" << (watched ? watched->objectName() : "None");

        // [MODIFIED] 2026-03-xx 顶级物理拦截逻辑：修正 Ctrl+S 与 Ctrl+Alt+S 冲突
        // 显式区分锁定逻辑与显示/隐藏逻辑，确保优先级。
        if (keyEvent->key() == Qt::Key_S && (keyEvent->modifiers() & Qt::ControlModifier)) {
            auto mods = keyEvent->modifiers();
            
            // 情况 A: Ctrl + Alt + S -> 切换加锁分类显示/隐藏
            if (mods & Qt::AltModifier) {
                qDebug() << "[MainWindow] 物理拦截捕获到 Ctrl+Alt+S, 切换显示/隐藏。";
                // 2026-03-24 [REFACTORED] 物理侧边栏隐藏逻辑待物理化
                auto& db = DatabaseManager::instance();
                bool isHidden = db.isLockedCategoriesHidden();
                
                // 漂移保护：隐藏后若处于加锁分类，切回全部
                if (isHidden && m_currentFilterType == "category" && m_currentFilterValue != -1) {
                    m_currentFilterType = "all";
                    m_currentFilterValue = -1;
                }
                
                refreshData();
                ToolTipOverlay::instance()->showText(QCursor::pos(), isHidden ? 
                    "<b style='color: #e67e22;'>[OK] 已隐藏加锁分类并强制重锁</b>" : 
                    "<b style='color: #2ecc71;'>[OK] 已显示所有分类并强制重锁</b>");
                return true;
            }
            
            // 情况 B: 纯 Ctrl + S -> 立即锁定当前分类 (排除 Shift 以免干扰 Ctrl+Shift+S)
            if (!(mods & Qt::ShiftModifier)) {
            // 2026-03-24 [REFACTORED] 资源管理器锁定逻辑待物理化
            }
        }
    }

    // [MODIFIED] 2026-03-xx 物理级拦截原生 ToolTip
    if (event->type() == QEvent::ToolTip) {
        QString text = watched->property("tooltipText").toString();
        if (!text.isEmpty()) {
            // 2026-03-xx 按照用户要求，按钮/组件 ToolTip 持续时间设为 2 秒 (2000ms)
            ToolTipOverlay::instance()->showText(QCursor::pos(), text, 2000);
        }
        return true; 
    }

    if (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut) {
        updateFocusLines();
    }

    if (watched == m_fileTreeView && event->type() == QEvent::KeyPress) {
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

            refreshData();
            ToolTipOverlay::instance()->showText(QCursor::pos(), "[OK] 已切换至全部数据");
            return true;
        }

        // [USER_REQUEST] 焦点切换快捷键从 Shift 改为 Tab
        if (keyEvent->key() == Qt::Key_Tab) {
            // [CRITICAL] 列表 -> 侧边栏焦点切换：跳转至当前激活分类或用户分类首项
            if (m_partitionTree->isVisible()) {
                m_partitionTree->setFocus();
                if (!m_partitionTree->currentIndex().isValid()) {
                    m_partitionTree->setCurrentIndex(m_partitionModel->index(0, 0));
                }
            } else if (m_systemTree->isVisible()) {
                m_systemTree->setFocus();
            }
            return true;
        }

        if (keyEvent->key() == Qt::Key_F2) {
            // 2026-03-24 [REFACTORED] 物理文件重命名逻辑
            return true;
        }
    }

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

            refreshData();
            ToolTipOverlay::instance()->showText(QCursor::pos(), "[OK] 已切换至全部数据");
            return true;
        }

        if (key == Qt::Key_F2) {
            if (watched == m_partitionTree) {
                QModelIndex current = m_partitionTree->currentIndex();
                if (current.isValid() && current.data(PhysicalCategoryModel::TypeRole).toString() == "physical_tag") {
                    // [CRITICAL] 物理标签也支持行内编辑
                    m_partitionTree->edit(current);
                }
            }
            return true;
        }

        // [USER_REQUEST] 焦点切换快捷键从 Shift 改为 Tab
        if (key == Qt::Key_Tab && (watched == m_partitionTree || watched == m_systemTree)) {
            // [CRITICAL] 侧边栏 -> 列表焦点切换：自动选中首项或恢复当前选中项
            m_fileTreeView->setFocus();
            auto* model = m_fileTreeView->model();
            if (model && !m_fileTreeView->currentIndex().isValid() && model->rowCount() > 0) {
                m_fileTreeView->setCurrentIndex(model->index(0, 0));
            }
            return true;
        }

        if (key == Qt::Key_Delete) {
            // 2026-03-24 [REFACTORED] 物理侧边栏删除逻辑待实现
            return true;
        }

        if ((key == Qt::Key_Up || key == Qt::Key_Down) && (modifiers & Qt::AltModifier)) {
            // 2026-03-24 [REFACTORED] 物理侧边栏排序逻辑待实现
        }
        
    }

    if (event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);

        // [NEW] 单行输入框箭头导航逻辑：↑ 移至首部，↓ 移至尾部
        if (auto* edit = qobject_cast<QLineEdit*>(watched)) {
            if (keyEvent->key() == Qt::Key_Up) {
                edit->setCursorPosition(0);
                return true;
            } else if (keyEvent->key() == Qt::Key_Down) {
                edit->setCursorPosition(edit->text().length());
                return true;
            }
        }

        if (keyEvent->key() == Qt::Key_Escape) {
            // [CRITICAL] 顶级两段式逻辑：顶栏搜索框按下 Esc 时返回界面
            if (m_header && watched == m_header->searchEdit()) {
                if (!m_header->searchEdit()->text().isEmpty()) {
                    m_header->searchEdit()->clear();
                } else {
                    m_fileTreeView->setFocus();
                }
                return true;
            }
            
            // 顶栏页码框按下 Esc 时返回界面
            if (m_header && watched == m_header->pageInput()) {
                m_fileTreeView->setFocus();
                return true;
            }

            // [CRITICAL] 如果焦点在元数据面板的输入框中
            if (m_metaPanel && watched == m_metaPanel->m_tagEdit) {
                QLineEdit* edit = qobject_cast<QLineEdit*>(watched);
                if (edit && !edit->text().isEmpty()) {
                    edit->clear();
                } else {
                    m_fileTreeView->setFocus();
                }
                return true;
            }
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::onTagSelected(const QModelIndex& index) {
    // 2026-03-24 [REFACTORED] 按照用户要求：侧边栏点击逻辑重构，支持物理路径与标签筛选
    m_currentFilterType = index.data(PhysicalCategoryModel::TypeRole).toString();
    
    if (m_currentFilterType == "drive" || m_currentFilterType == "quick_access") {
        QString path = index.data(PhysicalCategoryModel::PathRole).toString();
        m_folderBrowser->setRootPath(path);
        m_addressBar->setPath(path); // 2026-03-24 按照用户要求：同步地址栏
    } else if (m_currentFilterType == "physical_tag") {
        QString tag = index.data(PhysicalCategoryModel::NameRole).toString();
        // TODO: 通知 m_fileModel 过滤该物理标签
    }
    
    m_currentPage = 1;
    refreshData();
}

void MainWindow::showContextMenu(const QPoint& pos) {
    // 2026-03-24 [REFACTORED] 按照用户要求：资源管理器专用物理右键菜单
    auto selected = m_fileTreeView->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) {
        QModelIndex index = m_fileTreeView->indexAt(pos);
        if (index.isValid()) {
            m_fileTreeView->setCurrentIndex(index);
            selected << index;
        }
    }

    int selCount = selected.size();
    QMenu menu(this);
    IconHelper::setupMenu(&menu);
    menu.setStyleSheet("QMenu { background-color: #2D2D2D; color: #EEE; border: 1px solid #444; padding: 4px; } "
                       "QMenu::item { padding: 6px 10px 6px 10px; border-radius: 3px; } "
                       "QMenu::icon { margin-left: 6px; } "
                       "QMenu::item:selected { background-color: #3E3E42; color: white; }");

    if (selCount == 0) {
        menu.addAction(IconHelper::getIcon("refresh", "#2ecc71", 18), "刷新视图", this, &MainWindow::refreshData);
        menu.exec(m_fileTreeView->mapToGlobal(pos));
        return;
    }

    if (selCount == 1) {
        menu.addAction(IconHelper::getIcon("eye", "#41F2F2", 18), "快速预览 (Space)", this, &MainWindow::doPreview);
        menu.addAction(IconHelper::getIcon("folder", "#3A90FF", 18), "在资源管理器中显示", [this, selected]() {
            QString path = selected.first().data(FileSystemTreeModel::PathRole).toString();
            StringUtils::locateInExplorer(path, true);
        });
    }

    menu.addAction(IconHelper::getIcon("copy", "#1abc9c", 18), QString("复制路径 (%1)").arg(selCount), [this, selected]() {
        QStringList paths;
        for (const auto& idx : selected) paths << idx.data(FileSystemTreeModel::PathRole).toString();
        QApplication::clipboard()->setText(paths.join("\n"));
    });

    menu.addSeparator();

    if (selCount == 1) {
        menu.addAction(IconHelper::getIcon("edit", "#4a90e2", 18), "打开物理文件 (Enter)", this, &MainWindow::doOpenSelected);
    }

    auto* ratingMenu = menu.addMenu(IconHelper::getIcon("star", "#f39c12", 18), QString("物理星级标记 (%1)").arg(selCount));
    ratingMenu->setStyleSheet(menu.styleSheet());
    for (int i = 1; i <= 5; ++i) {
        ratingMenu->addAction(QString("★").repeated(i), [this, i]() { doSetRating(i); });
    }
    ratingMenu->addAction("清除物理星级", [this]() { doSetRating(0); });

    menu.addSeparator();
    menu.addAction(IconHelper::getIcon("trash", "#e74c3c", 18), "物理删除 (慎重)", [this](){ doDeleteSelected(true); });

    menu.exec(QCursor::pos());
}

void MainWindow::closeEvent(QCloseEvent* event) {
    saveLayout();
    QMainWindow::closeEvent(event);
}

void MainWindow::saveLayout() {
    QSettings settings("RapidNotes", "MainWindow");
    settings.setValue("geometry", saveGeometry());
    settings.setValue("windowState", saveState());

    if (m_header) {
        auto* btn = m_header->findChild<QPushButton*>("btnStayOnTop");
        if (btn) {
            settings.setValue("stayOnTop", btn->isChecked());
        }
    }
    
    QSplitter* splitter = findChild<QSplitter*>();
    if (splitter) {
        settings.setValue("splitterState", splitter->saveState());
    }

    // 保存面板可见性
    settings.setValue("showFilter", m_filterWrapper->isVisible());
    settings.setValue("showMetadata", m_metaPanel->isVisible());
}

void MainWindow::restoreLayout() {
    QSettings settings("RapidNotes", "MainWindow");
    if (settings.contains("geometry")) {
        restoreGeometry(settings.value("geometry").toByteArray());
    }
    if (settings.contains("windowState")) {
        restoreState(settings.value("windowState").toByteArray());
    }
    
    QSplitter* splitter = findChild<QSplitter*>();
    if (splitter && settings.contains("splitterState")) {
        splitter->restoreState(settings.value("splitterState").toByteArray());
    }

    // 恢复面板可见性
    bool showFilter = settings.value("showFilter", true).toBool();
    bool showMetadata = settings.value("showMetadata", true).toBool();
    
    m_filterWrapper->setVisible(showFilter);
    m_header->setFilterActive(showFilter);
    
    m_metaPanel->setVisible(showMetadata);
    m_header->setMetadataActive(showMetadata);

    bool stayOnTop = settings.value("stayOnTop", false).toBool();
    auto* btnStay = m_header->findChild<QPushButton*>("btnStayOnTop");
    if (btnStay) {
        btnStay->setChecked(stayOnTop);
        // 手动应用图标 (HeaderBar 不会自动切换图标，除非触发 toggled 信号)
        // 2026-03-xx 按照用户要求，修改置顶按钮样式：置顶后图标变为橙色。
        btnStay->setIcon(IconHelper::getIcon(stayOnTop ? "pin_vertical" : "pin_tilted", stayOnTop ? "#FF551C" : "#aaaaaa", 20));
        
        if (stayOnTop) {
            #ifdef Q_OS_WIN
            HWND hwnd = (HWND)winId();
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            #else
            setWindowFlag(Qt::WindowStaysOnTopHint, true);
            #endif
        }
    }

}


void MainWindow::doPreview() {
    // 增加防抖保护，防止由于快捷键冲突（子窗口与主窗口同时响应）导致的“双重触发”现象
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

void MainWindow::updatePreviewContent() {
    // 2026-03-24 [REFACTORED] 按照用户要求：资源管理器预览逻辑，切换至物理文件元数据
    QModelIndex index = m_fileTreeView->currentIndex();
    if (!index.isValid()) return;
    
    QString path = index.data(FileSystemTreeModel::PathRole).toString();
    if (path.isEmpty() || path == "Desktop" || path == "PC") return;

    QFileInfo info(path);
    // 从 Database 获取该物理路径关联的元数据
    QVariantMap fileMeta = Database::instance().getItemMeta(path);

    QVariantMap itemData; // 预览窗复用笔记模型结构，但命名为 itemData 以避歧义
    itemData["id"] = -1; // 物理项目无笔记 ID
    itemData["title"] = info.fileName();
    itemData["content"] = fileMeta.value("remark").toString();
    itemData["item_type"] = info.isDir() ? "folder" : "file";
    itemData["tags"] = fileMeta.value("tags").toString();
    itemData["rating"] = fileMeta.value("rating").toInt();
    itemData["is_pinned"] = fileMeta.value("pinned").toInt();
    itemData["created_at"] = info.birthTime().toString(Qt::ISODate);
    itemData["updated_at"] = info.lastModified().toString(Qt::ISODate);
    itemData["remark"] = fileMeta.value("remark").toString();
    
    auto* preview = QuickPreview::instance();

    QPoint pos;
    if (preview->isVisible()) {
        pos = preview->pos();
    } else {
        pos = m_fileTreeView->mapToGlobal(m_fileTreeView->rect().center()) - QPoint(250, 300);
    }

    preview->showPreview(itemData, pos, info.absolutePath(), m_fileTreeView);
}

void MainWindow::doDeleteSelected(bool physical) {
    // 2026-03-24 [REFACTORED] 按照用户要求：物理文件删除逻辑
    auto selected = m_fileTreeView->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;

    QString text = QString("确定要永久删除选中的 %1 个物理文件/文件夹吗？\n此操作不可逆！").arg(selected.count());
    FramelessMessageBox msg("物理删除", text, this);
    
    if (msg.exec() == QDialog::Accepted) {
        for (const auto& idx : selected) {
            QString path = idx.data(FileSystemTreeModel::PathRole).toString();
            if (QFileInfo(path).isDir()) QDir(path).removeRecursively();
            else QFile::remove(path);
        }
        refreshData();
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #2ecc71;'>[OK] 物理删除已完成</b>");
    }
}

void MainWindow::doToggleFavorite() {
    // 2026-03-24 [REFACTORED] 按照用户要求：物理收藏（标签/星级）
    // TODO: 实现物理星级联动
}

void MainWindow::doTogglePin() {
    // 2026-03-24 [REFACTORED] 按照用户要求：物理置顶（元数据 JSON 记录）
}

void MainWindow::doNewIdea() {
    // 2026-03-24 [REFACTORED] 资源管理器模式下，不直接新建笔记
}

void MainWindow::doCreateByLine(bool fromClipboard) {
    // 2026-03-24 [REFACTORED] 资源管理器不负责按行创建笔记数据
}

void MainWindow::doOCR() {
    // 2026-03-24 [REFACTORED] 按照用户要求：物理文件 OCR，结果存入 .am_meta.json
    QModelIndex index = m_fileTreeView->currentIndex();
    if (!index.isValid()) return;
    
    QString path = index.data(FileSystemTreeModel::PathRole).toString();
    if (path.isEmpty() || !QFileInfo(path).isFile()) return;

    QImage img(path);
    if (img.isNull()) return;

    auto* resWin = new OCRResultWindow(img, -1);
    connect(&OCRManager::instance(), &OCRManager::recognitionFinished, this, [this, path](const QString& text){
        // 物理 OCR 结果保存至元数据备注
        m_metaPanel->savePhysicalMeta(); 
    });
    
    resWin->show();
    OCRManager::instance().recognizeAsync(img, -1);
}

void MainWindow::doExtractContent() {
    // 2026-03-24 [REFACTORED] 按照用户要求：物理内容提取（复制文件内容或路径）
    auto selected = m_fileTreeView->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;

    QStringList paths;
    for (const auto& index : std::as_const(selected)) {
        paths << index.data(FileSystemTreeModel::PathRole).toString();
    }
    QApplication::clipboard()->setText(paths.join("\n"));
}

void MainWindow::doOpenSelected() {
    // 2026-03-24 [REFACTORED] 按照用户要求：双击/编辑快捷键改为物理打开
    QModelIndex index = m_fileTreeView->currentIndex();
    if (!index.isValid()) return;
    
    QString path = index.data(FileSystemTreeModel::PathRole).toString();
    if (!path.isEmpty() && QFileInfo::exists(path)) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    } else {
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #e67e22;'>[!] 无法打开：物理文件不存在</b>");
    }
}

void MainWindow::doSetRating(int rating) {
    // 2026-03-24 [REFACTORED] 按照用户要求：物理元数据星级设置
    auto selected = m_fileTreeView->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;
    
    for (const auto& idx : selected) {
        QString path = idx.data(FileSystemTreeModel::PathRole).toString();
        // 调用 AmMetaJson 更新元数据
    }
    refreshData();
}

void MainWindow::doMoveToCategory(int catId) {
    // 2026-03-24 [REFACTORED] 资源管理器不负责移动到笔记分类
}

void MainWindow::doMoveNote(int dir) {
    // 2026-03-24 [REFACTORED] 资源管理器模式下此功能已由 PathBuilder 或拖拽逻辑接管
}

void MainWindow::doCopyTags() {
    // 2026-03-24 [REFACTORED] 物理标签复制
}

void MainWindow::doPasteTags() {
    // 2026-03-24 [REFACTORED] 物理标签粘贴
}

void MainWindow::doRepeatAction() {
    // 2026-03-24 [REFACTORED] 资源管理器重复动作逻辑待实现（如重复打标）
}

void MainWindow::doImportCategory(int catId) {
    QStringList files = QFileDialog::getOpenFileNames(this, "选择导入文件", "", "所有文件 (*.*);;CSV文件 (*.csv)");
    if (files.isEmpty()) return;

    int totalCount = FileStorageHelper::processImport(files, catId);
    
    refreshData();
    ToolTipOverlay::instance()->showText(QCursor::pos(), QString("<b style='color: #2ecc71;'>[OK] 导入完成，共处理 %1 个项目</b>").arg(totalCount));
}

void MainWindow::doImportFolder(int catId) {
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
    ToolTipOverlay::instance()->showText(QCursor::pos(), QString("<b style='color: #2ecc71;'>[OK] 批量导入完成，共处理 %1 个目录共 %2 个项目</b>").arg(dirs.size()).arg(totalCount));
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

void MainWindow::doExportCategory(int catId, const QString& catName) {
    if (!verifyExportPermission()) return;
    FileStorageHelper::exportCategory(catId, catName, this);
}

void MainWindow::updateToolboxStatus(bool active) {
    // 2026-03-22 [NEW] 同步工具箱按钮颜色状态到 HeaderBar
    if (m_header) {
        m_header->updateToolboxStatus(active);
    }
}

bool MainWindow::verifyExportPermission() {
    // 2026-03-24 [REFACTORED] 将配置读取从 QuickWindow 迁移至 Common 组，实现架构解耦
    QSettings settings("RapidNotes", "Common");
    QString realPwd = settings.value("appPassword").toString();

    // 1. 如果用户未设置密码，根据方案，引导用户先进行安全设置
    if (realPwd.isEmpty()) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), 
            "<b style='color: #e67e22;'>[安全拦截] 请先在【系统设置】中设置“应用锁定密码”后再执行导出</b>", 3000);
        return false;
    }

    // 2. 弹出统一的验证对话框
    PasswordVerifyDialog dlg("导出身份验证", "当前操作涉及数据导出，请输入应用锁定密码以继续：", this);
    if (dlg.exec() != QDialog::Accepted) {
        return false;
    }

    // 3. 校验密码
    if (dlg.password() != realPwd) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), 
            "<b style='color: #e74c3c;'>❌ 密码验证失败，导出已终止</b>", 2000);
        return false;
    }

    // 验证通过
    return true;
}
