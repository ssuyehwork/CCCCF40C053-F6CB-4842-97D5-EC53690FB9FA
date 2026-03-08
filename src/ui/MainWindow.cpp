#include "ToolTipOverlay.h"
#include "MainWindow.h"
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
#include <QToolTip>
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
#include <QToolTip>
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
#include "NoteEditWindow.h"
#include "StringUtils.h"
#include "../core/FileStorageHelper.h"
#include "FramelessDialog.h"
#include "CategoryPasswordDialog.h"
#include "SettingsWindow.h"
#include "OCRResultWindow.h"
#include "../core/ShortcutManager.h"
#include "../core/OCRManager.h"
#include <functional>
#include <QVariant>
#include <QtGlobal>

#ifdef Q_OS_WIN
#include <windows.h>
#include <windowsx.h>
#define RESIZE_MARGIN 10
#endif

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent, Qt::FramelessWindowHint) {
    setWindowTitle("RapidNotes");
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

    // 【关键修改】区分两种信号
    // 1. 增量更新：添加新笔记时不刷新全表
    connect(&DatabaseManager::instance(), &DatabaseManager::noteAdded, this, &MainWindow::onNoteAdded);
    
    // 2. 全量刷新：修改、删除、分类变化（锁定状态）时才刷新全表 (通过 scheduleRefresh 节流)
    connect(&DatabaseManager::instance(), &DatabaseManager::noteUpdated, this, &MainWindow::scheduleRefresh);
    connect(&DatabaseManager::instance(), &DatabaseManager::categoriesChanged, this, &MainWindow::scheduleRefresh, Qt::QueuedConnection);

    connect(&DatabaseManager::instance(), &DatabaseManager::activeCategoryIdChanged, this, [this](int id){
        // [CRITICAL] 核心修复：只有当外部（如极速窗口）强制切换到一个具体的有效分类 (>0) 时，
        // 或者当前确实处于分类模式且需要同步为“取消选中”(-1) 时，才执行状态转换。
        // 这能有效防止点击“今日数据”、“全部数据”等系统项时，被此信号误杀回“未分类”状态。
        if (id > 0) {
            if (m_currentFilterType == "category" && m_currentFilterValue == id) return;
        } else {
            // id == -1 的情况
            if (m_currentFilterType != "category") return; // 当前已是系统模式（如今日、全部），无需处理
            if (m_currentFilterValue == -1) return; // 已经是未分类模式，无需重复刷新
        }

        m_currentFilterType = "category";
        m_currentFilterValue = id;
        m_currentPage = 1;
        scheduleRefresh();
    });

    restoreLayout(); // 恢复布局
    setupShortcuts();
    connect(&ShortcutManager::instance(), &ShortcutManager::shortcutsChanged, this, &MainWindow::updateShortcuts);
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
        if (visible) {
            m_filterPanel->updateStats(m_currentKeyword, m_currentFilterType, m_currentFilterValue);
        }
    });
    connect(m_header, &HeaderBar::newNoteRequested, this, [this](){
        NoteEditWindow* win = new NoteEditWindow();
        connect(win, &NoteEditWindow::noteSaved, this, &MainWindow::refreshData);
        win->show();
    });
    connect(m_header, &HeaderBar::toggleSidebar, this, [this](){
        m_sidebarContainer->setVisible(!m_sidebarContainer->isVisible());
    });
    connect(m_header, &HeaderBar::toolboxRequested, this, &MainWindow::toolboxRequested);
    connect(m_header, &HeaderBar::toolboxContextMenuRequested, this, &MainWindow::showToolboxMenu);
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
        "  border-top-left-radius: 12px;"
        "  border-top-right-radius: 12px;"
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

    // 侧边栏标题栏 (全宽下划线方案)
    auto* sidebarHeader = new QWidget();
    sidebarHeader->setFixedHeight(32);
    sidebarHeader->setStyleSheet(
        "background-color: #252526; "
        "border-top-left-radius: 12px; "
        "border-top-right-radius: 12px; "
        "border-bottom: 1px solid #333;"
    );
    auto* sidebarHeaderLayout = new QHBoxLayout(sidebarHeader);
    sidebarHeaderLayout->setContentsMargins(15, 0, 15, 0);
    auto* sbIcon = new QLabel();
    sbIcon->setPixmap(IconHelper::getIcon("category", "#3498db").pixmap(18, 18));
    sidebarHeaderLayout->addWidget(sbIcon);
    auto* sbTitle = new QLabel("数据分类");
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
    m_systemModel = new CategoryModel(CategoryModel::System, this);
    m_systemTree->setModel(m_systemModel);
    m_systemTree->setHeaderHidden(true);
    m_systemTree->setRootIsDecorated(false);
    m_systemTree->setIndentation(12);
    m_systemTree->setFixedHeight(176); // 8 items * 22px = 176px
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
                           "QMenu::item:selected { background-color: #4a90e2; color: white; }");

        // [CRITICAL] 锁定：基于文本“我的分区”判定右键弹出逻辑，支持新建分组
        if (!index.isValid() || index.data(Qt::DisplayRole).toString() == "我的分区") {
            menu.addAction(IconHelper::getIcon("add", "#3498db", 18), "新建分组", [this]() {
                FramelessInputDialog dlg("新建分组", "组名称:", "", this);
                if (dlg.exec() == QDialog::Accepted) {
                    QString text = dlg.text();
                    if (!text.isEmpty()) {
                        DatabaseManager::instance().addCategory(text);
                        refreshData();
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

        QString type = index.data(CategoryModel::TypeRole).toString();
        if (type == "category") {
            int catId = index.data(CategoryModel::IdRole).toInt();
            QString currentName = index.data(CategoryModel::NameRole).toString();

            menu.addAction(IconHelper::getIcon("add", "#3498db", 18), "新建数据", [this, catId]() {
                auto* win = new NoteEditWindow();
                win->setDefaultCategory(catId);
                connect(win, &NoteEditWindow::noteSaved, this, &MainWindow::refreshData);
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
                        refreshData();
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
            });
            menu.addAction(IconHelper::getIcon("tag", "#FFAB91", 18), "设置预设标签", [this, catId]() {
                QString currentTags = DatabaseManager::instance().getCategoryPresetTags(catId);
                FramelessInputDialog dlg("设置预设标签", "标签 (逗号分隔):", currentTags, this);
                if (dlg.exec() == QDialog::Accepted) {
                    DatabaseManager::instance().setCategoryPresetTags(catId, dlg.text());
                }
            });
            menu.addSeparator();
            menu.addAction(IconHelper::getIcon("add", "#aaaaaa", 18), "新建分组", [this]() {
                FramelessInputDialog dlg("新建分组", "组名称:", "", this);
                if (dlg.exec() == QDialog::Accepted) {
                    QString text = dlg.text();
                    if (!text.isEmpty()) {
                        DatabaseManager::instance().addCategory(text);
                        refreshData();
                    }
                }
            });
            menu.addAction(IconHelper::getIcon("add", "#3498db", 18), "新建子分区", [this, catId]() {
                FramelessInputDialog dlg("新建子分区", "区名称:", "", this);
                if (dlg.exec() == QDialog::Accepted) {
                    QString text = dlg.text();
                    if (!text.isEmpty()) {
                        DatabaseManager::instance().addCategory(text, catId);
                        refreshData();
                    }
                }
            });
            menu.addSeparator();

            if (selected.size() == 1) {
                menu.addAction(IconHelper::getIcon("edit", "#aaaaaa", 18), "重命名分类", [this, index]() {
                    m_partitionTree->edit(index);
                });
            }

            QString deleteText = selected.size() > 1 ? QString("删除选中的 %1 个分类").arg(selected.size()) : "删除分类";
            menu.addAction(IconHelper::getIcon("trash", "#e74c3c", 18), deleteText, [this, selected]() {
                QString confirmMsg = selected.size() > 1 ? "确定要删除选中的分类及其子分类和笔记吗？" : "确定要删除此分类吗？其子分类和笔记也将移至回收站。";
                FramelessMessageBox dlg("确认删除", confirmMsg, this);
                if (dlg.exec() == QDialog::Accepted) {
                    QList<int> ids;
                    for (const auto& idx : selected) {
                        if (idx.data(CategoryModel::TypeRole).toString() == "category") {
                            ids << idx.data(CategoryModel::IdRole).toInt();
                        }
                    }
                    DatabaseManager::instance().softDeleteCategories(ids);
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
                            refreshData();
                        } else {
                            ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #e74c3c;'>✖ 密码错误</b>");
                        }
                    }
                });
            });
            pwdMenu->addAction("立即锁定", [this, catId]() {
                DatabaseManager::instance().lockCategory(catId);
                refreshData();
            })->setShortcut(QKeySequence("Ctrl+Shift+L"));
        } else if (type == "trash") {
            menu.addAction(IconHelper::getIcon("refresh", "#2ecc71", 18), "全部恢复 (到未分类)", [this](){
                DatabaseManager::instance().restoreAllFromTrash();
                refreshData();
            });
            menu.addSeparator();
            menu.addAction(IconHelper::getIcon("trash", "#e74c3c", 18), "清空回收站", [this]() {
                FramelessMessageBox dlg("确认清空", "确定要永久删除回收站中的所有内容吗？\n(此操作不可逆)", this);
                if (dlg.exec() == QDialog::Accepted) {
                    DatabaseManager::instance().emptyTrash();
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
    
    // 连接拖拽信号 (使用 Model 定义的枚举)
    auto onNotesDropped = [this](const QList<int>& ids, const QModelIndex& targetIndex){
        if (!targetIndex.isValid()) return;
        QString type = targetIndex.data(CategoryModel::TypeRole).toString();
        for (int id : ids) {
            if (type == "category") {
                int catId = targetIndex.data(CategoryModel::IdRole).toInt();
                DatabaseManager::instance().updateNoteState(id, "category_id", catId);
            } else if (targetIndex.data().toString() == "收藏" || type == "bookmark") { 
                DatabaseManager::instance().updateNoteState(id, "is_favorite", 1);
            } else if (type == "trash") {
                DatabaseManager::instance().updateNoteState(id, "is_deleted", 1);
            } else if (type == "uncategorized") {
                DatabaseManager::instance().updateNoteState(id, "category_id", QVariant());
            }
        }
        refreshData();
    };

    connect(m_systemTree, &DropTreeView::notesDropped, this, onNotesDropped);
    connect(m_partitionTree, &DropTreeView::notesDropped, this, onNotesDropped);
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
        "  border-top-left-radius: 12px;"
        "  border-top-right-radius: 12px;"
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

    // 列表标题栏 (锁定 32px, 统一配色与分割线)
    auto* listHeader = new QWidget();
    listHeader->setFixedHeight(32);
    listHeader->setStyleSheet(
        "background-color: #252526; "
        "border-top-left-radius: 12px; "
        "border-top-right-radius: 12px; "
        "border-bottom: 1px solid #333;" 
    );
    auto* listHeaderLayout = new QHBoxLayout(listHeader);
    listHeaderLayout->setContentsMargins(15, 0, 15, 0); 
    auto* listIcon = new QLabel();
    listIcon->setPixmap(IconHelper::getIcon("list_ul", "#2ecc71").pixmap(18, 18));
    listHeaderLayout->addWidget(listIcon);
    auto* listHeaderTitle = new QLabel("笔记列表");
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
    
    m_noteList = new CleanListView();
    m_noteList->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_noteList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_noteModel = new NoteModel(this);
    m_noteList->setModel(m_noteModel);
    m_noteList->setItemDelegate(new NoteDelegate(m_noteList));
    m_noteList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_noteList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    connect(m_noteList, &QListView::customContextMenuRequested, this, &MainWindow::showContextMenu);
    
    // 恢复垂直间距为 5，垂直 Padding 为 5；仅水平 Padding 设为 0
    m_noteList->setSpacing(5); 
    m_noteList->setStyleSheet("QListView { background: transparent; border: none; padding-top: 5px; padding-bottom: 5px; padding-left: 0px; padding-right: 0px; }");
    
    // 基础拖拽使能 (其余复杂逻辑已由 CleanListView 实现)
    m_noteList->setDragEnabled(true);

    connect(m_noteList->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::onSelectionChanged);
    connect(m_noteList, &QListView::doubleClicked, this, [this](const QModelIndex& index){
        if (!index.isValid()) return;
        int id = index.data(NoteModel::IdRole).toInt();
        QVariantMap note = DatabaseManager::instance().getNoteById(id);
        QString type = note.value("item_type").toString();
        
        if (type == "local_file" || type == "local_folder" || type == "local_batch") {
            QString relativePath = note.value("content").toString();
            QString fullPath = QCoreApplication::applicationDirPath() + "/" + relativePath;
            
            if (QFile::exists(fullPath)) {
                QDesktopServices::openUrl(QUrl::fromLocalFile(fullPath));
            } else {
                ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #e74c3c;'>✖ 文件已丢失：<br></b>" + fullPath);
            }
            return;
        }

        NoteEditWindow* win = new NoteEditWindow(id);
        connect(win, &NoteEditWindow::noteSaved, this, &MainWindow::refreshData);
        win->show();
    });

    listContentLayout->addWidget(m_noteList);

    m_lockWidget = new CategoryLockWidget(this);
    m_lockWidget->setVisible(false);
    connect(m_lockWidget, &CategoryLockWidget::unlocked, this, [this](){
        refreshData();
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
        "  border-top-left-radius: 12px;"
        "  border-top-right-radius: 12px;"
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
        "border-top-left-radius: 12px; "
        "border-top-right-radius: 12px; "
        "border-bottom: 1px solid #333;"
    );
    auto* editorHeaderLayout = new QHBoxLayout(editorHeader);
    // [CRITICAL] 视觉对齐锁定：此处顶部边距必须设为 2px，以配合 32px 的标题栏高度，使文字达到垂直居中。
    editorHeaderLayout->setContentsMargins(15, 2, 15, 0);
    auto* edIcon = new QLabel();
    edIcon->setPixmap(IconHelper::getIcon("eye", "#e67e22").pixmap(18, 18));
    editorHeaderLayout->addWidget(edIcon);
    auto* edTitle = new QLabel("预览数据"); // 保护用户修改的标题内容
    edTitle->setStyleSheet("color: #e67e22; font-size: 13px; font-weight: bold; background: transparent; border: none;");
    editorHeaderLayout->addWidget(edTitle);
    editorHeaderLayout->addStretch();

    // [CRITICAL] 编辑逻辑重定义：MainWindow 已移除行内编辑模式，此按钮固定为触发弹窗编辑 (doEditSelected)。
    m_editBtn = new QPushButton();
    m_editBtn->setFixedSize(24, 24);
    m_editBtn->setCursor(Qt::PointingHandCursor);
    m_editBtn->setEnabled(false);
    m_editBtn->setToolTip("编辑选中的笔记 (Ctrl+B)");
    m_editBtn->setIcon(IconHelper::getIcon("edit", "#555555"));
    m_editBtn->setStyleSheet(
        "QPushButton { background: transparent; border: none; border-radius: 4px; }"
        "QPushButton:hover:enabled { background-color: rgba(255, 255, 255, 0.1); }"
    );
    connect(m_editBtn, &QPushButton::clicked, this, &MainWindow::doEditSelected);
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

    // 内容容器
    auto* editorContent = new QWidget();
    editorContent->setAttribute(Qt::WA_StyledBackground, true);
    editorContent->setStyleSheet("background: transparent; border: none;");
    auto* editorContentLayout = new QVBoxLayout(editorContent);
    editorContentLayout->setContentsMargins(2, 2, 2, 2); // 编辑器保留微量对齐边距

    m_editor = new Editor();
    m_editor->togglePreview(true); // 默认开启预览模式
    m_editor->setReadOnly(true); // 默认不可编辑
    
    editorContentLayout->addWidget(m_editor);
    editorContainerLayout->addWidget(editorContent);
    
    // 直接放入 Splitter
    splitter->addWidget(editorContainer);

    // 5. 元数据面板 - 独立出来
    m_metaPanel = new MetadataPanel(this);
    m_metaPanel->setMinimumWidth(230);
    connect(m_metaPanel, &MetadataPanel::noteUpdated, this, &MainWindow::refreshData);
    connect(m_metaPanel, &MetadataPanel::closed, this, [this](){
        m_header->setMetadataActive(false);
    });
    connect(m_metaPanel, &MetadataPanel::tagAdded, this, [this](const QStringList& tags){
        QModelIndexList indices = m_noteList->selectionModel()->selectedIndexes();
        if (indices.isEmpty()) return;
        for (const auto& index : std::as_const(indices)) {
            int id = index.data(NoteModel::IdRole).toInt();
            DatabaseManager::instance().addTagsToNote(id, tags);
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

    // 6. 高级筛选器卡片容器
    auto* filterContainer = new QFrame();
    filterContainer->setMinimumWidth(230);
    filterContainer->setObjectName("FilterContainer");
    filterContainer->setAttribute(Qt::WA_StyledBackground, true);
    filterContainer->setStyleSheet(
        "#FilterContainer {"
        "  background-color: #1e1e1e;"
        "  border: 1px solid #333333;"
        "  border-top-left-radius: 12px;"
        "  border-top-right-radius: 12px;"
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
        "border-top-left-radius: 12px; "
        "border-top-right-radius: 12px; "
        "border-bottom: 1px solid #333;"
    );
    auto* filterHeaderLayout = new QHBoxLayout(filterHeader);
    filterHeaderLayout->setContentsMargins(15, 0, 15, 0);
    auto* fiIcon = new QLabel();
    fiIcon->setPixmap(IconHelper::getIcon("filter", "#f1c40f").pixmap(18, 18));
    filterHeaderLayout->addWidget(fiIcon);
    auto* fiTitle = new QLabel("高级筛选");
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

    auto* preview = QuickPreview::instance();
    connect(preview, &QuickPreview::editRequested, this, [this, preview](int id){
        if (!preview->caller() || preview->caller()->window() != this) return;
        NoteEditWindow* win = new NoteEditWindow(id);
        connect(win, &NoteEditWindow::noteSaved, this, &MainWindow::refreshData);
        win->show();
    });
    connect(preview, &QuickPreview::prevRequested, this, [this, preview](){
        if (!preview->caller() || preview->caller()->window() != this) return;
        QModelIndex current = m_noteList->currentIndex();
        if (!current.isValid() || m_noteModel->rowCount() == 0) return;

        int catId = current.data(NoteModel::CategoryIdRole).toInt();
        int row = current.row();
        int count = m_noteModel->rowCount();
        
        // 循环向上查找相同分类
        for (int i = 1; i <= count; ++i) {
            int prevRow = (row - i + count) % count;
            QModelIndex idx = m_noteModel->index(prevRow, 0);
            if (idx.data(NoteModel::CategoryIdRole).toInt() == catId) {
                m_noteList->setCurrentIndex(idx);
                m_noteList->scrollTo(idx);
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
        QModelIndex current = m_noteList->currentIndex();
        if (!current.isValid() || m_noteModel->rowCount() == 0) return;

        int catId = current.data(NoteModel::CategoryIdRole).toInt();
        int row = current.row();
        int count = m_noteModel->rowCount();

        // 循环向下查找相同分类
        for (int i = 1; i <= count; ++i) {
            int nextRow = (row + i) % count;
            QModelIndex idx = m_noteModel->index(nextRow, 0);
            if (idx.data(NoteModel::CategoryIdRole).toInt() == catId) {
                m_noteList->setCurrentIndex(idx);
                m_noteList->scrollTo(idx);
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
        // 在模型中查找此 ID 的行
        for (int i = 0; i < m_noteModel->rowCount(); ++i) {
            QModelIndex idx = m_noteModel->index(i, 0);
            if (idx.data(NoteModel::IdRole).toInt() == id) {
                m_noteList->setCurrentIndex(idx);
                m_noteList->scrollTo(idx);
                // 注意：setCurrentIndex 会触发 onSelectionChanged -> updatePreviewContent
                return;
            }
        }
        // 如果在当前列表中没找到（可能被过滤了），则直接更新预览内容而不切换列表选中项
        QVariantMap note = DatabaseManager::instance().getNoteById(id);
        if (!note.isEmpty()) {
            preview->showPreview(
                id,
                note.value("title").toString(),
                note.value("content").toString(),
                note.value("item_type").toString(),
                note.value("data_blob").toByteArray(),
                preview->pos(),
                "", // 分类名暂时留空或根据需要查询
                m_noteList
            );
        }
    });

    m_noteList->installEventFilter(this);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls() || event->mimeData()->hasText() || event->mimeData()->hasImage()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dragMoveEvent(QDragMoveEvent* event) {
    event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event) {
    const QMimeData* mime = event->mimeData();
    int targetId = -1;
    if (m_currentFilterType == "category") {
        targetId = m_currentFilterValue.toInt();
    }

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
            DatabaseManager::instance().addNote("外部链接", remoteUrls.join(";"), {"链接"}, "", targetId, "link");
            event->acceptProposedAction();
            return;
        }
    } else if (mime->hasText() && !mime->text().trimmed().isEmpty()) {
        QString content = mime->text();
        QString title = content.trimmed().left(50).replace("\n", " ");
        DatabaseManager::instance().addNote(title, content, {}, "", targetId, "text");
        event->acceptProposedAction();
    } else if (mime->hasImage()) {
        QImage img = qvariant_cast<QImage>(mime->imageData());
        if (!img.isNull()) {
            QByteArray dataBlob;
            QBuffer buffer(&dataBlob);
            buffer.open(QIODevice::WriteOnly);
            img.save(&buffer, "PNG");
            DatabaseManager::instance().addNote("[拖入图片] " + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"), "[Image Data]", {}, "", targetId, "image", dataBlob);
            event->acceptProposedAction();
        }
    }
}

void MainWindow::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);
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

void MainWindow::onNoteAdded(const QVariantMap& note) {
    // 1. 基础状态检查
    if (note.value("is_deleted").toInt() == 1) return;

    // 2. 检查分类/状态过滤器匹配
    bool matches = true;
    if (m_currentFilterType == "category") {
        matches = (note.value("category_id").toInt() == m_currentFilterValue.toInt());
    } else if (m_currentFilterType == "untagged") {
        matches = note.value("tags").toString().isEmpty();
    } else if (m_currentFilterType == "bookmark") {
        matches = (note.value("is_favorite").toInt() == 1);
    } else if (m_currentFilterType == "trash") {
        matches = false;
    } else if (m_currentFilterType == "recently_visited") {
        matches = false; // 新笔记尚未正式被“访问”
    }

    // 3. 关键词匹配检查
    if (matches && !m_currentKeyword.isEmpty()) {
        QString title = note.value("title").toString();
        QString content = note.value("content").toString();
        QString tags = note.value("tags").toString();
        if (!title.contains(m_currentKeyword, Qt::CaseInsensitive) && 
            !content.contains(m_currentKeyword, Qt::CaseInsensitive) && 
            !tags.contains(m_currentKeyword, Qt::CaseInsensitive)) {
            matches = false;
        }
    }

    // 4. 高级筛选器活跃时，为了保证精准，采取全量刷新策略
    if (matches && !m_filterWrapper->isHidden()) {
        matches = false;
    }

    if (matches && m_currentPage == 1) {
        m_noteModel->prependNote(note);
        m_noteList->scrollToTop();
    }
    
    // 依然需要触发侧边栏计数同步与潜在的高级筛选状态刷新
    scheduleRefresh();
}

void MainWindow::scheduleRefresh() {
    m_refreshTimer->start();
}

void MainWindow::refreshData() {
    // 保存当前选中项状态以供恢复
    QString selectedType;
    QVariant selectedValue;
    QModelIndex sysIdx = m_systemTree->currentIndex();
    QModelIndex partIdx = m_partitionTree->currentIndex();
    
    // 记忆当前选中的笔记 ID 列表，以便在刷新后恢复多选状态
    QSet<int> selectedNoteIds;
    auto selectedIndices = m_noteList->selectionModel()->selectedIndexes();
    for (const auto& idx : selectedIndices) {
        selectedNoteIds.insert(idx.data(NoteModel::IdRole).toInt());
    }
    int lastCurrentNoteId = m_noteList->currentIndex().data(NoteModel::IdRole).toInt();

    if (sysIdx.isValid()) {
        selectedType = sysIdx.data(CategoryModel::TypeRole).toString();
        selectedValue = sysIdx.data(CategoryModel::NameRole);
    } else if (partIdx.isValid()) {
        selectedType = partIdx.data(CategoryModel::TypeRole).toString();
        selectedValue = partIdx.data(CategoryModel::IdRole);
    }

    QSet<QString> expandedPaths;
    std::function<void(const QModelIndex&)> checkChildren = [&](const QModelIndex& parent) {
        for (int j = 0; j < m_partitionModel->rowCount(parent); ++j) {
            QModelIndex child = m_partitionModel->index(j, 0, parent);
            if (m_partitionTree->isExpanded(child)) {
                QString type = child.data(CategoryModel::TypeRole).toString();
                if (type == "category") {
                    expandedPaths.insert("cat_" + QString::number(child.data(CategoryModel::IdRole).toInt()));
                } else {
                    expandedPaths.insert(child.data(CategoryModel::NameRole).toString());
                }
            }
            if (m_partitionModel->rowCount(child) > 0) checkChildren(child);
        }
    };

    for (int i = 0; i < m_partitionModel->rowCount(); ++i) {
        QModelIndex index = m_partitionModel->index(i, 0);
        if (m_partitionTree->isExpanded(index)) {
            expandedPaths.insert(index.data(CategoryModel::NameRole).toString());
        }
        checkChildren(index);
    }

    QVariantMap criteria = m_filterPanel->getCheckedCriteria();
    auto notes = DatabaseManager::instance().searchNotes(m_currentKeyword, m_currentFilterType, m_currentFilterValue, m_currentPage, m_pageSize, criteria);
    int totalCount = DatabaseManager::instance().getNotesCount(m_currentKeyword, m_currentFilterType, m_currentFilterValue, criteria);

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

    m_noteList->setVisible(!isLocked);
    m_lockWidget->setVisible(isLocked);

    if (isLocked) {
        m_editor->setPlainText("");
        m_metaPanel->clearSelection();
    }

    m_noteModel->setNotes(isLocked ? QList<QVariantMap>() : notes);

    // 恢复笔记选中状态 (支持多选恢复)
    if (!selectedNoteIds.isEmpty()) {
        QItemSelection selection;
        for (int i = 0; i < m_noteModel->rowCount(); ++i) {
            QModelIndex idx = m_noteModel->index(i, 0);
            int id = idx.data(NoteModel::IdRole).toInt();
            if (selectedNoteIds.contains(id)) {
                selection.select(idx, idx);
            }
            if (id == lastCurrentNoteId) {
                m_noteList->setCurrentIndex(idx);
            }
        }
        if (!selection.isEmpty()) {
            m_noteList->selectionModel()->select(selection, QItemSelectionModel::Select | QItemSelectionModel::Rows);
        }
    }

    m_systemModel->refresh();
    m_partitionModel->refresh();

    int totalPages = (totalCount + m_pageSize - 1) / m_pageSize;
    if (totalPages < 1) totalPages = 1;
    m_header->updatePagination(m_currentPage, totalPages);

    // 恢复系统项选中
    if (!selectedType.isEmpty() && selectedType != "category") {
        for (int i = 0; i < m_systemModel->rowCount(); ++i) {
            QModelIndex idx = m_systemModel->index(i, 0);
            if (idx.data(CategoryModel::TypeRole).toString() == selectedType &&
                idx.data(CategoryModel::NameRole) == selectedValue) {
                m_systemTree->setCurrentIndex(idx);
                break;
            }
        }
    }

    // 恢复分区选中与展开
    for (int i = 0; i < m_partitionModel->rowCount(); ++i) {
        QModelIndex index = m_partitionModel->index(i, 0);
        QString name = index.data(Qt::DisplayRole).toString();

        // [CRITICAL] 锁定：基于文本“我的分区”恢复默认展开状态
        if (name == "我的分区" || expandedPaths.contains(name)) {
            m_partitionTree->setExpanded(index, true);
        }
        
        std::function<void(const QModelIndex&)> restoreChildren = [&](const QModelIndex& parent) {
            for (int j = 0; j < m_partitionModel->rowCount(parent); ++j) {
                QModelIndex child = m_partitionModel->index(j, 0, parent);
                QString cType = child.data(CategoryModel::TypeRole).toString();
                QString cName = child.data(CategoryModel::NameRole).toString();
                
                // 恢复选中
                if (!selectedType.isEmpty() && cType == "category" && child.data(CategoryModel::IdRole) == selectedValue) {
                    m_partitionTree->setCurrentIndex(child);
                }

                QString identifier = (cType == "category") ? 
                    ("cat_" + QString::number(child.data(CategoryModel::IdRole).toInt())) : cName;

                // [CRITICAL] 锁定：基于文本匹配，确保“我的分区”下的直属分类始终展开
                if (expandedPaths.contains(identifier) || (parent.data(Qt::DisplayRole).toString() == "我的分区")) {
                    m_partitionTree->setExpanded(child, true);
                }
                if (m_partitionModel->rowCount(child) > 0) restoreChildren(child);
            }
        };
        restoreChildren(index);
    }

    if (!m_filterWrapper->isHidden()) {
        m_filterPanel->updateStats(m_currentKeyword, m_currentFilterType, m_currentFilterValue);
    }
}

void MainWindow::onSelectionChanged(const QItemSelection& selected, const QItemSelection& deselected) {
    QModelIndexList indices = m_noteList->selectionModel()->selectedIndexes();
    if (indices.isEmpty()) {
        m_metaPanel->clearSelection();
        m_editor->setPlainText("");
        m_editBtn->setEnabled(false);
        m_editBtn->setIcon(IconHelper::getIcon("edit", "#555555"));
    } else if (indices.size() == 1) {
        int id = indices.first().data(NoteModel::IdRole).toInt();
        QVariantMap note = DatabaseManager::instance().getNoteById(id);
        
        // 记录访问
        DatabaseManager::instance().recordAccess(id);

        m_editor->setNote(note, true);
        m_editor->togglePreview(true); // 切换笔记时默认展示预览
        m_metaPanel->setNote(note);
        m_editBtn->setEnabled(true);
        m_editBtn->setIcon(IconHelper::getIcon("edit", "#e67e22"));

    } else {
        m_metaPanel->setMultipleNotes(indices.size());
        m_editor->setPlainText(QString("已选中 %1 条笔记").arg(indices.size()));
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
    auto* previewSc = new QShortcut(ShortcutManager::instance().getShortcut("mw_preview"), m_noteList, [this](){ doPreview(); }, Qt::WidgetShortcut);
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
    add("mw_save", [this](){ doLockSelected(); });
    add("mw_edit", [this](){ doEditSelected(); });
    add("mw_extract", [this](){ doExtractContent(); });
    add("mw_lock_cat", [this](){
        if (m_currentFilterType == "category" && m_currentFilterValue != -1) {
            DatabaseManager::instance().lockCategory(m_currentFilterValue.toInt());
            refreshData();
        }
    });

    // [PROFESSIONAL] 将删除快捷键绑定到列表，允许侧边栏通过 eventFilter 独立处理 Del 键
    auto* delSoftSc = new QShortcut(ShortcutManager::instance().getShortcut("mw_delete_soft"), m_noteList, [this](){ doDeleteSelected(false); }, Qt::WidgetShortcut);
    delSoftSc->setProperty("id", "mw_delete_soft");
    auto* delHardSc = new QShortcut(ShortcutManager::instance().getShortcut("mw_delete_hard"), m_noteList, [this](){ doDeleteSelected(true); }, Qt::WidgetShortcut);
    delHardSc->setProperty("id", "mw_delete_hard");

    add("mw_copy_tags", [this](){ doCopyTags(); });
    add("mw_paste_tags", [this](){ doPasteTags(); });
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

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_noteList && event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_F2) {
            QModelIndex current = m_noteList->currentIndex();
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
    }

    if ((watched == m_partitionTree || watched == m_systemTree) && event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        int key = keyEvent->key();
        auto modifiers = keyEvent->modifiers();

        if (key == Qt::Key_F2) {
            if (watched == m_partitionTree) {
                QModelIndex current = m_partitionTree->currentIndex();
                if (current.isValid() && current.data(CategoryModel::TypeRole).toString() == "category") {
                    // [CRITICAL] 锁定：统一使用行内编辑模式，严禁改为弹出对话框，以保持与 QuickWindow 的逻辑一致性
                    m_partitionTree->edit(current);
                }
            }
            return true;
        }

        if (key == Qt::Key_Delete) {
            if (watched == m_partitionTree) {
                auto selected = m_partitionTree->selectionModel()->selectedIndexes();
                if (!selected.isEmpty()) {
                    QString confirmMsg = selected.size() > 1 ? QString("确定要删除选中的 %1 个分类及其下所有内容吗？").arg(selected.size()) : "确定要删除选中的分类及其下所有内容吗？";
                    FramelessMessageBox dlg("确认删除", confirmMsg, this);
                    if (dlg.exec() == QDialog::Accepted) {
                        QList<int> ids;
                        for (const auto& idx : selected) {
                            if (idx.data(CategoryModel::TypeRole).toString() == "category") {
                                ids << idx.data(CategoryModel::IdRole).toInt();
                            }
                        }
                        DatabaseManager::instance().softDeleteCategories(ids);
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
                    refreshData();
                    // 重新选中该分类 (refreshData 会刷新整个模型)
                    // 注意：refreshData 内部有恢复选中的逻辑，但它是基于 NameRole 的。
                    // 既然 sort_order 变了，我们需要确保它还在选中状态。
                    return true;
                }
            }
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::onTagSelected(const QModelIndex& index) {
    m_currentFilterType = index.data(CategoryModel::TypeRole).toString();
    if (m_currentFilterType == "category") {
        m_currentFilterValue = index.data(CategoryModel::IdRole).toInt();
        StringUtils::recordRecentCategory(m_currentFilterValue.toInt());
        DatabaseManager::instance().setActiveCategoryId(m_currentFilterValue.toInt());
    } else {
        m_currentFilterValue = -1;
        DatabaseManager::instance().setActiveCategoryId(-1);
    }
    m_currentPage = 1;
    refreshData();
}

void MainWindow::showContextMenu(const QPoint& pos) {
    auto selected = m_noteList->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) {
        QModelIndex index = m_noteList->indexAt(pos);
        if (index.isValid()) {
            m_noteList->setCurrentIndex(index);
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
        menu.addAction(IconHelper::getIcon("eye", "#1abc9c", 18), "预览 (Space)", this, &MainWindow::doPreview);
        
        QString content = selected.first().data(NoteModel::ContentRole).toString();
        QString type = selected.first().data(NoteModel::TypeRole).toString();
        
        if (type == "image") {
            menu.addAction(IconHelper::getIcon("screenshot_ocr", "#3498db", 18), "从图提取文字", this, &MainWindow::doOCR);
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
        
        menu.addAction(IconHelper::getIcon("calendar", "#4facfe", 18), "生成待办事项", [this, selected]() {
            int noteId = selected.first().data(NoteModel::IdRole).toInt();
            QString title = selected.first().data(NoteModel::TitleRole).toString();
            QString content = selected.first().data(NoteModel::ContentRole).toString();
            
            DatabaseManager::Todo t;
            t.title = "待办: " + title;
            t.content = StringUtils::htmlToPlainText(content);
            t.noteId = noteId;
            t.startTime = QDateTime::currentDateTime();
            t.endTime = t.startTime.addSecs(3600);
            
            DatabaseManager::instance().addTodo(t);
            ToolTipOverlay::instance()->showText(QCursor::pos(), "✅ 已成功转化为待办事项");
        });
    }
    
    menu.addAction(IconHelper::getIcon("copy", "#1abc9c", 18), QString("复制内容 (%1)").arg(selCount), this, &MainWindow::doExtractContent);
    menu.addSeparator();

    if (selCount == 1) {
        menu.addAction(IconHelper::getIcon("edit", "#4a90e2", 18), "编辑 (Ctrl+B)", this, &MainWindow::doEditSelected);
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

    bool isFavorite = (selCount == 1) && selected.first().data(NoteModel::FavoriteRole).toBool();
    menu.addAction(IconHelper::getIcon(isFavorite ? "bookmark_filled" : "bookmark", "#ff6b81", 18), 
                   isFavorite ? "取消书签" : "添加书签 (Ctrl+E)", this, &MainWindow::doToggleFavorite);

    bool isPinned = (selCount == 1) && selected.first().data(NoteModel::PinnedRole).toBool();
    menu.addAction(IconHelper::getIcon(isPinned ? "pin_vertical" : "pin_tilted", isPinned ? "#3A90FF" : "#aaaaaa", 18), 
                   isPinned ? "取消置顶" : "置顶选中项 (Ctrl+P)", this, &MainWindow::doTogglePin);
    
    bool isLocked = (selCount == 1) && selected.first().data(NoteModel::LockedRole).toBool();
    menu.addAction(IconHelper::getIcon("lock", isLocked ? "#aaaaaa" : "#888888", 18), 
                   isLocked ? "解锁选中项" : "锁定选中项 (Ctrl+S)", this, &MainWindow::doLockSelected);
    
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
            // 批量恢复笔记（不再强制设为 NULL，保留原分类关系）
            if (!noteIds.isEmpty()) DatabaseManager::instance().updateNoteStateBatch(noteIds, "is_deleted", 0);
            // 批量恢复分类及其层级
            if (!catIds.isEmpty()) DatabaseManager::instance().restoreCategories(catIds);
            refreshData();
        });
        menu.addAction(IconHelper::getIcon("trash", "#e74c3c", 18), "彻底删除 (不可逆)", [this](){ doDeleteSelected(true); });
    } else {
        menu.addAction(IconHelper::getIcon("trash", "#e74c3c", 18), "移至回收站 (Delete)", [this](){ doDeleteSelected(false); });
    }

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
        btnStay->setIcon(IconHelper::getIcon(stayOnTop ? "pin_vertical" : "pin_tilted", stayOnTop ? "#ffffff" : "#aaaaaa", 20));
        
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

void MainWindow::showToolboxMenu(const QPoint& pos) {
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
        // 预定位：居中于主窗口
        dlg->move(this->geometry().center() - dlg->rect().center());
        dlg->exec();
    });

    menu.exec(pos);
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
    QModelIndex index = m_noteList->currentIndex();
    if (!index.isValid()) return;
    int id = index.data(NoteModel::IdRole).toInt();
    QVariantMap note = DatabaseManager::instance().getNoteById(id);
    
    auto* preview = QuickPreview::instance();

    QPoint pos;
    if (preview->isVisible()) {
        pos = preview->pos();
    } else {
        pos = m_noteList->mapToGlobal(m_noteList->rect().center()) - QPoint(250, 300);
    }

    preview->showPreview(
        id,
        note.value("title").toString(), 
        note.value("content").toString(), 
        note.value("item_type").toString(),
        note.value("data_blob").toByteArray(),
        pos,
        index.data(NoteModel::CategoryNameRole).toString(),
        m_noteList
    );
}

void MainWindow::doDeleteSelected(bool physical) {
    auto selected = m_noteList->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;

    bool inTrash = (m_currentFilterType == "trash");
    
    if (physical || inTrash) {
        QString title = inTrash ? "清空项目" : "彻底删除";
        QString text = QString("确定要永久删除选中的 %1 条数据吗？\n此操作不可逆，数据将无法找回。").arg(selected.count());
        
        FramelessMessageBox msg(title, text, this);
        QList<int> idsToDelete;
        for (const auto& index : std::as_const(selected)) idsToDelete << index.data(NoteModel::IdRole).toInt();
        
        if (msg.exec() == QDialog::Accepted) {
            if (!idsToDelete.isEmpty()) {
                DatabaseManager::instance().deleteNotesBatch(idsToDelete);
                refreshData();
                ToolTipOverlay::instance()->showText(QCursor::pos(), QString("✔ 已永久删除 %1 条数据").arg(idsToDelete.size()));
            }
        }
    } else {
        QList<int> ids;
        for (const auto& index : std::as_const(selected)) ids << index.data(NoteModel::IdRole).toInt();
        DatabaseManager::instance().softDeleteNotes(ids);
        refreshData();
    }
}

void MainWindow::doToggleFavorite() {
    auto selected = m_noteList->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;
    for (const auto& index : std::as_const(selected)) {
        int id = index.data(NoteModel::IdRole).toInt();
        DatabaseManager::instance().toggleNoteState(id, "is_favorite");
    }
    refreshData();
}

void MainWindow::doTogglePin() {
    auto selected = m_noteList->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;
    for (const auto& index : std::as_const(selected)) {
        int id = index.data(NoteModel::IdRole).toInt();
        DatabaseManager::instance().toggleNoteState(id, "is_pinned");
    }
    refreshData();
}

void MainWindow::doLockSelected() {
    auto selected = m_noteList->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;
    
    bool firstState = selected.first().data(NoteModel::LockedRole).toBool();
    bool targetState = !firstState;

    QList<int> ids;
    for (const auto& index : std::as_const(selected)) ids << index.data(NoteModel::IdRole).toInt();
    
    DatabaseManager::instance().updateNoteStateBatch(ids, "is_locked", targetState);
    refreshData();
}

void MainWindow::doNewIdea() {
    NoteEditWindow* win = new NoteEditWindow();
    connect(win, &NoteEditWindow::noteSaved, this, &MainWindow::refreshData);
    win->show();
}

void MainWindow::doOCR() {
    QModelIndex index = m_noteList->currentIndex();
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

void MainWindow::doExtractContent() {
    auto selected = m_noteList->selectionModel()->selectedIndexes();
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

void MainWindow::doEditSelected() {
    QModelIndex index = m_noteList->currentIndex();
    if (!index.isValid()) return;
    int id = index.data(NoteModel::IdRole).toInt();
    NoteEditWindow* win = new NoteEditWindow(id);
    connect(win, &NoteEditWindow::noteSaved, this, &MainWindow::refreshData);
    win->show();
}

void MainWindow::doSetRating(int rating) {
    auto selected = m_noteList->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;
    for (const auto& index : std::as_const(selected)) {
        int id = index.data(NoteModel::IdRole).toInt();
        DatabaseManager::instance().updateNoteState(id, "rating", rating);
    }
    refreshData();
}

void MainWindow::doMoveToCategory(int catId) {
    auto selected = m_noteList->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;

    QList<int> ids;
    for (const auto& index : std::as_const(selected)) ids << index.data(NoteModel::IdRole).toInt();
    
    DatabaseManager::instance().moveNotesToCategory(ids, catId);
    
    if (catId != -1) {
        StringUtils::recordRecentCategory(catId);
    }
    refreshData();
}


void MainWindow::doCopyTags() {
    auto selected = m_noteList->selectionModel()->selectedIndexes();
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

void MainWindow::doPasteTags() {
    auto selected = m_noteList->selectionModel()->selectedIndexes();
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

    // 刷新数据以显示新标签
    refreshData();
    ToolTipOverlay::instance()->showText(QCursor::pos(), QString("✅ 已覆盖粘贴标签至 %1 条数据").arg(selected.size()));
}

void MainWindow::doImportCategory(int catId) {
    QStringList files = QFileDialog::getOpenFileNames(this, "选择导入文件", "", "所有文件 (*.*);;CSV文件 (*.csv)");
    if (files.isEmpty()) return;

    int totalCount = FileStorageHelper::processImport(files, catId);
    
    refreshData();
    ToolTipOverlay::instance()->showText(QCursor::pos(), QString("✅ 导入完成，共处理 %1 个项目").arg(totalCount));
}

void MainWindow::doImportFolder(int catId) {
    QString dir = QFileDialog::getExistingDirectory(this, "选择导入文件夹", "");
    if (dir.isEmpty()) return;

    int totalCount = FileStorageHelper::processImport({dir}, catId);
    
    refreshData();
    ToolTipOverlay::instance()->showText(QCursor::pos(), QString("✅ 文件夹导入完成，共处理 %1 个项目").arg(totalCount));
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
    QString dir = QFileDialog::getExistingDirectory(this, "选择导出目录", "");
    if (dir.isEmpty()) return;

    QString safeCatName = catName;
    safeCatName.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");
    QString exportPath = dir + "/" + safeCatName;
    QDir().mkpath(exportPath);

    QList<QVariantMap> notes = DatabaseManager::instance().searchNotes("", "category", catId, -1, -1);
    if (notes.isEmpty()) return;

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

    FramelessProgressDialog* progress = nullptr;
    const qint64 sizeThreshold = 50 * 1024 * 1024;
    const int countThreshold = 50;
    if (totalSize >= sizeThreshold || totalCount >= countThreshold) {
        progress = new FramelessProgressDialog("导出进度", "正在准备导出文件...", 0, totalCount, this);
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
            } else if (type == "image") {
                fileName += ".png";
            }
            
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
        QCoreApplication::processEvents();
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
