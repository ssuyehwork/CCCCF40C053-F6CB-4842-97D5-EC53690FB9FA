#include "SearchAppWindow.h"
#include "FileSearchWidget.h"
#include "KeywordSearchWidget.h"
#include "IconHelper.h"
#include "StringUtils.h"
#include "ToolTipOverlay.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QLabel>
#include <QSettings>
#include <QFileInfo>
#include <QDir>
#include <QMenu>
#include <QAction>
#include <QApplication>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QUrl>
#include <QDesktopServices>
#include <QProcess>
#include <QDateTime>
#include <QTextStream>

// ----------------------------------------------------------------------------
// SharedSidebarListWidget
// ----------------------------------------------------------------------------
SharedSidebarListWidget::SharedSidebarListWidget(QWidget* parent) : QListWidget(parent) {
    setAcceptDrops(true);
}

void SharedSidebarListWidget::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}

void SharedSidebarListWidget::dragMoveEvent(QDragMoveEvent* event) {
    event->acceptProposedAction();
}

void SharedSidebarListWidget::dropEvent(QDropEvent* event) {
    if (event->mimeData()->hasUrls()) {
        for (const QUrl& url : event->mimeData()->urls()) {
            QString path = url.toLocalFile();
            if (QDir(path).exists()) emit folderDropped(path);
        }
        event->acceptProposedAction();
    }
}

// ----------------------------------------------------------------------------
// SharedCollectionListWidget
// ----------------------------------------------------------------------------
SharedCollectionListWidget::SharedCollectionListWidget(QWidget* parent) : QListWidget(parent) {
    setAcceptDrops(true);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
}

void SharedCollectionListWidget::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}

void SharedCollectionListWidget::dragMoveEvent(QDragMoveEvent* event) {
    event->acceptProposedAction();
}

void SharedCollectionListWidget::dropEvent(QDropEvent* event) {
    QStringList paths;
    if (event->mimeData()->hasUrls()) {
        for (const QUrl& url : event->mimeData()->urls()) {
            QString p = url.toLocalFile();
            if (!p.isEmpty() && QFileInfo(p).isFile()) paths << p;
        }
    }
    if (!paths.isEmpty()) {
        emit filesDropped(paths);
        event->acceptProposedAction();
    }
}

// ----------------------------------------------------------------------------
// SearchAppWindow
// ----------------------------------------------------------------------------
SearchAppWindow::SearchAppWindow(QWidget* parent)
    : FramelessDialog("综合搜索工具", parent)
{
    setObjectName("SearchAppWindow");
    resize(1300, 850);
    setupStyles();
    initUI();
    loadFavorites();
    loadCollection();
}

SearchAppWindow::~SearchAppWindow() {
}

void SearchAppWindow::setupStyles() {
    // 整体配色参考 VSCode 深色主题
    setStyleSheet(R"(
        QWidget {
            background-color: #1E1E1E;
            color: #D4D4D4;
            font-family: "Microsoft YaHei", "Segoe UI";
            font-size: 13px;
        }
        QSplitter::handle { background-color: #333; }

        /* 侧边栏列表样式 */
        #SidebarList {
            background-color: #252526;
            border: 1px solid #333;
            border-radius: 4px;
            padding: 2px;
        }
        #SidebarList::item {
            height: 32px;
            padding-left: 8px;
            color: #CCC;
            border-radius: 4px;
        }
        #SidebarList::item:hover { background-color: #2A2D2E; }
        #SidebarList::item:selected {
            background-color: #37373D;
            color: #FFF;
            border-left: 3px solid #007ACC;
        }

        /* Tab样式 */
        QTabWidget::pane {
            border: 1px solid #333;
            background: #1E1E1E;
            top: -1px;
        }
        QTabBar::tab {
            background: #2D2D2D;
            color: #888;
            padding: 8px 20px;
            border: 1px solid #333;
            border-bottom: none;
            margin-right: 2px;
        }
        QTabBar::tab:selected {
            background: #1E1E1E;
            color: #FFF;
            border-bottom: 2px solid #007ACC;
            font-weight: bold;
        }

        QLabel#SidebarHeader {
            color: #888;
            font-weight: bold;
            font-size: 11px;
            text-transform: uppercase;
            padding: 5px 2px;
        }

        QPushButton#SidebarActionBtn {
            background-color: #2D2D30;
            border: 1px solid #444;
            color: #AAA;
            border-radius: 4px;
            height: 28px;
        }
        QPushButton#SidebarActionBtn:hover {
            background-color: #3E3E42;
            color: #FFF;
            border-color: #666;
        }
    )");
}

void SearchAppWindow::initUI() {
    auto* mainLayout = new QHBoxLayout(m_contentArea);
    mainLayout->setContentsMargins(10, 5, 10, 10);
    mainLayout->setSpacing(0);

    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->setHandleWidth(1);
    mainLayout->addWidget(splitter);

    // --- 左侧：目录收藏 ---
    auto* leftWidget = new QWidget();
    auto* leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(0, 0, 8, 0);
    leftLayout->setSpacing(6);

    auto* lblLeft = new QLabel("文件夹收藏");
    lblLeft->setObjectName("SidebarHeader");
    leftLayout->addWidget(lblLeft);

    m_sidebar = new SharedSidebarListWidget();
    m_sidebar->setObjectName("SidebarList");
    m_sidebar->setMinimumWidth(220);
    m_sidebar->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_sidebar, &QListWidget::itemClicked, this, &SearchAppWindow::onSidebarItemClicked);
    connect(m_sidebar, &QListWidget::customContextMenuRequested, this, &SearchAppWindow::showSidebarContextMenu);
    connect(m_sidebar, &SharedSidebarListWidget::folderDropped, this, &SearchAppWindow::addFavorite);
    leftLayout->addWidget(m_sidebar);

    auto* btnAddFav = new QPushButton("收藏当前路径");
    btnAddFav->setObjectName("SidebarActionBtn");
    connect(btnAddFav, &QPushButton::clicked, this, &SearchAppWindow::onFavoriteCurrentPath);
    leftLayout->addWidget(btnAddFav);

    splitter->addWidget(leftWidget);

    // --- 中间：Tab 搜索页 ---
    m_tabWidget = new QTabWidget();
    m_fileSearchWidget = new FileSearchWidget();
    m_keywordSearchWidget = new KeywordSearchWidget();

    m_tabWidget->addTab(m_fileSearchWidget, IconHelper::getIcon("folder", "#AAA"), "查找文件");
    m_tabWidget->addTab(m_keywordSearchWidget, IconHelper::getIcon("find_keyword", "#AAA"), "查找关键字");

    splitter->addWidget(m_tabWidget);

    // --- 右侧：文件收藏 ---
    auto* rightWidget = new QWidget();
    auto* rightLayout = new QVBoxLayout(rightWidget);
    rightLayout->setContentsMargins(8, 0, 0, 0);
    rightLayout->setSpacing(6);

    auto* lblRight = new QLabel("文件收藏");
    lblRight->setObjectName("SidebarHeader");
    rightLayout->addWidget(lblRight);

    m_collectionSidebar = new SharedCollectionListWidget();
    m_collectionSidebar->setObjectName("SidebarList");
    m_collectionSidebar->setMinimumWidth(220);
    m_collectionSidebar->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_collectionSidebar, &QListWidget::itemClicked, this, &SearchAppWindow::onCollectionItemClicked);
    connect(m_collectionSidebar, &QListWidget::customContextMenuRequested, this, &SearchAppWindow::showCollectionContextMenu);
    connect(m_collectionSidebar, &SharedCollectionListWidget::filesDropped, this, &SearchAppWindow::addCollectionItems);
    rightLayout->addWidget(m_collectionSidebar);

    auto* btnMerge = new QPushButton("合并收藏内容");
    btnMerge->setObjectName("SidebarActionBtn");
    connect(btnMerge, &QPushButton::clicked, this, &SearchAppWindow::onMergeCollectionFiles);
    rightLayout->addWidget(btnMerge);

    splitter->addWidget(rightWidget);

    // 设置伸缩因子，中间主区域占据主要空间
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 0);

    // 初始化默认比例
    splitter->setSizes({220, 800, 220});
}

void SearchAppWindow::onSidebarItemClicked(QListWidgetItem* item) {
    if (!item) return;
    QString path = item->data(Qt::UserRole).toString();

    // 同步到当前活动的搜索组件
    if (m_tabWidget->currentIndex() == 0) {
        m_fileSearchWidget->setPath(path);
    } else {
        m_keywordSearchWidget->setPath(path);
    }
}

void SearchAppWindow::showSidebarContextMenu(const QPoint& pos) {
    QListWidgetItem* item = m_sidebar->itemAt(pos);
    if (!item) return;

    QMenu menu(this);
    IconHelper::setupMenu(&menu);

    QString path = item->data(Qt::UserRole).toString();
    menu.addAction(IconHelper::getIcon("folder", "#F1C40F"), "在资源管理器中打开", [path](){
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });

    menu.addSeparator();
    menu.addAction(IconHelper::getIcon("pin", "#F1C40F"), "置顶", [this, item](){
        int row = m_sidebar->row(item);
        if (row > 0) {
            QListWidgetItem* taken = m_sidebar->takeItem(row);
            m_sidebar->insertItem(0, taken);
            m_sidebar->setCurrentItem(taken);
            saveFavorites();
        }
    });

    menu.addAction(IconHelper::getIcon("close", "#E74C3C"), "取消收藏", [this, item](){
        delete m_sidebar->takeItem(m_sidebar->row(item));
        saveFavorites();
    });

    menu.exec(m_sidebar->mapToGlobal(pos));
}

void SearchAppWindow::onCollectionItemClicked(QListWidgetItem* item) {
    // 单击暂无操作，保留双击或右键逻辑
}

void SearchAppWindow::showCollectionContextMenu(const QPoint& pos) {
    auto selectedItems = m_collectionSidebar->selectedItems();
    if (selectedItems.isEmpty()) return;

    QMenu menu(this);
    IconHelper::setupMenu(&menu);

    menu.addAction(IconHelper::getIcon("merge", "#3498DB"), "合并选中内容", [this](){
        onMergeCollectionFiles();
    });

    menu.addAction(IconHelper::getIcon("copy", "#2ECC71"), "复制路径", [selectedItems](){
        QStringList paths;
        for (auto* it : selectedItems) paths << it->data(Qt::UserRole).toString();
        QApplication::clipboard()->setText(paths.join("\n"));
    });

    menu.addSeparator();
    menu.addAction(IconHelper::getIcon("close", "#E74C3C"), "移除收藏", [this, selectedItems](){
        for (auto* it : selectedItems) delete it;
        saveCollection();
    });

    menu.exec(m_collectionSidebar->mapToGlobal(pos));
}

void SearchAppWindow::onMergeCollectionFiles() {
    // 转发给 FileSearchWidget 处理实际的合并逻辑，或者在这里实现
    // 由于 FileSearchWidget 已经有完善的合并逻辑，我们直接调用它的接口
    QStringList paths;
    for (int i = 0; i < m_collectionSidebar->count(); ++i) {
        paths << m_collectionSidebar->item(i)->data(Qt::UserRole).toString();
    }

    if (paths.isEmpty()) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#e74c3c;'>✖ 收藏夹为空</b>");
        return;
    }

    m_fileSearchWidget->onMergeFiles(paths, "", true);
}

void SearchAppWindow::onFavoriteCurrentPath() {
    QString path;
    if (m_tabWidget->currentIndex() == 0) path = m_fileSearchWidget->getCurrentPath();
    else path = m_keywordSearchWidget->getCurrentPath();

    if (QDir(path).exists()) addFavorite(path);
    else ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#e74c3c;'>✖ 无效路径</b>");
}

void SearchAppWindow::addFavorite(const QString& path) {
    if (path.isEmpty()) return;
    // 检查重复
    for (int i = 0; i < m_sidebar->count(); ++i) {
        if (m_sidebar->item(i)->data(Qt::UserRole).toString() == path) return;
    }

    QFileInfo fi(path);
    auto* item = new QListWidgetItem(IconHelper::getIcon("folder", "#F1C40F"), fi.fileName().isEmpty() ? path : fi.fileName());
    item->setData(Qt::UserRole, path);
    item->setToolTip(path);
    m_sidebar->addItem(item);
    saveFavorites();
}

void SearchAppWindow::addCollectionItem(const QString& path) {
    addCollectionItems({path});
}

void SearchAppWindow::addCollectionItems(const QStringList& paths) {
    bool added = false;
    for (const QString& path : paths) {
        if (!QFile::exists(path)) continue;

        bool exists = false;
        for (int i = 0; i < m_collectionSidebar->count(); ++i) {
            if (m_collectionSidebar->item(i)->data(Qt::UserRole).toString() == path) {
                exists = true; break;
            }
        }
        if (exists) continue;

        QFileInfo fi(path);
        auto* item = new QListWidgetItem(IconHelper::getIcon("file", "#2ECC71"), fi.fileName());
        item->setData(Qt::UserRole, path);
        item->setToolTip(path);
        m_collectionSidebar->addItem(item);
        added = true;
    }
    if (added) saveCollection();
}

void SearchAppWindow::loadFavorites() {
    QSettings settings("RapidNotes", "FileSearchFavorites");
    QStringList favs = settings.value("list").toStringList();
    for (const QString& p : favs) {
        if (QDir(p).exists()) {
            QFileInfo fi(p);
            auto* item = new QListWidgetItem(IconHelper::getIcon("folder", "#F1C40F"), fi.fileName().isEmpty() ? p : fi.fileName());
            item->setData(Qt::UserRole, p);
            item->setToolTip(p);
            m_sidebar->addItem(item);
        }
    }
}

void SearchAppWindow::saveFavorites() {
    QStringList favs;
    for (int i = 0; i < m_sidebar->count(); ++i) {
        favs << m_sidebar->item(i)->data(Qt::UserRole).toString();
    }
    QSettings settings("RapidNotes", "FileSearchFavorites");
    settings.setValue("list", favs);
}

void SearchAppWindow::loadCollection() {
    QSettings settings("RapidNotes", "FileSearchCollection");
    QStringList coll = settings.value("list").toStringList();
    for (const QString& p : coll) {
        if (QFile::exists(p)) {
            QFileInfo fi(p);
            auto* item = new QListWidgetItem(IconHelper::getIcon("file", "#2ECC71"), fi.fileName());
            item->setData(Qt::UserRole, p);
            item->setToolTip(p);
            m_collectionSidebar->addItem(item);
        }
    }
}

void SearchAppWindow::saveCollection() {
    QStringList coll;
    for (int i = 0; i < m_collectionSidebar->count(); ++i) {
        coll << m_collectionSidebar->item(i)->data(Qt::UserRole).toString();
    }
    QSettings settings("RapidNotes", "FileSearchCollection");
    settings.setValue("list", coll);
}

void SearchAppWindow::switchToFileSearch() {
    m_tabWidget->setCurrentIndex(0);
}

void SearchAppWindow::switchToKeywordSearch() {
    m_tabWidget->setCurrentIndex(1);
}

void SearchAppWindow::resizeEvent(QResizeEvent* event) {
    FramelessDialog::resizeEvent(event);
}

void SearchAppWindow::showEvent(QShowEvent* event) {
    FramelessDialog::showEvent(event);
}
