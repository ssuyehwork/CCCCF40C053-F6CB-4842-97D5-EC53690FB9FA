#include "ToolTipOverlay.h"
#include "MainWindow.h"
#include <QDebug>
#include <QListView>
#include <QTreeView>
#include "StringUtils.h"
#include "TitleEditorDialog.h"
#include "../core/DatabaseManager.h"
#include "../core/ClipboardMonitor.h"
#include "NoteDelegate.h"
#include "CategoryDelegate.h"
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

// 2026-03-xx 按照用户要求：MainWindow.cpp 深度重构。
// 移除所有对已物理删除窗口（OCRResultWindow, NoteEditWindow 等）的包含和调用。

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent, Qt::FramelessWindowHint) {
    setWindowTitle("RapidManager");
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

    connect(&DatabaseManager::instance(), &DatabaseManager::noteAdded, this, &MainWindow::onNoteAdded);
    connect(&DatabaseManager::instance(), &DatabaseManager::noteUpdated, this, &MainWindow::scheduleRefresh);
    connect(&DatabaseManager::instance(), &DatabaseManager::categoriesChanged, this, &MainWindow::scheduleRefresh, Qt::QueuedConnection);

    connect(&DatabaseManager::instance(), &DatabaseManager::activeCategoryIdChanged, this, [this](int id){
        if (id > 0) {
            if (m_currentFilterType == "category" && m_currentFilterValue == id) return;
        } else {
            if (m_currentFilterType != "category") return;
            if (m_currentFilterValue == -1) return;
        }

        m_currentFilterType = "category";
        m_currentFilterValue = id;
        m_currentPage = 1;
        scheduleRefresh();
    });

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
    if (auto* title = m_header->findChild<QLabel*>()) {
         if (title->text() == "快速笔记") title->setText("数据管理终端");
    }

    QList<QPushButton*> buttons = m_header->findChildren<QPushButton*>();
    for (auto* btn : buttons) {
        QString tip = btn->property("tooltipText").toString();
        if (tip.contains("工具箱") || tip.contains("全局锁定")) {
            btn->hide();
        }
    }

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
        if (visible) m_filterPanel->updateStats(m_currentKeyword, m_currentFilterType, m_currentFilterValue);
    });
    connect(m_header, &HeaderBar::newNoteRequested, this, &MainWindow::doNewIdea);
    connect(m_header, &HeaderBar::toggleSidebar, this, [this](){
        m_sidebarContainer->setVisible(!m_sidebarContainer->isVisible());
        updateFocusLines();
    });

    connect(m_header, &HeaderBar::metadataToggled, this, [this](bool checked){ m_metaPanel->setVisible(checked); });
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
    m_sidebarContainer->setStyleSheet("#SidebarContainer { background-color: #1e1e1e; border: 1px solid #333333; }");

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

    auto* sidebarHeader = new QWidget();
    sidebarHeader->setFixedHeight(32);
    sidebarHeader->setStyleSheet("background-color: #252526; border-bottom: 1px solid #333;");
    auto* sidebarHeaderLayout = new QHBoxLayout(sidebarHeader);
    sidebarHeaderLayout->setContentsMargins(15, 0, 15, 0);
    auto* sbIcon = new QLabel();
    sbIcon->setPixmap(IconHelper::getIcon("category", "#3498db").pixmap(18, 18));
    sidebarHeaderLayout->addWidget(sbIcon);
    auto* sbTitle = new QLabel("数据分类");
    sbTitle->setStyleSheet("color: #3498db; font-size: 13px; font-weight: bold; background: transparent; border: none;");
    sidebarHeaderLayout->addWidget(sbTitle);
    sidebarHeaderLayout->addStretch();
    sidebarContainerLayout->addWidget(sidebarHeader);

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
    m_systemModel = new CategoryModel(CategoryModel::System, this);
    m_systemTree->setModel(m_systemModel);
    m_systemTree->setHeaderHidden(true);
    m_systemTree->setRootIsDecorated(false);
    m_systemTree->setIndentation(12);
    m_systemTree->setFixedHeight(176);
    m_systemTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_systemTree->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_systemTree->setContextMenuPolicy(Qt::CustomContextMenu);

    m_partitionTree = new DropTreeView();
    m_partitionTree->setStyleSheet(treeStyle);
    m_partitionTree->setItemDelegate(new CategoryDelegate(this));
    m_partitionModel = new CategoryModel(CategoryModel::User, this);
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

    splitter->addWidget(m_sidebarContainer);

    auto onSidebarMenu = [this](const QPoint& pos){
        auto* tree = qobject_cast<QTreeView*>(sender());
        if (!tree) return;
        QModelIndex index = tree->indexAt(pos);
        QMenu menu(this);
        IconHelper::setupMenu(&menu);
        menu.setStyleSheet("QMenu { background-color: #2D2D2D; color: #EEE; border: 1px solid #444; padding: 4px; } "
                           "QMenu::item { padding: 6px 10px 6px 10px; border-radius: 3px; } "
                           "QMenu::icon { margin-left: 6px; } "
                           "QMenu::item:selected { background-color: #3E3E42; color: white; }");

        if (!index.isValid() || index.data(CategoryModel::NameRole).toString() == "我的分类") {
            menu.addAction(IconHelper::getIcon("add", "#3498db", 18), "新建分类", [this]() {
                FramelessInputDialog dlg("新建分类", "组名称:", "", this);
                if (dlg.exec() == QDialog::Accepted) {
                    QString text = dlg.text();
                    if (!text.isEmpty()) { DatabaseManager::instance().addCategory(text); refreshData(); }
                }
            });
            menu.exec(tree->mapToGlobal(pos));
            return;
        }

        QString type = index.data(CategoryModel::TypeRole).toString();
        if (type == "category") {
            int catId = index.data(CategoryModel::IdRole).toInt();
            menu.addAction(IconHelper::getIcon("add", "#3498db", 18), "新建数据", [this, catId]() {
                int newId = DatabaseManager::instance().addNote("新记录", "", {}, "", catId);
                if (newId > 0) {
                    refreshData();
                    for (int i = 0; i < m_noteModel->rowCount(); ++i) {
                        QModelIndex idx = m_noteModel->index(i, 0);
                        if (idx.data(NoteModel::IdRole).toInt() == newId) {
                            m_noteList->setCurrentIndex(idx); m_editor->setFocus(); break;
                        }
                    }
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
            m_partitionTree->selectionModel()->clearSelection(); m_partitionTree->setCurrentIndex(QModelIndex());
        } else {
            m_systemTree->selectionModel()->clearSelection(); m_systemTree->setCurrentIndex(QModelIndex());
        }
        onTagSelected(index);
    };

    connect(m_systemTree, &QTreeView::clicked, this, [this, onSelection](const QModelIndex& idx){ onSelection(m_systemTree, idx); });
    connect(m_partitionTree, &QTreeView::clicked, this, [this, onSelection](const QModelIndex& idx){ onSelection(m_partitionTree, idx); });
    
    auto onNotesDropped = [this](const QList<int>& ids, const QModelIndex& targetIndex){
        if (!targetIndex.isValid()) return;
        QString type = targetIndex.data(CategoryModel::TypeRole).toString();
        for (int id : ids) {
            if (type == "category") {
                int catId = targetIndex.data(CategoryModel::IdRole).toInt();
                DatabaseManager::instance().updateNoteState(id, "category_id", catId);
            } else if (type == "trash") {
                DatabaseManager::instance().updateNoteState(id, "is_deleted", 1);
            }
        }
        refreshData();
    };

    connect(m_systemTree, &DropTreeView::notesDropped, this, onNotesDropped);
    connect(m_partitionTree, &DropTreeView::notesDropped, this, onNotesDropped);

    auto* listContainer = new QFrame();
    listContainer->setMinimumWidth(230);
    listContainer->setObjectName("ListContainer");
    listContainer->setAttribute(Qt::WA_StyledBackground, true);
    listContainer->setStyleSheet("#ListContainer { background-color: #1e1e1e; border: 1px solid #333333; }");

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

    auto* listHeader = new QWidget();
    listHeader->setFixedHeight(32);
    listHeader->setStyleSheet("background-color: #252526; border-bottom: 1px solid #333;");
    auto* listHeaderLayout = new QHBoxLayout(listHeader);
    listHeaderLayout->setContentsMargins(15, 0, 15, 0); 
    auto* listIcon = new QLabel();
    listIcon->setPixmap(IconHelper::getIcon("list_ul", "#2ecc71").pixmap(18, 18));
    listHeaderLayout->addWidget(listIcon);
    auto* listHeaderTitle = new QLabel("笔记列表");
    listHeaderTitle->setStyleSheet("color: #2ecc71; font-size: 13px; font-weight: bold; background: transparent; border: none;");
    listHeaderLayout->addWidget(listHeaderTitle);
    listHeaderLayout->addStretch();
    listContainerLayout->addWidget(listHeader);

    auto* listContent = new QWidget();
    listContent->setAttribute(Qt::WA_StyledBackground, true);
    listContent->setStyleSheet("background: transparent; border: none;");
    auto* listContentLayout = new QVBoxLayout(listContent);
    listContentLayout->setContentsMargins(15, 8, 15, 8);
    
    m_noteList = new CleanListView();
    m_noteList->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_noteModel = new NoteModel(this);
    m_noteList->setModel(m_noteModel);
    m_noteList->setItemDelegate(new NoteDelegate(m_noteList));
    m_noteList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_noteList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    connect(m_noteList, &QListView::customContextMenuRequested, this, &MainWindow::showContextMenu);
    
    m_noteList->setSpacing(5); 
    m_noteList->setStyleSheet("QListView { background: transparent; border: none; padding-top: 5px; }");
    
    m_noteList->setDragEnabled(true); m_noteList->setAcceptDrops(true);
    
    connect(m_noteList->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::onSelectionChanged);
    connect(m_noteList, &QListView::doubleClicked, this, [this](const QModelIndex& index){ if (index.isValid()) m_editor->setFocus(); });

    listContentLayout->addWidget(m_noteList);

    m_lockWidget = new CategoryLockWidget(this);
    m_lockWidget->setVisible(false);
    connect(m_lockWidget, &CategoryLockWidget::unlocked, this, [this](){ refreshData(); });
    listContentLayout->addWidget(m_lockWidget);

    listContainerLayout->addWidget(listContent);
    splitter->addWidget(listContainer);
    
    auto* editorContainer = new QFrame();
    editorContainer->setMinimumWidth(230);
    editorContainer->setObjectName("EditorContainer");
    editorContainer->setAttribute(Qt::WA_StyledBackground, true);
    editorContainer->setStyleSheet("#EditorContainer { background-color: #1e1e1e; border: 1px solid #333333; }");

    auto* editorShadow = new QGraphicsDropShadowEffect(editorContainer);
    editorShadow->setBlurRadius(10);
    editorShadow->setXOffset(0);
    editorShadow->setYOffset(4);
    editorShadow->setColor(QColor(0, 0, 0, 150));
    editorContainer->setGraphicsEffect(editorShadow);

    auto* editorContainerLayout = new QVBoxLayout(editorContainer);
    editorContainerLayout->setContentsMargins(0, 0, 0, 0);
    editorContainerLayout->setSpacing(0);

    auto* editorHeader = new QWidget();
    editorHeader->setFixedHeight(32);
    editorHeader->setStyleSheet("background-color: #252526; border-bottom: 1px solid #333;");
    auto* editorHeaderLayout = new QHBoxLayout(editorHeader);
    editorHeaderLayout->setContentsMargins(15, 2, 15, 0);
    auto* edIcon = new QLabel();
    edIcon->setPixmap(IconHelper::getIcon("eye", "#41F2F2").pixmap(18, 18));
    editorHeaderLayout->addWidget(edIcon);
    auto* edTitle = new QLabel("预览与编辑");
    edTitle->setStyleSheet("color: #41F2F2; font-size: 13px; font-weight: bold; background: transparent; border: none;");
    editorHeaderLayout->addWidget(edTitle);
    editorHeaderLayout->addStretch();

    m_editBtn = new QPushButton();
    m_editBtn->setFixedSize(24, 24);
    m_editBtn->setEnabled(false);
    m_editBtn->setProperty("tooltipText", "保存修改 (Ctrl+S)"); m_editBtn->installEventFilter(this);
    m_editBtn->setIcon(IconHelper::getIcon("save", "#555555"));
    m_editBtn->setStyleSheet("QPushButton { background: transparent; border: none; }");
    connect(m_editBtn, &QPushButton::clicked, this, [this](){
        QModelIndex index = m_noteList->currentIndex();
        if (!index.isValid()) return;
        int id = index.data(NoteModel::IdRole).toInt();
        if (DatabaseManager::instance().updateNoteState(id, "content", m_editor->getOptimizedContent())) {
            ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #2ecc71;'>[OK] 内容已保存</b>", 700);
            refreshData();
        }
    });
    editorHeaderLayout->addWidget(m_editBtn);
    editorContainerLayout->addWidget(editorHeader);

    auto* editorContent = new QWidget();
    editorContent->setAttribute(Qt::WA_StyledBackground, true);
    editorContent->setStyleSheet("background: transparent; border: none;");
    auto* editorContentLayout = new QVBoxLayout(editorContent);
    editorContentLayout->setContentsMargins(2, 2, 2, 2);

    m_editor = new Editor();
    m_editor->togglePreview(false); m_editor->setReadOnly(false);
    
    editorContentLayout->addWidget(m_editor);
    editorContainerLayout->addWidget(editorContent);
    splitter->addWidget(editorContainer);

    m_metaPanel = new MetadataPanel(this);
    m_metaPanel->setMinimumWidth(230);
    connect(m_metaPanel, &MetadataPanel::noteUpdated, this, &MainWindow::refreshData);
    connect(m_metaPanel, &MetadataPanel::closed, this, [this](){ m_header->setMetadataActive(false); });
    splitter->addWidget(m_metaPanel);
    
    m_filterWrapper = new QFrame();
    m_filterWrapper->setMinimumWidth(230);
    m_filterWrapper->setObjectName("FilterContainer");
    m_filterWrapper->setStyleSheet("#FilterContainer { background-color: #1e1e1e; border: 1px solid #333333; }");
    auto* fwLayout = new QVBoxLayout(m_filterWrapper);
    fwLayout->setContentsMargins(0, 0, 0, 0);
    m_filterPanel = new FilterPanel(this);
    connect(m_filterPanel, &FilterPanel::filterChanged, this, &MainWindow::refreshData);
    fwLayout->addWidget(m_filterPanel);
    splitter->addWidget(m_filterWrapper);

    splitter->setStretchFactor(0, 1); splitter->setStretchFactor(1, 2);
    splitter->setStretchFactor(2, 8); splitter->setStretchFactor(3, 1);
    splitter->setStretchFactor(4, 1);
    splitter->setSizes({230, 230, 600, 230, 230});

    contentLayout->addWidget(splitter);
    mainLayout->addWidget(contentWidget);

    m_systemTree->installEventFilter(this); m_partitionTree->installEventFilter(this); m_noteList->installEventFilter(this);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls() || event->mimeData()->hasText() || event->mimeData()->hasImage()) event->acceptProposedAction();
}
void MainWindow::dragMoveEvent(QDragMoveEvent* event) { event->acceptProposedAction(); }
void MainWindow::dropEvent(QDropEvent* event) {
    const QMimeData* mime = event->mimeData();
    int targetId = getCurrentCategoryId();
    QStringList localPaths = StringUtils::extractLocalPathsFromMime(mime);
    if (!localPaths.isEmpty()) { FileStorageHelper::processImport(localPaths, targetId); event->acceptProposedAction(); return; }
    if (mime->hasText()) { DatabaseManager::instance().addNote(mime->text().trimmed().left(50), mime->text(), {}, "", targetId, "text"); event->acceptProposedAction(); }
}

bool MainWindow::event(QEvent* event) { return QMainWindow::event(event); }
void MainWindow::showEvent(QShowEvent* event) { QMainWindow::showEvent(event); if (m_noteList) m_noteList->setFocus(); }

#ifdef Q_OS_WIN
bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result) {
    MSG* msg = static_cast<MSG*>(message);
    if (msg->message == WM_NCHITTEST) {
        int x = GET_X_LPARAM(msg->lParam); int y = GET_Y_LPARAM(msg->lParam);
        QPoint pos = mapFromGlobal(QPoint(x, y)); int m = RESIZE_MARGIN; int w = width(); int h = height();
        bool L = pos.x() < m; bool R = pos.x() > w - m; bool T = pos.y() < m; bool B = pos.y() > h - m;
        if (T && L) *result = HTTOPLEFT; else if (T && R) *result = HTTOPRIGHT; else if (B && L) *result = HTBOTTOMLEFT; else if (B && R) *result = HTBOTTOMRIGHT;
        else if (T) *result = HTTOP; else if (B) *result = HTBOTTOM; else if (L) *result = HTLEFT; else if (R) *result = HTRIGHT;
        else return QMainWindow::nativeEvent(eventType, message, result);
        return true;
    }
    return QMainWindow::nativeEvent(eventType, message, result);
}
#endif

void MainWindow::onNoteAdded(const QVariantMap& note) { scheduleRefresh(); }
void MainWindow::scheduleRefresh() { m_refreshTimer->start(); }
void MainWindow::refreshData() {
    auto notes = DatabaseManager::instance().searchNotes(m_currentKeyword, m_currentFilterType, m_currentFilterValue, m_currentPage, m_pageSize, m_filterPanel->getCheckedCriteria());
    int totalCount = DatabaseManager::instance().getNotesCount(m_currentKeyword, m_currentFilterType, m_currentFilterValue, m_filterPanel->getCheckedCriteria());
    m_noteList->setVisible(true); m_noteModel->setNotes(notes);
    m_systemModel->refresh(); m_partitionModel->refresh();
    m_header->updatePagination(m_currentPage, (totalCount + m_pageSize - 1) / m_pageSize);
}

void MainWindow::onSelectionChanged(const QItemSelection& selected, const QItemSelection& deselected) {
    QModelIndexList indices = m_noteList->selectionModel()->selectedIndexes();
    if (indices.size() == 1) {
        int id = indices.first().data(NoteModel::IdRole).toInt();
        QVariantMap note = DatabaseManager::instance().getNoteById(id);
        m_editor->setNote(note, false); m_metaPanel->setNote(note);
        m_editBtn->setEnabled(true); m_editBtn->setIcon(IconHelper::getIcon("save", "#2ecc71"));
    } else {
        m_editor->setPlainText(""); m_metaPanel->clearSelection(); m_editBtn->setEnabled(false);
    }
}

void MainWindow::setupShortcuts() {
    auto add = [&](const QString& id, std::function<void()> func) { new QShortcut(ShortcutManager::instance().getShortcut(id), this, func); };
    add("mw_refresh", [this](){ refreshData(); }); add("mw_new", [this](){ doNewIdea(); }); add("mw_edit", [this](){ m_editBtn->click(); });
}

void MainWindow::updateShortcuts() {}
void MainWindow::updateFocusLines() {
    QWidget* focus = QApplication::focusWidget();
    if (m_listFocusLine) m_listFocusLine->setVisible(focus == m_noteList);
    if (m_sidebarFocusLine) m_sidebarFocusLine->setVisible(focus == m_systemTree || focus == m_partitionTree);
}
bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut) updateFocusLines();
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::keyPressEvent(QKeyEvent* event) { QMainWindow::keyPressEvent(event); }
void MainWindow::closeEvent(QCloseEvent* event) { saveLayout(); QMainWindow::closeEvent(event); }
void MainWindow::saveLayout() {}
void MainWindow::restoreLayout() {}

void MainWindow::onTagSelected(const QModelIndex& index) {
    m_currentFilterType = index.data(CategoryModel::TypeRole).toString();
    m_currentFilterValue = (m_currentFilterType == "category") ? index.data(CategoryModel::IdRole) : -1;
    m_currentPage = 1; refreshData();
}

void MainWindow::showContextMenu(const QPoint& pos) {
    auto selected = m_noteList->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;
    QMenu menu(this); IconHelper::setupMenu(&menu);
    menu.addAction(IconHelper::getIcon("copy", "#1abc9c", 18), "复制内容", this, &MainWindow::doExtractContent);
    menu.addAction(IconHelper::getIcon("edit", "#4a90e2", 18), "编辑 (F2)", [this](){ m_editor->setFocus(); });
    menu.addSeparator();
    menu.addAction(IconHelper::getIcon("trash", "#e74c3c", 18), "移至回收站", [this](){ doDeleteSelected(false); });
    menu.exec(QCursor::pos());
}

void MainWindow::doPreview() {}
void MainWindow::updatePreviewContent() {}
void MainWindow::doDeleteSelected(bool physical) {
    auto selected = m_noteList->selectionModel()->selectedIndexes(); if (selected.isEmpty()) return;
    QList<int> ids; for (const auto& idx : selected) ids << idx.data(NoteModel::IdRole).toInt();
    if (physical) DatabaseManager::instance().deleteNotesBatch(ids); else DatabaseManager::instance().softDeleteNotes(ids);
    refreshData();
}
void MainWindow::doToggleFavorite() {}
void MainWindow::doTogglePin() {}
void MainWindow::doNewIdea() {
    int newId = DatabaseManager::instance().addNote("新记录", "", {}, "", getCurrentCategoryId());
    if (newId > 0) {
        refreshData();
        for (int i = 0; i < m_noteModel->rowCount(); ++i) {
            QModelIndex idx = m_noteModel->index(i, 0);
            if (idx.data(NoteModel::IdRole).toInt() == newId) { m_noteList->setCurrentIndex(idx); m_editor->setFocus(); break; }
        }
    }
}
void MainWindow::doCreateByLine(bool fromClipboard) {}
void MainWindow::doOCR() {
    QModelIndex index = m_noteList->currentIndex(); if (!index.isValid()) return;
    int id = index.data(NoteModel::IdRole).toInt(); QVariantMap note = DatabaseManager::instance().getNoteById(id);
    if (note.value("item_type").toString() != "image") return;
    QImage img; img.loadFromData(note.value("data_blob").toByteArray()); if (img.isNull()) return;
    connect(&OCRManager::instance(), &OCRManager::recognitionFinished, this, [this, id](const QString& text, int noteId){
        if (id == noteId) { m_editor->setPlainText(m_editor->toPlainText() + "\n\n[OCR]:\n" + text); m_editBtn->click(); }
    }, Qt::UniqueConnection);
    OCRManager::instance().recognizeAsync(img, id);
}
void MainWindow::doExtractContent() {
    auto selected = m_noteList->selectionModel()->selectedIndexes(); if (selected.isEmpty()) return;
    QList<QVariantMap> notes; for (const auto& idx : selected) notes << DatabaseManager::instance().getNoteById(idx.data(NoteModel::IdRole).toInt());
    StringUtils::copyNotesToClipboard(notes);
}
void MainWindow::doEditSelected() { m_editBtn->click(); }
void MainWindow::doSetRating(int rating) {}
void MainWindow::doMoveToCategory(int catId) {}
void MainWindow::doMoveNote(DatabaseManager::MoveDirection dir) {}
void MainWindow::doCopyTags() {}
void MainWindow::doPasteTags() {}
void MainWindow::doRepeatAction() {}
void MainWindow::doImportCategory(int catId) {}
void MainWindow::doImportFolder(int catId) {}
void MainWindow::doExportCategory(int catId, const QString& catName) {}
bool MainWindow::verifyExportPermission() { return true; }
void MainWindow::updateToolboxStatus(bool active) {}
