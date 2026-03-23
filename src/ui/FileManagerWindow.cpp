#include "FileManagerWindow.h"
#include <QSplitter>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFrame>
#include <QStandardItem>
#include <QTimer>
#include "../mft/MftReader.h"

namespace ui {

FileManagerWindow::FileManagerWindow(QWidget* parent) : QMainWindow(parent) {
    initUI();
    
    // 默认样式设置
    setStyleSheet("QWidget { border-radius: 0px; }"); // 核心容器采用直角
}

void FileManagerWindow::updateToolboxStatus(bool active) {
    // 基础实现，后续可扩展视觉反馈
}

void FileManagerWindow::refreshContent() {
    m_model->clear();
    auto& index = mft::MftReader::instance().getIndex();
    std::lock_guard<std::mutex> lock(mft::MftReader::instance().getMutex());
    
    int count = 0;
    for (const auto& pair : index) {
        if (count++ > 500) break; // 初始仅展示前500个以防卡顿
        
        QStandardItem* item = new QStandardItem(QString::fromStdWString(pair.second.name));
        if (pair.second.isDir()) {
            item->setIcon(style()->standardIcon(QStyle::SP_DirIcon));
        } else {
            item->setIcon(style()->standardIcon(QStyle::SP_FileIcon));
        }
        m_model->appendRow(item);
    }
}

void FileManagerWindow::initUI() {
    // 1. 顶部标题栏
    m_header = new HeaderBar(this);
    setMenuWidget(m_header);

    // 2. 主分割器布局
    QSplitter* mainSplitter = new QSplitter(Qt::Horizontal, this);
    setCentralWidget(mainSplitter);

    // ---------------------------------------------------------
    // 左侧区域 (分类 + 树状导航)
    // ---------------------------------------------------------
    QSplitter* leftSplitter = new QSplitter(Qt::Vertical, mainSplitter);
    
    // ① 分类容器模块
    m_categoryContainer = new QFrame();
    m_categoryContainer->setObjectName("CategoryContainer");
    m_categoryContainer->setMinimumHeight(150);
    QVBoxLayout* catLayout = new QVBoxLayout(m_categoryContainer);
    catLayout->setContentsMargins(10, 5, 10, 5);
    QLabel* catLabel = new QLabel("数据分类");
    catLabel->setStyleSheet("font-weight: bold; font-size: 14px;");
    catLayout->addWidget(catLabel);
    catLayout->addStretch();
    leftSplitter->addWidget(m_categoryContainer);

    // ② 树状导航递归模块
    m_treeContainer = new QFrame();
    m_treeContainer->setObjectName("NavigationTreeContainer");
    QVBoxLayout* treeLayout = new QVBoxLayout(m_treeContainer);
    treeLayout->setContentsMargins(0, 0, 0, 0);
    QTreeView* navTree = new QTreeView();
    navTree->setHeaderHidden(true);
    treeLayout->addWidget(navTree);
    leftSplitter->addWidget(m_treeContainer);

    mainSplitter->addWidget(leftSplitter);

    // ---------------------------------------------------------
    // 中间区域 (内容容器)
    // ---------------------------------------------------------
    // ③ 内容容器模块 (类似 Adobe Bridge)
    m_contentContainer = new QFrame(mainSplitter);
    m_contentContainer->setObjectName("ContentContainer");
    QVBoxLayout* contentLayout = new QVBoxLayout(m_contentContainer);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    
    m_contentView = new QListView();
    m_contentView->setViewMode(QListView::IconMode);
    m_contentView->setResizeMode(QListView::Adjust);
    m_contentView->setIconSize(QSize(64, 64));
    m_contentView->setGridSize(QSize(100, 100));
    
    m_model = new QStandardItemModel(this);
    m_contentView->setModel(m_model);
    
    contentLayout->addWidget(m_contentView);
    mainSplitter->addWidget(m_contentContainer);

    // ---------------------------------------------------------
    // 右侧区域 (元数据 + 高级筛选)
    // ---------------------------------------------------------
    QSplitter* rightSplitter = new QSplitter(Qt::Vertical, mainSplitter);

    // ④ 元数据面板模块 (星级、标签、颜色)
    m_metaContainer = new QFrame();
    m_metaContainer->setObjectName("MetadataPanelContainer");
    QVBoxLayout* metaLayout = new QVBoxLayout(m_metaContainer);
    metaLayout->setContentsMargins(10, 5, 10, 5);
    QLabel* metaLabel = new QLabel("元数据");
    metaLabel->setStyleSheet("font-weight: bold; font-size: 14px;");
    metaLayout->addWidget(metaLabel);
    metaLayout->addStretch();
    rightSplitter->addWidget(m_metaContainer);

    // ⑤ 高级筛选容器模块
    m_filterContainer = new QFrame();
    m_filterContainer->setObjectName("AdvancedFilterContainer");
    QVBoxLayout* filterLayout = new QVBoxLayout(m_filterContainer);
    filterLayout->setContentsMargins(10, 5, 10, 5);
    QLabel* filterLabel = new QLabel("高级筛选");
    filterLabel->setStyleSheet("font-weight: bold; font-size: 14px;");
    filterLayout->addWidget(filterLabel);
    filterLayout->addStretch();
    rightSplitter->addWidget(m_filterContainer);

    mainSplitter->addWidget(rightSplitter);

    // 设置初始分配比例 (左:中:右 = 1:3:1)
    mainSplitter->setStretchFactor(0, 1);
    mainSplitter->setStretchFactor(1, 3);
    mainSplitter->setStretchFactor(2, 1);

    // 初始加载内容 (延迟执行以等待 MFT 索引在后台线程完成初始扫描)
    QTimer::singleShot(2000, this, &FileManagerWindow::refreshContent);
}

} // namespace ui
