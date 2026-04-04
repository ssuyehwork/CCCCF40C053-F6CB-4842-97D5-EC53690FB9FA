#include "ToolTipOverlay.h"
#include "MainWindow.h"
#include <QDebug>
#include <QListView>
#include <QTreeView>
#include "StringUtils.h"
#include "CategoryDelegate.h"
#include "TitleEditorDialog.h"
#include "../db/CategoryRepo.h"
#include "../db/ItemRepo.h"
#include "../core/HotkeyManager.h"
#include "IconHelper.h"
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
#include "../core/ShortcutManager.h"
#include <functional>
#include <QVariant>
#include <QtGlobal>

#ifdef Q_OS_WIN
#include <windows.h>
#include <windowsx.h>
#define RESIZE_MARGIN 10
#endif

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent, Qt::FramelessWindowHint) {
    setWindowTitle("ArcMeta");
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
    m_refreshTimer->setSingleShot(false); 
    m_refreshTimer->setInterval(30000); 
    connect(m_refreshTimer, &QTimer::timeout, this, [this](){
        QString today = QDate::currentDate().toString("yyyy-MM-dd");
        if (m_lastRefreshDate != today) {
            refreshData();
        }
    });
    m_refreshTimer->start();

    m_lastRefreshDate = QDate::currentDate().toString("yyyy-MM-dd");
    
    // 延迟加载初始数据
    QTimer::singleShot(500, this, &MainWindow::refreshData);

    restoreLayout(); 
    setupShortcuts();
    connect(&ShortcutManager::instance(), &ShortcutManager::shortcutsChanged, this, &MainWindow::updateShortcuts);
    
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

    m_header = new HeaderBar(this);
    connect(m_header, &HeaderBar::searchChanged, this, [this](const QString& text){
        m_currentKeyword = text;
        m_currentPage = 1;
        m_searchTimer->start(300);
    });
    connect(m_header, &HeaderBar::pageChanged, this, [this](int page){
        m_currentPage = page;
        refreshData();
    });
    connect(m_header, &HeaderBar::refreshRequested, this, &MainWindow::refreshData);
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
    });
    connect(m_header, &HeaderBar::createItemRequested, this, &MainWindow::doCreateNewItem);

    connect(m_header, &HeaderBar::toggleSidebar, this, [this](){
        m_sidebarContainer->setVisible(!m_sidebarContainer->isVisible());
        updateFocusLines();
    });
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

    auto* contentWidget = new QWidget(centralWidget);
    contentWidget->setAttribute(Qt::WA_StyledBackground, true);
    contentWidget->setStyleSheet("background: transparent; border: none;");
    auto* contentLayout = new QVBoxLayout(contentWidget);
    contentLayout->setContentsMargins(5, 5, 5, 5); 
    contentLayout->setSpacing(0);

    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->setHandleWidth(5); 
    splitter->setChildrenCollapsible(false);
    splitter->setAttribute(Qt::WA_StyledBackground, true);
    splitter->setStyleSheet("QSplitter { background: transparent; border: none; } QSplitter::handle { background: transparent; }");

    m_sidebarContainer = new QFrame();
    m_sidebarContainer->setMinimumWidth(230);
    m_sidebarContainer->setObjectName("SidebarContainer");
    m_sidebarContainer->setAttribute(Qt::WA_StyledBackground, true);
    m_sidebarContainer->setStyleSheet("#SidebarContainer { background-color: #1e1e1e; border: 1px solid #333333; border-radius: 0px; }");

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
    m_sidebarFocusLine->setStyleSheet("background-color: #3498db;");
    m_sidebarFocusLine->hide();
    sidebarContainerLayout->addWidget(m_sidebarFocusLine);

    auto* sidebarHeader = new QWidget();
    sidebarHeader->setFixedHeight(32);
    sidebarHeader->setStyleSheet("background-color: #252526; border-bottom: 1px solid #333;");
    auto* sidebarHeaderLayout = new QHBoxLayout(sidebarHeader);
    sidebarHeaderLayout->setContentsMargins(15, 0, 15, 0);
    auto* sbIcon = new QLabel();
    sbIcon->setPixmap(IconHelper::getIcon("category", "#3498db").pixmap(18, 18));
    sidebarHeaderLayout->addWidget(sbIcon);
    auto* sbTitle = new QLabel("资源管理器");
    sbTitle->setStyleSheet("color: #3498db; font-size: 13px; font-weight: bold; background: transparent; border: none;");
    sidebarHeaderLayout->addWidget(sbTitle);
    sidebarHeaderLayout->addStretch();
    
    sidebarHeader->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(sidebarHeader, &QWidget::customContextMenuRequested, this, [this, splitter, sidebarHeader](const QPoint& pos){
        QMenu menu;
        IconHelper::setupMenu(&menu);
        menu.setStyleSheet("QMenu { background-color: #2D2D2D; color: #EEE; border: 1px solid #444; padding: 4px; } "
                           "QMenu::item { padding: 6px 10px 6px 10px; border-radius: 3px; } "
                           "QMenu::icon { margin-left: 6px; } "
                           "QMenu::item:selected { background-color: #3E3E42; }");
        menu.addAction("向左移动", [this, splitter](){
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

    auto* sbContent = new QWidget();
    sbContent->setAttribute(Qt::WA_StyledBackground, true);
    auto* sbContentLayout = new QVBoxLayout(sbContent);
    sbContentLayout->setContentsMargins(7, 10, 0, 0);
    sbContentLayout->setSpacing(0);

    QString treeStyle = R"(
        QTreeView { background-color: transparent; border: none; color: #CCC; outline: none; }
        QTreeView::item:!selectable { color: #ffffff; font-weight: bold; }
        QTreeView::item { height: 22px; padding: 0px; }
    )";

    m_systemTree = new ArcMeta::DropTreeView();
    m_systemTree->setStyleSheet(treeStyle); 
    m_systemTree->setItemDelegate(new CategoryDelegate(this));
    m_systemModel = new ArcMeta::CategoryModel(ArcMeta::CategoryModel::System, this);
    m_systemTree->setModel(m_systemModel);
    m_systemTree->setHeaderHidden(true);
    m_systemTree->setRootIsDecorated(true);
    m_systemTree->setIndentation(12);
    m_systemTree->setFixedHeight(176); 
    m_systemTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_systemTree->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_systemTree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_systemTree->setContextMenuPolicy(Qt::CustomContextMenu);

    m_partitionTree = new ArcMeta::DropTreeView();
    m_partitionTree->setStyleSheet(treeStyle);
    m_partitionTree->setItemDelegate(new CategoryDelegate(this));
    m_partitionModel = new ArcMeta::CategoryModel(ArcMeta::CategoryModel::User, this);
    m_partitionTree->setModel(m_partitionModel);
    m_partitionTree->setHeaderHidden(true);
    m_partitionTree->setRootIsDecorated(true);
    m_partitionTree->setIndentation(12);
    m_partitionTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_partitionTree->setDragEnabled(true);
    m_partitionTree->setAcceptDrops(true);
    m_partitionTree->setDropIndicatorShown(true);
    m_partitionTree->setDragDropMode(QAbstractItemView::InternalMove);
    m_partitionTree->setDefaultDropAction(Qt::MoveAction);
    m_partitionTree->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_partitionTree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    safeExpandPartitionTree(); 
    m_partitionTree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_partitionTree->setContextMenuPolicy(Qt::CustomContextMenu);
    
    sbContentLayout->addWidget(m_systemTree);
    sbContentLayout->addWidget(m_partitionTree);
    sidebarContainerLayout->addWidget(sbContent);
    splitter->addWidget(m_sidebarContainer);

    auto onSidebarMenu = [this](const QPoint& pos){
        auto* tree = qobject_cast<QTreeView*>(sender());
        if (!tree) return;
        QModelIndex index = tree->indexAt(pos);
        QMenu menu(this);
        IconHelper::setupMenu(&menu);
        menu.setStyleSheet("QMenu { background-color: #2D2D2D; color: #EEE; border: 1px solid #444; padding: 4px; } "
                           "QMenu::item { padding: 6px 10px 6px 10px; border-radius: 3px; } "
                           "QMenu::item:selected { background-color: #3E3E42; color: white; }");

        if (!index.isValid() || index.data(ArcMeta::CategoryModel::NameRole).toString() == "我的分类") {
            menu.addAction(IconHelper::getIcon("add", "#3498db", 18), "新建分类", [this]() {
                FramelessInputDialog dlg("新建分类", "组名称:", "", this);
                if (dlg.exec() == QDialog::Accepted) {
                    QString text = dlg.text();
                    if (!text.isEmpty()) {
                        ArcMeta::Category cat;
                        cat.name = text.toStdWString();
                        ArcMeta::CategoryRepo::add(cat);
                        refreshData();
                    }
                }
            });
            menu.exec(tree->mapToGlobal(pos));
            return;
        }

        QString type = index.data(ArcMeta::CategoryModel::TypeRole).toString();
        if (type == "category") {
            int catId = index.data(ArcMeta::CategoryModel::IdRole).toInt();
            menu.addAction(IconHelper::getIcon("add", "#3498db", 18), "创建子分类", [this, catId]() {
                FramelessInputDialog dlg("新建子分类", "区名称:", "", this);
                if (dlg.exec() == QDialog::Accepted) {
                    QString text = dlg.text();
                    if (!text.isEmpty()) {
                        ArcMeta::Category cat;
                        cat.name = text.toStdWString();
                        cat.parentId = catId;
                        ArcMeta::CategoryRepo::add(cat);
                        refreshData();
                    }
                }
            });
            menu.addAction(IconHelper::getIcon("trash", "#e74c3c", 18), "删除分类", [this, index]() {
                FramelessMessageBox dlg("确认删除", "确定要删除此分类吗？", this);
                if (dlg.exec() == QDialog::Accepted) {
                    ArcMeta::CategoryRepo::remove(index.data(ArcMeta::CategoryModel::IdRole).toInt());
                    refreshData();
                }
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

    // 核心转型：接入 ContentPanel
    m_contentPanel = new ArcMeta::ContentPanel(this);
    connect(m_contentPanel, &ArcMeta::ContentPanel::directorySelected, this, [this](const QString& path){
        m_contentPanel->loadDirectory(path);
    });
    connect(m_contentPanel, &ArcMeta::ContentPanel::selectionChanged, this, [this](const QStringList& paths){
        if (!paths.isEmpty()) {
            // 这里可以对接元数据面板显示第一个选中的文件信息
            qDebug() << "选中资源:" << paths.first();
        }
    });
    splitter->addWidget(m_contentPanel);
    
    m_metaPanel = new MetadataPanel(this);
    m_metaPanel->setMinimumWidth(230);
    splitter->addWidget(m_metaPanel);

    m_filterContainer = new QFrame();
    m_filterContainer->setMinimumWidth(230);
    m_filterContainer->setObjectName("FilterContainer");
    m_filterContainer->setStyleSheet("#FilterContainer { background-color: #1e1e1e; border: 1px solid #333333; border-radius: 0px; }");
    m_filterPanel = new FilterPanel(this);
    auto* flayout = new QVBoxLayout(m_filterContainer);
    flayout->addWidget(m_filterPanel);
    m_filterWrapper = m_filterContainer;
    splitter->addWidget(m_filterContainer);

    mainLayout->addWidget(contentWidget);
    contentLayout->addWidget(splitter);
}

void MainWindow::refreshData() {
    m_systemModel->refresh();
    m_partitionModel->refresh();
    
    // 如果没有当前目录，默认加载第一个分区或计算机视图
    if (m_contentPanel->model()->rowCount() == 0) {
        m_contentPanel->loadDirectory("computer://");
    }
}

void MainWindow::onTagSelected(const QModelIndex& index) {
    QString type = index.data(ArcMeta::CategoryModel::TypeRole).toString();
    if (type == "category") {
        int catId = index.data(ArcMeta::CategoryModel::IdRole).toInt();
        std::vector<std::wstring> paths = ArcMeta::CategoryRepo::getItemPathsInCategory(catId);
        QStringList qPaths;
        for (const auto& p : paths) qPaths << QString::fromStdWString(p);
        m_contentPanel->loadPaths(qPaths);
    } else if (type == "all" || type == "today" || type == "yesterday" || type == "recently_visited") {
         // 系统分类逻辑可在此根据需要实现全盘搜索或特定过滤
         m_contentPanel->loadDirectory("computer://");
    }
}

void MainWindow::doCreateNewItem(const QString& type) {
    m_contentPanel->createNewItem(type);
}

// 补全存根
void MainWindow::scheduleRefresh() { QTimer::singleShot(300, this, &MainWindow::refreshData); }
void MainWindow::onSelectionChanged(const QItemSelection& s, const QItemSelection& d) { }
void MainWindow::showContextMenu(const QPoint& p) { }
void MainWindow::saveLayout() { }
void MainWindow::restoreLayout() { }
void MainWindow::updateShortcuts() { }
void MainWindow::setupShortcuts() { }
void MainWindow::updateFocusLines() { }
void MainWindow::safeExpandPartitionTree() { }
void MainWindow::doDeleteSelected(bool p) { }
void MainWindow::doToggleFavorite() { }
void MainWindow::doTogglePin() { }
void MainWindow::doCreateByLine(bool f) { }
void MainWindow::doExtractContent() { }
void MainWindow::doSetRating(int r) { }
void MainWindow::doMoveToCategory(int c) { }
void MainWindow::doCopyTags() { }
void MainWindow::doPasteTags() { }
void MainWindow::doImportCategory(int c) { }
void MainWindow::doImportFolder(int c) { }
void MainWindow::doExportCategory(int c, const QString& n) { }
bool MainWindow::verifyExportPermission() { return true; }
void MainWindow::doPreview() { }
void MainWindow::updatePreviewContent() { }
void MainWindow::showEvent(QShowEvent* e) { QMainWindow::showEvent(e); }
void MainWindow::closeEvent(QCloseEvent* e) { QMainWindow::closeEvent(e); }
bool MainWindow::event(QEvent* e) { return QMainWindow::event(e); }
bool MainWindow::eventFilter(QObject* w, QEvent* e) { return QMainWindow::eventFilter(w, e); }
void MainWindow::dragEnterEvent(QDragEnterEvent* e) { }
void MainWindow::dragMoveEvent(QDragMoveEvent* e) { }
void MainWindow::dropEvent(QDropEvent* e) { }
void MainWindow::keyPressEvent(QKeyEvent* e) { QMainWindow::keyPressEvent(e); }

#ifdef Q_OS_WIN
bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result) { return false; }
#endif
