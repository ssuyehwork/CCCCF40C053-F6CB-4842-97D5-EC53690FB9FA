#include "SearchAppWindow.h"
#include "FileSearchWidget.h"
#include "KeywordSearchWidget.h"
#include "IconHelper.h"
#include "StringUtils.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QMenu>
#include <QDesktopServices>
#include <QUrl>
#include <QFileInfo>
#include <QDir>
#include <QApplication>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>

// ----------------------------------------------------------------------------
// Sidebar ListWidget subclass for Drag & Drop
// ----------------------------------------------------------------------------
class GlobalSidebarListWidget : public QListWidget {
    Q_OBJECT
public:
    explicit GlobalSidebarListWidget(QWidget* parent = nullptr) : QListWidget(parent) {
        setAcceptDrops(true);
    }
signals:
    void folderDropped(const QString& path);
protected:
    void dragEnterEvent(QDragEnterEvent* event) override {
        if (event->mimeData()->hasUrls() || event->mimeData()->hasText()) {
            event->acceptProposedAction();
        }
    }
    void dragMoveEvent(QDragMoveEvent* event) override {
        event->acceptProposedAction();
    }
    void dropEvent(QDropEvent* event) override {
        QString path;
        if (event->mimeData()->hasUrls()) {
            path = event->mimeData()->urls().at(0).toLocalFile();
        } else if (event->mimeData()->hasText()) {
            path = event->mimeData()->text();
        }
        
        if (!path.isEmpty() && QDir(path).exists()) {
            emit folderDropped(path);
            event->acceptProposedAction();
        }
    }
};

class GlobalFileFavoriteListWidget : public QListWidget {
    Q_OBJECT
public:
    explicit GlobalFileFavoriteListWidget(QWidget* parent = nullptr) : QListWidget(parent) {
        setAcceptDrops(true);
    }
signals:
    void filesDropped(const QStringList& paths);
protected:
    void dragEnterEvent(QDragEnterEvent* event) override {
        if (event->mimeData()->hasUrls() || event->mimeData()->hasText()) {
            event->acceptProposedAction();
        }
    }
    void dragMoveEvent(QDragMoveEvent* event) override {
        event->acceptProposedAction();
    }
    void dropEvent(QDropEvent* event) override {
        QStringList paths;
        if (event->mimeData()->hasUrls()) {
            for (const QUrl& url : event->mimeData()->urls()) {
                QString p = url.toLocalFile();
                if (!p.isEmpty()) paths << p;
            }
        } else if (event->mimeData()->hasText()) {
            paths = event->mimeData()->text().split("\n", Qt::SkipEmptyParts);
        }
        
        if (!paths.isEmpty()) {
            emit filesDropped(paths);
            event->acceptProposedAction();
        }
    }
};

class FavoriteItem : public QListWidgetItem {
public:
    using QListWidgetItem::QListWidgetItem;
    bool operator<(const QListWidgetItem &other) const override {
        bool thisPinned = data(Qt::UserRole + 1).toBool();
        bool otherPinned = other.data(Qt::UserRole + 1).toBool();
        if (thisPinned != otherPinned) return thisPinned; 
        return text().localeAwareCompare(other.text()) < 0;
    }
};

SearchAppWindow::SearchAppWindow(QWidget* parent) 
    : FramelessDialog("聚合搜索工具", parent) 
{
    setObjectName("SearchTool_SearchAppWindow_Standalone");
    resize(1200, 800);
    setupStyles();
    initUI();
    loadFolderFavorites();
    loadFileFavorites();
}

SearchAppWindow::~SearchAppWindow() {
}

void SearchAppWindow::setupStyles() {
    m_tabWidget = new QTabWidget();
    m_tabWidget->setStyleSheet(R"(
        QTabWidget::pane {
            border: 1px solid #333;
            background: #1e1e1e;
            margin-top: -1px;
            border-top-left-radius: 0px;
            border-top-right-radius: 4px;
            border-bottom-left-radius: 4px;
            border-bottom-right-radius: 4px;
        }
        QTabBar::tab {
            background: #2D2D30;
            color: #AAA;
            padding: 10px 20px;
            border: 1px solid #333;
            border-bottom: 1px solid #333;
            border-top-left-radius: 6px;
            border-top-right-radius: 6px;
            margin-right: 2px;
        }
        QTabBar::tab:hover {
            background: #3E3E42;
            color: #EEE;
        }
        QTabBar::tab:selected {
            background: #1e1e1e;
            color: #007ACC;
            border-bottom: 1px solid #1e1e1e;
            font-weight: bold;
        }
        QTabBar {
            border-bottom: 1px solid #333;
        }
    )");

    setStyleSheet(R"(
        QWidget {
            font-family: "Microsoft YaHei", "Segoe UI", sans-serif;
            font-size: 14px;
            color: #E0E0E0;
        }
        QListWidget {
            background-color: #252526; 
            border: 1px solid #333333;
            border-radius: 6px;
            padding: 4px;
        }
        QListWidget::item {
            height: 30px;
            padding-left: 8px;
            border-radius: 4px;
            color: #CCCCCC;
        }
        QListWidget::item:selected {
            background-color: #37373D;
            border-left: 3px solid #007ACC;
            color: #FFFFFF;
        }
        QListWidget::item:hover {
            background-color: #2A2D2E;
        }
        QSplitter::handle {
            background: transparent;
        }
    )");
}

void SearchAppWindow::initUI() {
    auto* mainHLayout = new QHBoxLayout(m_contentArea);
    mainHLayout->setContentsMargins(10, 5, 10, 10);
    mainHLayout->setSpacing(0);

    auto* splitter = new QSplitter(Qt::Horizontal);
    mainHLayout->addWidget(splitter);

    // --- 左侧：目录收藏 ---
    auto* leftSidebarWidget = new QWidget();
    auto* leftLayout = new QVBoxLayout(leftSidebarWidget);
    leftLayout->setContentsMargins(0, 0, 5, 0);
    leftLayout->setSpacing(10);

    auto* leftHeader = new QHBoxLayout();
    auto* leftIcon = new QLabel();
    leftIcon->setPixmap(IconHelper::getIcon("folder", "#888").pixmap(14, 14));
    leftHeader->addWidget(leftIcon);
    auto* leftTitle = new QLabel("收藏夹 (可拖入)");
    leftTitle->setStyleSheet("color: #888; font-weight: bold; font-size: 12px;");
    leftHeader->addWidget(leftTitle);
    leftHeader->addStretch();
    leftLayout->addLayout(leftHeader);

    auto* sidebar = new GlobalSidebarListWidget();
    m_folderSidebar = sidebar;
    m_folderSidebar->setMinimumWidth(180);
    m_folderSidebar->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(sidebar, SIGNAL(folderDropped(QString)), this, SLOT(addFolderFavorite(QString)));
    connect(m_folderSidebar, &QListWidget::itemClicked, this, &SearchAppWindow::onSidebarItemClicked);
    connect(m_folderSidebar, &QListWidget::customContextMenuRequested, this, &SearchAppWindow::showSidebarContextMenu);
    leftLayout->addWidget(m_folderSidebar);

    auto* btnAddFav = new QPushButton("收藏当前路径");
    btnAddFav->setFixedHeight(32);
    btnAddFav->setStyleSheet("QPushButton { background-color: #2D2D30; border: 1px solid #444; color: #AAA; border-radius: 4px; font-size: 12px; } QPushButton:hover { background-color: #3E3E42; color: #FFF; }");
    connect(btnAddFav, &QPushButton::clicked, [this](){
        QString path;
        if (m_tabWidget->currentIndex() == 0) {
            path = m_fileSearchWidget->currentPath();
        } else {
            path = m_keywordSearchWidget->currentPath();
        }
        if (!path.isEmpty() && QDir(path).exists()) {
            addFolderFavorite(path);
        }
    });
    leftLayout->addWidget(btnAddFav);
    splitter->addWidget(leftSidebarWidget);

    // --- 中间：主搜索框 ---
    m_fileSearchWidget = new FileSearchWidget();
    m_keywordSearchWidget = new KeywordSearchWidget();

    m_tabWidget->addTab(m_fileSearchWidget, IconHelper::getIcon("folder", "#AAA"), "文件查找");
    m_tabWidget->addTab(m_keywordSearchWidget, IconHelper::getIcon("find_keyword", "#AAA"), "关键字查找");
    
    connect(m_fileSearchWidget, SIGNAL(requestAddFileFavorite(QStringList)), this, SLOT(addFileFavorite(QStringList)));
    connect(m_keywordSearchWidget, SIGNAL(requestAddFileFavorite(QStringList)), this, SLOT(addFileFavorite(QStringList)));
    connect(m_fileSearchWidget, SIGNAL(requestAddFolderFavorite(QString)), this, SLOT(addFolderFavorite(QString)));
    connect(m_keywordSearchWidget, SIGNAL(requestAddFolderFavorite(QString)), this, SLOT(addFolderFavorite(QString)));

    splitter->addWidget(m_tabWidget);

    // --- 右侧：文件收藏 ---
    auto* rightSidebarWidget = new QWidget();
    auto* rightLayout = new QVBoxLayout(rightSidebarWidget);
    rightLayout->setContentsMargins(5, 0, 0, 0);
    rightLayout->setSpacing(10);

    auto* rightHeader = new QHBoxLayout();
    auto* rightIcon = new QLabel();
    rightIcon->setPixmap(IconHelper::getIcon("star", "#888").pixmap(14, 14));
    rightHeader->addWidget(rightIcon);
    auto* rightTitle = new QLabel("文件收藏");
    rightTitle->setStyleSheet("color: #888; font-weight: bold; font-size: 12px;");
    rightHeader->addWidget(rightTitle);
    rightHeader->addStretch();
    rightLayout->addLayout(rightHeader);

    auto* favList = new GlobalFileFavoriteListWidget();
    m_fileFavoritesList = favList;
    m_fileFavoritesList->setMinimumWidth(180);
    m_fileFavoritesList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_fileFavoritesList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(favList, SIGNAL(filesDropped(QStringList)), this, SLOT(addFileFavorite(QStringList)));
    connect(m_fileFavoritesList, &QListWidget::customContextMenuRequested, this, &SearchAppWindow::showFileFavoriteContextMenu);
    connect(m_fileFavoritesList, &QListWidget::itemDoubleClicked, this, &SearchAppWindow::onFileFavoriteItemDoubleClicked);
    rightLayout->addWidget(m_fileFavoritesList);

    splitter->addWidget(rightSidebarWidget);

    splitter->setStretchFactor(0, 0); // 左
    splitter->setStretchFactor(1, 1); // 中
    splitter->setStretchFactor(2, 0); // 右
}

void SearchAppWindow::onSidebarItemClicked(QListWidgetItem* item) {
    if (!item) return;
    QString path = item->data(Qt::UserRole).toString();
    
    if (m_tabWidget->currentIndex() == 0) {
        m_fileSearchWidget->setSearchPath(path);
    } else {
        m_keywordSearchWidget->setSearchPath(path);
    }
}

void SearchAppWindow::showSidebarContextMenu(const QPoint& pos) {
    QListWidgetItem* item = m_folderSidebar->itemAt(pos);
    if (!item) return;
    
    QMenu menu(this);
    menu.setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    menu.setAttribute(Qt::WA_TranslucentBackground);

    bool isPinned = item->data(Qt::UserRole + 1).toBool();
    QAction* pinAct = menu.addAction(IconHelper::getIcon("pin_vertical", isPinned ? "#007ACC" : "#AAA"), isPinned ? "取消置顶" : "置顶文件夹");
    QAction* removeAct = menu.addAction(IconHelper::getIcon("close", "#E74C3C"), "取消收藏");
    
    QAction* selected = menu.exec(m_folderSidebar->mapToGlobal(pos));
    if (selected == pinAct) {
        bool newPinned = !isPinned;
        item->setData(Qt::UserRole + 1, newPinned);
        item->setIcon(IconHelper::getIcon("folder", newPinned ? "#007ACC" : "#F1C40F"));
        m_folderSidebar->sortItems(Qt::AscendingOrder);
        saveFolderFavorites();
    } else if (selected == removeAct) {
        delete m_folderSidebar->takeItem(m_folderSidebar->row(item));
        saveFolderFavorites();
    }
}

void SearchAppWindow::addFolderFavorite(const QString& path, bool pinned) {
    for (int i = 0; i < m_folderSidebar->count(); ++i) {
        if (m_folderSidebar->item(i)->data(Qt::UserRole).toString() == path) return;
    }
    QFileInfo fi(path);
    auto* item = new FavoriteItem(IconHelper::getIcon("folder", pinned ? "#007ACC" : "#F1C40F"), fi.fileName());
    item->setData(Qt::UserRole, path);
    item->setData(Qt::UserRole + 1, pinned);
    item->setToolTip(StringUtils::wrapToolTip(path));
    m_folderSidebar->addItem(item);
    m_folderSidebar->sortItems(Qt::AscendingOrder);
    saveFolderFavorites();
}

void SearchAppWindow::addFileFavorite(const QStringList& paths) {
    QSettings settings("SearchTool_Standalone", "GlobalFileFavorites");
    QStringList favs = settings.value("list").toStringList();
    bool changed = false;
    for (const QString& path : paths) {
        if (!path.isEmpty() && !favs.contains(path)) {
            favs.prepend(path);
            changed = true;
        }
    }
    if (changed) {
        settings.setValue("list", favs);
        loadFileFavorites();
    }
}

void SearchAppWindow::onFileFavoriteItemDoubleClicked(QListWidgetItem* item) {
    QString path = item->data(Qt::UserRole).toString();
    if (!path.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void SearchAppWindow::showFileFavoriteContextMenu(const QPoint& pos) {
    auto selectedItems = m_fileFavoritesList->selectedItems();
    if (selectedItems.isEmpty()) {
        auto* item = m_fileFavoritesList->itemAt(pos);
        if (item) {
            item->setSelected(true);
            selectedItems << item;
        }
    }
    if (selectedItems.isEmpty()) return;

    QMenu menu(this);
    menu.setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    menu.setAttribute(Qt::WA_TranslucentBackground);
    
    menu.addAction(IconHelper::getIcon("close", "#E74C3C"), "取消收藏", this, SLOT(removeFileFavorite()));
    menu.exec(m_fileFavoritesList->mapToGlobal(pos));
}

void SearchAppWindow::removeFileFavorite() {
    auto items = m_fileFavoritesList->selectedItems();
    if (items.isEmpty()) return;
    QSettings settings("SearchTool_Standalone", "GlobalFileFavorites");
    QStringList favs = settings.value("list").toStringList();
    for (auto* item : items) {
        QString path = item->data(Qt::UserRole).toString();
        favs.removeAll(path);
        delete item;
    }
    settings.setValue("list", favs);
}

void SearchAppWindow::loadFolderFavorites() {
    QSettings settings("SearchTool_Standalone", "GlobalFolderFavorites");
    QVariantList favs = settings.value("list").toList();
    for (const auto& fav : favs) {
        QVariantMap map = fav.toMap();
        addFolderFavorite(map["path"].toString(), map["pinned"].toBool());
    }
}

void SearchAppWindow::saveFolderFavorites() {
    QVariantList favs;
    for (int i = 0; i < m_folderSidebar->count(); ++i) {
        QVariantMap map;
        map["path"] = m_folderSidebar->item(i)->data(Qt::UserRole).toString();
        map["pinned"] = m_folderSidebar->item(i)->data(Qt::UserRole + 1).toBool();
        favs << map;
    }
    QSettings settings("SearchTool_Standalone", "GlobalFolderFavorites");
    settings.setValue("list", favs);
}

void SearchAppWindow::loadFileFavorites() {
    m_fileFavoritesList->clear();
    QSettings settings("SearchTool_Standalone", "GlobalFileFavorites");
    QStringList favs = settings.value("list").toStringList();
    for (const QString& path : favs) {
        QFileInfo fi(path);
        auto* item = new QListWidgetItem(IconHelper::getIcon("file", "#4A90E2"), fi.fileName());
        item->setData(Qt::UserRole, path);
        item->setToolTip(StringUtils::wrapToolTip(path));
        m_fileFavoritesList->addItem(item);
    }
}

void SearchAppWindow::saveFileFavorites() {}

#include "SearchAppWindow.moc"

void SearchAppWindow::resizeEvent(QResizeEvent* event) {
    FramelessDialog::resizeEvent(event);
}
void SearchAppWindow::switchToFileSearch() { m_tabWidget->setCurrentIndex(0); }
void SearchAppWindow::switchToKeywordSearch() { m_tabWidget->setCurrentIndex(1); }
