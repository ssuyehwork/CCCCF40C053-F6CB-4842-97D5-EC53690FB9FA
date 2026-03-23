#include "MainWindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFrame>
#include <QSplitter>
#include <QStandardItem>
#include <QDir>
#include <QDebug>
#include <QCursor>
#include <QApplication>
#include <QScreen>
#include <QMessageBox>
#include <QThreadPool>
#include <QTimer>
#include <QStandardPaths>
#include <QFileInfo>
#include <QDesktopServices>
#include <QUrl>
#include "IconHelper.h"
#include "ToolTipOverlay.h"
#include "../mft/MftReader.h"
#include "../mft/PathBuilder.h"
#include "../db/Database.h"
#include "../db/SyncEngine.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <windowsx.h>
#endif

namespace ui {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent, Qt::FramelessWindowHint) {
    setWindowTitle("RapidNotes - Resource Manager");
    resize(1200, 800);
    setAcceptDrops(true);
    
    setupDatabase();
    initUI();
    startMftScanning();
    
    // 2026-03-22 按照用户要求：MainWindow 与 QuickWindow 彻底脱钩，不再共用任何笔记相关的模型
    // 采用 QTimer 延迟加载首个视图
    QTimer::singleShot(500, this, &MainWindow::refreshContent);
}

MainWindow::~MainWindow() {
    // 确保资源清理
}

void MainWindow::setupDatabase() {
    // 1. 初始化独立的 file_manager.db
    QString dbPath = QCoreApplication::applicationDirPath() + "/file_manager.db";
    if (!db::Database::instance().init(dbPath)) {
        qWarning() << "Failed to init file_manager.db";
    }
}

void MainWindow::initUI() {
    auto* centralWidget = new QWidget(this);
    centralWidget->setObjectName("CentralWidget");
    centralWidget->setStyleSheet("#CentralWidget { background-color: #1E1E1E; }");
    setCentralWidget(centralWidget);
    
    auto* mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // 顶部标题栏
    m_header = new HeaderBar(this);
    mainLayout->addWidget(m_header);
    
    connect(m_header, &HeaderBar::windowClose, this, &MainWindow::close);
    connect(m_header, &HeaderBar::windowMinimize, this, &MainWindow::showMinimized);
    connect(m_header, &HeaderBar::windowMaximize, this, [this](){
        if (isMaximized()) showNormal();
        else showMaximized();
    });

    // 主分割器：[左侧栏] | [核心内容] | [右侧栏]
    QSplitter* mainSplitter = new QSplitter(Qt::Horizontal, centralWidget);
    mainSplitter->setHandleWidth(5);
    mainSplitter->setStyleSheet("QSplitter::handle { background: transparent; }");

    // ==========================================
    // 1. 左侧垂直分栏 (① 分类 + ② 树状导航)
    // ==========================================
    QSplitter* leftSplitter = new QSplitter(Qt::Vertical, mainSplitter);
    
    // ① 分类容器
    m_container1_Category = new QFrame();
    m_container1_Category->setObjectName("CategoryContainer");
    m_container1_Category->setMinimumHeight(200);
    m_container1_Category->setStyleSheet("QFrame#CategoryContainer { background-color: #1E1E1E; border: 1px solid #333; border-radius: 0px; }");
    QVBoxLayout* catLayout = new QVBoxLayout(m_container1_Category);
    catLayout->setContentsMargins(10, 5, 10, 5);
    catLayout->setSpacing(5);
    
    QLabel* catLabel = new QLabel("数据分类");
    catLabel->setStyleSheet("color: #3498db; font-weight: bold; font-size: 13px;");
    catLayout->addWidget(catLabel);
    
    // 逻辑分类项 (占位，后期对接 SyncEngine 聚合表)
    QStringList cats = {"最近修改", "图片聚合", "大文件", "文档汇总"};
    for (const QString& c : cats) {
        QLabel* item = new QLabel(c);
        item->setStyleSheet("color: #CCC; padding-left: 10px;");
        catLayout->addWidget(item);
    }
    catLayout->addStretch();
    leftSplitter->addWidget(m_container1_Category);

    // ② 树状递归导航
    m_container2_Tree = new QFrame();
    m_container2_Tree->setObjectName("TreeNavigationContainer");
    m_container2_Tree->setStyleSheet("QFrame#TreeNavigationContainer { background-color: #1E1E1E; border: 1px solid #333; border-top: none; border-radius: 0px; }");
    QVBoxLayout* treeLayout = new QVBoxLayout(m_container2_Tree);
    treeLayout->setContentsMargins(0, 0, 0, 0);
    
    m_treeView = new QTreeView();
    m_treeView->setHeaderHidden(true);
    m_treeView->setIndentation(15);
    m_treeView->setStyleSheet("QTreeView { background: transparent; border: none; color: #CCC; }");
    m_treeModel = new QStandardItemModel(this);
    m_treeView->setModel(m_treeModel);
    
    treeLayout->addWidget(m_treeView);
    leftSplitter->addWidget(m_container2_Tree);

    mainSplitter->addWidget(leftSplitter);

    // ==========================================
    // 2. 中央内容区域 (③ 内容容器 - Adobe Bridge 风格)
    // ==========================================
    m_container3_Content = new QFrame();
    m_container3_Content->setObjectName("ContentContainer");
    m_container3_Content->setStyleSheet("QFrame#ContentContainer { background-color: #1E1E1E; border: 1px solid #333; border-left: none; border-right: none; border-radius: 0px; }");
    QVBoxLayout* contentLayout = new QVBoxLayout(m_container3_Content);
    contentLayout->setContentsMargins(5, 5, 5, 5);
    
    m_contentView = new QListView();
    m_contentView->setViewMode(QListView::IconMode);
    m_contentView->setResizeMode(QListView::Adjust);
    m_contentView->setSpacing(10);
    m_contentView->setGridSize(QSize(120, 140));
    m_contentView->setIconSize(QSize(64, 64));
    m_contentView->setStyleSheet("QListView { background: transparent; border: none; color: #CCC; }");
    
    m_contentModel = new QStandardItemModel(this);
    m_contentView->setModel(m_contentModel);
    
    contentLayout->addWidget(m_contentView);
    mainSplitter->addWidget(m_container3_Content);

    // ==========================================
    // 3. 右侧垂直分栏 (④ 元数据面板 + ⑤ 筛选器)
    // ==========================================
    QSplitter* rightSplitter = new QSplitter(Qt::Vertical, mainSplitter);
    
    // ④ 元数据面板
    m_container4_Meta = new QFrame();
    m_container4_Meta->setObjectName("MetadataPanelContainer");
    m_container4_Meta->setMinimumHeight(250);
    m_container4_Meta->setStyleSheet("QFrame#MetadataPanelContainer { background-color: #1E1E1E; border: 1px solid #333; border-radius: 0px; }");
    QVBoxLayout* metaLayout = new QVBoxLayout(m_container4_Meta);
    metaLayout->setContentsMargins(15, 10, 15, 10);
    
    QLabel* metaLabel = new QLabel("元数据属性");
    metaLabel->setStyleSheet("color: #41F2F2; font-weight: bold; font-size: 13px;");
    metaLayout->addWidget(metaLabel);
    
    // 元数据控件 (星级、标签、颜色标记)
    QLabel* starLabel = new QLabel("★ ★ ★ ★ ★");
    starLabel->setStyleSheet("color: #f39c12; font-size: 20px; margin-top: 10px;");
    metaLayout->addWidget(starLabel);
    
    metaLayout->addStretch();
    rightSplitter->addWidget(m_container4_Meta);

    // ⑤ 筛选器
    m_container5_Filter = new QFrame();
    m_container5_Filter->setObjectName("AdvancedFilterContainer");
    m_container5_Filter->setStyleSheet("QFrame#AdvancedFilterContainer { background-color: #1E1E1E; border: 1px solid #333; border-top: none; border-radius: 0px; }");
    QVBoxLayout* filterLayout = new QVBoxLayout(m_container5_Filter);
    filterLayout->setContentsMargins(15, 10, 15, 10);
    
    QLabel* filterLabel = new QLabel("高级筛选器");
    filterLabel->setStyleSheet("color: #f1c40f; font-weight: bold; font-size: 13px;");
    filterLayout->addWidget(filterLabel);
    
    filterLayout->addStretch();
    rightSplitter->addWidget(m_container5_Filter);

    mainSplitter->addWidget(rightSplitter);

    // 调整分割比例 (左:中:右 = 2:6:2)
    mainSplitter->setStretchFactor(0, 2);
    mainSplitter->setStretchFactor(1, 6);
    mainSplitter->setStretchFactor(2, 2);
    
    mainLayout->addWidget(mainSplitter);

    // 2026-03-22 统一遵循 UI 视觉规范：核心容器直角设计
    // [UI-FIX] 禁止将其他按钮、输入框修改为直角，严格保持 MainWindow 的五大容器直角标准。
}

void MainWindow::startMftScanning() {
    // 异步启动 MFT 扫描和 USN 监听
    QThreadPool::globalInstance()->start([](){
        DWORD drives = GetLogicalDrives();
        for (int i = 0; i < 26; ++i) {
            if (drives & (1 << i)) {
                wchar_t driveRoot[] = { (wchar_t)(L'A' + i), L':', L'\\', L'\0' };
                if (GetDriveTypeW(driveRoot) == DRIVE_FIXED) {
                    std::wstring volumePath = L"\\\\.\\" + std::wstring(driveRoot, 2);
                    // 2026-03-22 执行 MFT 加载
                    mft::MftReader::instance().loadVolumeIndex(volumePath);
                    // 启动 USN 实时监听
                    mft::UsnWatcher::instance().start(volumePath);
                }
            }
        }
        // 扫描完成后触发同步引擎
        db::SyncEngine::instance().startIncrementalSync();
    });
}

void MainWindow::refreshContent() {
    // [Consensus] ③ 标记的内容容器专用来显示文件夹或文件
    m_contentModel->clear();
    
    auto& index = mft::MftReader::instance().getIndex();
    std::lock_guard<std::mutex> lock(mft::MftReader::instance().getMutex());
    
    // 初始展示 C 盘根目录内容作为 Demo
    int limit = 0;
    for (const auto& pair : index) {
        if (limit++ > 200) break;
        
        QStandardItem* item = new QStandardItem(QString::fromStdWString(pair.second.name));
        if (pair.second.isDir()) {
            item->setIcon(style()->standardIcon(QStyle::SP_DirIcon));
        } else {
            item->setIcon(style()->standardIcon(QStyle::SP_FileIcon));
        }
        m_contentModel->appendRow(item);
    }
}

void MainWindow::onFileSelected(const QModelIndex& index) {
    // [Consensus] ④ 标记的容器模块专用来显示文件夹或文件的元数据
    if (!index.isValid()) return;
    qDebug() << "File selected:" << index.data().toString();
}

void MainWindow::onDirectoryClicked(const QModelIndex& index) {
    // [Consensus] ② 标记的容器模块弄成树状递归导航
    if (!index.isValid()) return;
    qDebug() << "Directory clicked:" << index.data().toString();
}

void MainWindow::applyFilter() {
    // [Consensus] ⑤ 标记的容器模块专用来过滤 / 筛选文件夹、文件
    qDebug() << "Applying filter...";
}

void MainWindow::onMetadataChanged() {
    // 原子写入 .am_meta.json 并同步至 SQLite
    qDebug() << "Metadata changed.";
}

#ifdef Q_OS_WIN
bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result) {
    MSG* msg = static_cast<MSG*>(message);
    if (msg->message == WM_NCHITTEST) {
        int x = GET_X_LPARAM(msg->lParam);
        int y = GET_Y_LPARAM(msg->lParam);
        QPoint pos = mapFromGlobal(QPoint(x, y));
        int margin = 5;
        if (pos.y() < margin) *result = HTTOP;
        else if (pos.y() > height() - margin) *result = HTBOTTOM;
        else if (pos.x() < margin) *result = HTLEFT;
        else if (pos.x() > width() - margin) *result = HTRIGHT;
        else if (pos.y() < 36) *result = HTCAPTION;
        else return false;
        return true;
    }
    return false;
}
#endif

bool MainWindow::event(QEvent* event) {
    return QMainWindow::event(event);
}

void MainWindow::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    mft::UsnWatcher::instance().stop();
    QMainWindow::closeEvent(event);
}

void MainWindow::updateToolboxStatus(bool active) {
    if (m_header) m_header->updateToolboxStatus(active);
}

} // namespace ui
