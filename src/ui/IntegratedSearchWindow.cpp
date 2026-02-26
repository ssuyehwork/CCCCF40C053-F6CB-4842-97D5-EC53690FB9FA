#include "IntegratedSearchWindow.h"
#include "FileSearchWindow.h"
#include "KeywordSearchWindow.h"
#include "IconHelper.h"
#include "ToolTipOverlay.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTabBar>
#include <QLabel>
#include <QPushButton>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QMenu>
#include <QFileDialog>
#include <QDesktopServices>
#include <QProcess>
#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QDateTime>
#include <QDirIterator>
#include <QUrl>

IntegratedSearchWindow::IntegratedSearchWindow(QWidget* parent)
    : FramelessDialog("搜索中心", parent)
{
    setObjectName("IntegratedSearchWindow");
    loadWindowSettings();
    resize(1250, 800);
    setupStyles();
    initUI();
    loadFavorites();
    loadCollection();
    m_resizeHandle = new ResizeHandle(this, this);
    m_resizeHandle->raise();
}

IntegratedSearchWindow::~IntegratedSearchWindow() {}

void IntegratedSearchWindow::setupStyles()
{
    setStyleSheet(R"(
        #IntegratedSearchWindow { background-color: #1E1E1E; }
        QTabWidget::pane { border: none; background-color: #1E1E1E; }
        QTabBar::tab {
            background-color: transparent;
            color: #888888;
            padding: 12px 24px;
            margin-right: 15px;
            font-size: 14px;
            border-bottom: 3px solid transparent;
        }
        QTabBar::tab:selected {
            color: #007ACC;
            border-bottom: 3px solid #007ACC;
            font-weight: bold;
        }
        QTabBar::tab:hover { color: #CCCCCC; }
        QListWidget {
            background-color: #252526;
            border: 1px solid #333;
            border-radius: 6px;
            padding: 4px;
            color: #CCC;
        }
        QListWidget::item { min-height: 28px; padding-left: 10px; border-radius: 4px; }
        QListWidget::item:selected { background-color: #37373D; border-left: 4px solid #007ACC; color: #FFF; }
        #SideButton {
            background-color: #2D2D30; border: 1px solid #444; color: #AAA; border-radius: 4px; font-size: 12px;
        }
        #SideButton:hover { background-color: #3E3E42; color: #FFF; border-color: #666; }
        QLabel { color: #888; font-weight: bold; font-size: 12px; }
    )");
}

void IntegratedSearchWindow::initUI()
{
    auto* mainLayout = new QHBoxLayout(m_contentArea);
    mainLayout->setContentsMargins(15, 15, 15, 15);
    mainLayout->setSpacing(0);

    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->setHandleWidth(1);
    mainLayout->addWidget(splitter);

    // 左侧: 收藏夹
    auto* leftSide = new QWidget();
    auto* leftLayout = new QVBoxLayout(leftSide);
    leftLayout->setContentsMargins(0, 0, 15, 0);
    auto* leftHeader = new QHBoxLayout();
    auto* leftIcon = new QLabel(); leftIcon->setPixmap(IconHelper::getIcon("folder", "#007ACC").pixmap(16, 16));
    leftHeader->addWidget(leftIcon);
    leftHeader->addWidget(new QLabel("收藏夹 (可拖入)"));
    leftHeader->addStretch();
    leftLayout->addLayout(leftHeader);

    m_sidebar = new QListWidget();
    m_sidebar->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_sidebar, &QListWidget::itemClicked, this, &IntegratedSearchWindow::onSidebarItemClicked);
    connect(m_sidebar, &QListWidget::customContextMenuRequested, this, &IntegratedSearchWindow::showSidebarContextMenu);
    leftLayout->addWidget(m_sidebar);

    auto* btnAddFav = new QPushButton("收藏当前路径"); btnAddFav->setObjectName("SideButton"); btnAddFav->setFixedHeight(34);
    connect(btnAddFav, &QPushButton::clicked, this, [this](){
        QString p = m_fileSearchWidget->currentPath();
        if(QDir(p).exists()) addFavorite(p);
        else ToolTipOverlay::instance()->showText(QCursor::pos(), "✖ 路径无效");
    });
    leftLayout->addWidget(btnAddFav);
    splitter->addWidget(leftSide);

    // 中间: 搜索主区
    auto* center = new QWidget();
    auto* centerLayout = new QVBoxLayout(center);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    m_tabWidget = new QTabWidget();
    m_tabWidget->setDocumentMode(true);
    m_fileSearchWidget = new FileSearchWidget();
    m_keywordSearchWidget = new KeywordSearchWidget();
    m_tabWidget->addTab(m_fileSearchWidget, IconHelper::getIcon("folder", "#007ACC", 18), "文件查找");
    m_tabWidget->addTab(m_keywordSearchWidget, IconHelper::getIcon("find_keyword", "#007ACC", 18), "关键字查找");
    centerLayout->addWidget(m_tabWidget);
    splitter->addWidget(center);

    // 针对集成窗口精调置顶按钮样式
    m_btnPin->setStyleSheet("QPushButton { border: none; background: transparent; border-radius: 4px; } "
                            "QPushButton:hover { background-color: rgba(0, 122, 204, 0.1); } "
                            "QPushButton:checked { background-color: transparent; }");
    connect(m_btnPin, &QPushButton::toggled, this, [this](bool checked){
        m_btnPin->setIcon(IconHelper::getIcon(checked ? "pin_vertical" : "pin_tilted", checked ? "#007ACC" : "#aaaaaa"));
    });

    // 右侧: 文件收藏
    auto* rightSide = new QWidget();
    auto* rightLayout = new QVBoxLayout(rightSide);
    rightLayout->setContentsMargins(15, 0, 0, 0);
    auto* rightHeader = new QHBoxLayout();
    auto* rightIcon = new QLabel(); rightIcon->setPixmap(IconHelper::getIcon("file", "#007ACC").pixmap(16, 16));
    rightHeader->addWidget(rightIcon);
    rightHeader->addWidget(new QLabel("文件收藏 (可多选/拖入)"));
    rightHeader->addStretch();
    rightLayout->addLayout(rightHeader);

    m_collectionSidebar = new QListWidget();
    m_collectionSidebar->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_collectionSidebar->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_collectionSidebar, &QListWidget::customContextMenuRequested, this, &IntegratedSearchWindow::showCollectionContextMenu);
    rightLayout->addWidget(m_collectionSidebar);

    auto* btnMerge = new QPushButton("合并收藏内容"); btnMerge->setObjectName("SideButton"); btnMerge->setFixedHeight(34);
    connect(btnMerge, &QPushButton::clicked, this, &IntegratedSearchWindow::onMergeCollectionFiles);
    rightLayout->addWidget(btnMerge);
    splitter->addWidget(rightSide);

    splitter->setStretchFactor(0, 0); splitter->setStretchFactor(1, 1); splitter->setStretchFactor(2, 0);
}

void IntegratedSearchWindow::setCurrentTab(SearchType type) { m_tabWidget->setCurrentIndex(static_cast<int>(type)); }
void IntegratedSearchWindow::resizeEvent(QResizeEvent* e) { FramelessDialog::resizeEvent(e); if(m_resizeHandle) m_resizeHandle->move(width()-20, height()-20); }

void IntegratedSearchWindow::onSidebarItemClicked(QListWidgetItem* item) {
    if (!item) return;
    QString p = item->data(Qt::UserRole).toString();
    m_fileSearchWidget->setPath(p); m_keywordSearchWidget->setPath(p);
}

void IntegratedSearchWindow::showSidebarContextMenu(const QPoint& pos) {
    QListWidgetItem* item = m_sidebar->itemAt(pos); if(!item) return;
    QMenu menu(this);
    menu.addAction(IconHelper::getIcon("pin", "#F1C40F"), "置顶文件夹", [this, item](){
        int row = m_sidebar->row(item); if (row > 0) { QListWidgetItem* taken = m_sidebar->takeItem(row); m_sidebar->insertItem(0, taken); saveFavorites(); }
    });
    menu.addAction(IconHelper::getIcon("close", "#E74C3C"), "取消收藏", [this, item](){ delete m_sidebar->takeItem(m_sidebar->row(item)); saveFavorites(); });
    menu.exec(m_sidebar->mapToGlobal(pos));
}

void IntegratedSearchWindow::addFavorite(const QString& p) {
    for (int i = 0; i < m_sidebar->count(); ++i) if (m_sidebar->item(i)->data(Qt::UserRole).toString() == p) return;
    auto* item = new QListWidgetItem(IconHelper::getIcon("folder", "#F1C40F"), QFileInfo(p).fileName().isEmpty() ? p : QFileInfo(p).fileName());
    item->setData(Qt::UserRole, p); item->setToolTip(p); m_sidebar->addItem(item); saveFavorites();
}

void IntegratedSearchWindow::loadFavorites() {
    QSettings s("RapidNotes", "SearchFavorites"); QStringList favs = s.value("list").toStringList();
    for(const auto& p : favs) if(QDir(p).exists()) addFavorite(p);
}

void IntegratedSearchWindow::saveFavorites() {
    QStringList favs; for(int i=0; i<m_sidebar->count(); ++i) favs << m_sidebar->item(i)->data(Qt::UserRole).toString();
    QSettings s("RapidNotes", "SearchFavorites"); s.setValue("list", favs);
}

void IntegratedSearchWindow::onCollectionItemClicked(QListWidgetItem*) {}

void IntegratedSearchWindow::showCollectionContextMenu(const QPoint& pos) {
    auto selectedItems = m_collectionSidebar->selectedItems(); if (selectedItems.isEmpty()) return;
    QMenu menu(this);
    menu.addAction(IconHelper::getIcon("merge", "#3498DB"), "合并选中内容", [this](){
        QStringList paths; for (auto* item : m_collectionSidebar->selectedItems()) paths << item->data(Qt::UserRole).toString();
        onMergeFiles(paths, "", true);
    });
    menu.addAction(IconHelper::getIcon("close", "#E74C3C"), "取消收藏", [this](){
        for (auto* item : m_collectionSidebar->selectedItems()) delete item; saveCollection();
    });
    menu.exec(m_collectionSidebar->mapToGlobal(pos));
}

void IntegratedSearchWindow::addCollectionItem(const QString& p) {
    for (int i = 0; i < m_collectionSidebar->count(); ++i) if (m_collectionSidebar->item(i)->data(Qt::UserRole).toString() == p) return;
    auto* item = new QListWidgetItem(IconHelper::getIcon("file", "#2ECC71"), QFileInfo(p).fileName());
    item->setData(Qt::UserRole, p); item->setToolTip(p); m_collectionSidebar->addItem(item); saveCollection();
}

void IntegratedSearchWindow::loadCollection() {
    QSettings s("RapidNotes", "SearchCollection"); QStringList coll = s.value("list").toStringList();
    for(const auto& p : coll) if(QFile::exists(p)) addCollectionItem(p);
}

void IntegratedSearchWindow::saveCollection() {
    QStringList coll; for(int i=0; i<m_collectionSidebar->count(); ++i) coll << m_collectionSidebar->item(i)->data(Qt::UserRole).toString();
    QSettings s("RapidNotes", "SearchCollection"); s.setValue("list", coll);
}

void IntegratedSearchWindow::onMergeCollectionFiles() {
    QStringList paths; for (int i = 0; i < m_collectionSidebar->count(); ++i) paths << m_collectionSidebar->item(i)->data(Qt::UserRole).toString();
    if (!paths.isEmpty()) onMergeFiles(paths, "", true);
}

void IntegratedSearchWindow::onMergeFiles(const QStringList& filePaths, const QString& rootPath, bool useCombineDir) {
    if (filePaths.isEmpty()) return;
    QString targetDir = rootPath;
    if (useCombineDir) {
        targetDir = QCoreApplication::applicationDirPath() + "/Combine";
        QDir().mkpath(targetDir);
    }
    QString ts = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString outPath = QDir(targetDir).filePath(ts + "_export.md");
    QFile outFile(outPath);
    if (outFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&outFile);
        out << "# Exported Files - " << ts << "\n\n";
        for (const auto& fp : filePaths) {
            out << "## File: " << fp << "\n\n```\n";
            QFile f(fp); if (f.open(QIODevice::ReadOnly)) out << f.readAll();
            out << "\n```\n\n";
        }
        ToolTipOverlay::instance()->showText(QCursor::pos(), "✔ 已保存至 Combine 目录");
    }
}

#include "IntegratedSearchWindow.moc"
