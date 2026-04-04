#include "NavPanel.h"
#include "UiHelper.h"
#include "TreeItemDelegate.h"
#include "DropTreeView.h"
#include <QHeaderView>
#include <QScrollBar>
#include <QLabel>
#include <QStandardItemModel>
#include <QStandardItem>
#include <QFileIconProvider>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QStandardPaths>

namespace ArcMeta {

/**
 * @brief 构造函数，设置面板属性
 */
NavPanel::NavPanel(QWidget* parent)
    : QFrame(parent) {
    setObjectName("ListContainer");
    setAttribute(Qt::WA_StyledBackground, true);
    // 设置面板宽度（遵循文档：导航面板 230px）
    setMinimumWidth(230);
    
    // 核心修正：移除宽泛的 QWidget QSS，防止其屏蔽 MainWindow 赋予的 ID 边框样式
    setStyleSheet("color: #EEEEEE;");

    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);

    initUi();
}

/**
 * @brief 初始化 UI 组件
 */
void NavPanel::setFocusHighlight(bool visible) {
    if (m_focusLine) m_focusLine->setVisible(visible);
}

void NavPanel::initUi() {
    // 物理还原：1px 翠绿高亮焦点线 (#2ecc71)
    m_focusLine = new QWidget(this);
    m_focusLine->setFixedHeight(1);
    m_focusLine->setStyleSheet("background-color: #2ecc71;");
    m_focusLine->hide(); // 初始隐藏
    m_mainLayout->addWidget(m_focusLine);

    // 面板标题 (还原旧版架构：Layout + Icon + Text)
    QWidget* header = new QWidget(this);
    header->setObjectName("ContainerHeader");
    header->setFixedHeight(32);
    // 重新注入标题栏样式，确保背景色和边框还原
    header->setStyleSheet(
        "QWidget#ContainerHeader {"
        "  background-color: #252526;"
        "  border-bottom: 1px solid #333;"
        "}"
    );
    QHBoxLayout* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(15, 2, 15, 0); // 严格还原 15px 左右边距，顶部 2px 偏移以垂直居中
    headerLayout->setSpacing(8);

    QLabel* iconLabel = new QLabel(header);
    iconLabel->setPixmap(UiHelper::getIcon("list_ul", QColor("#2ecc71"), 18).pixmap(18, 18));
    headerLayout->addWidget(iconLabel);

    QLabel* titleLabel = new QLabel("笔记列表", header);
    titleLabel->setStyleSheet("color: #2ecc71; font-size: 13px; font-weight: bold; background: transparent; border: none;");
    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();

    m_mainLayout->addWidget(header);

    // 核心修正：为列表内容包裹容器，恢复旧版 (15, 8, 15, 8) 的呼吸边距
    QWidget* contentWrapper = new QWidget(this);
    contentWrapper->setStyleSheet("background: transparent; border: none;");
    QVBoxLayout* contentLayout = new QVBoxLayout(contentWrapper);
    contentLayout->setContentsMargins(15, 8, 15, 8);
    contentLayout->setSpacing(0);

    // 物理还原：使用自定义视图以支持无快照拖拽
    m_treeView = new DropTreeView(this);
    m_treeView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_treeView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_treeView->setHeaderHidden(true);
    m_treeView->setAnimated(true);
    
    // 物理还原：20px 缩进以对齐三角形图标
    m_treeView->setIndentation(20);
    
    // 物理修正：禁用编辑触发，防止双击重命名
    m_treeView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    
    // 物理还原：双击锁定为伸展或折叠
    m_treeView->setExpandsOnDoubleClick(true);
    
    // 增强：开启拖拽收藏功能
    m_treeView->setDragEnabled(true);
    m_treeView->setDragDropMode(QAbstractItemView::DragOnly);
    
    m_treeView->setItemDelegate(new TreeItemDelegate(this));

    m_model = new QStandardItemModel(this);
    QFileIconProvider iconProvider;

    // 1. 新增：桌面入口 (使用系统原生图标)
    QString desktopPath = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    QStandardItem* desktopItem = new QStandardItem(iconProvider.icon(QFileInfo(desktopPath)), "桌面");
    desktopItem->setData(desktopPath, Qt::UserRole + 1);
    // 增加虚拟子项以便显示展开箭头
    desktopItem->appendRow(new QStandardItem("Loading..."));
    m_model->appendRow(desktopItem);

    // 2. 新增：此电脑入口 (使用系统原生图标)
    // 对于此电脑这种虚拟路径，尝试用 Computer 专用图标，若失败则回退到系统驱动器图标
    QIcon computerIcon = iconProvider.icon(QFileIconProvider::Computer);
    QStandardItem* computerItem = new QStandardItem(computerIcon, "此电脑");
    computerItem->setData("computer://", Qt::UserRole + 1);
    m_model->appendRow(computerItem);

    // 3. 磁盘列表
    const auto drives = QDir::drives();
    for (const QFileInfo& drive : drives) {
        QString driveName = drive.absolutePath();
        QStandardItem* driveItem = new QStandardItem(iconProvider.icon(drive), driveName);
        driveItem->setData(driveName, Qt::UserRole + 1);
        driveItem->appendRow(new QStandardItem("Loading..."));
        m_model->appendRow(driveItem);
    }

    m_treeView->setModel(m_model);
    connect(m_treeView, &QTreeView::expanded, this, &NavPanel::onItemExpanded);

    // 树形控件样式美化
    m_treeView->setStyleSheet(
        "QTreeView { background-color: transparent; border: none; font-size: 12px; outline: none; }"
        "QTreeView::item { height: 28px; padding-left: 0px; color: #EEEEEE; }"
        
        "/* 物理还原：复原三角形折叠图标 */"
        "QTreeView::branch:has-children:closed { image: url(:/icons/arrow_right.svg); }"
        "QTreeView::branch:has-children:open   { image: url(:/icons/arrow_down.svg); }"
        "QTreeView::branch:has-children:closed:has-siblings { image: url(:/icons/arrow_right.svg); }"
        "QTreeView::branch:has-children:open:has-siblings   { image: url(:/icons/arrow_down.svg); }"
    );


    connect(m_treeView, &QTreeView::clicked, this, &NavPanel::onTreeClicked);

    contentLayout->addWidget(m_treeView);
    m_mainLayout->addWidget(contentWrapper, 1);
}

/**
 * @brief 设置当前显示的根路径并自动展开
 */
void NavPanel::setRootPath(const QString& path) {
    Q_UNUSED(path);
    // 由于改为扁平化快捷入口列表，不再支持 setRootPath 的树深度同步
}

void NavPanel::selectPath(const QString& path) {
    for (int i = 0; i < m_model->rowCount(); ++i) {
        QStandardItem* item = m_model->item(i);
        if (item->data(Qt::UserRole + 1).toString() == path) {
            m_treeView->setCurrentIndex(item->index());
            m_treeView->setFocus();
            break;
        }
    }
}

/**
 * @brief 当用户点击目录时，发出信号告知外部组件（如内容面板）
 */
void NavPanel::onTreeClicked(const QModelIndex& index) {
    QString path = index.data(Qt::UserRole + 1).toString();
    if (!path.isEmpty() && path != "computer://") {
        emit directorySelected(path);
    } else if (path == "computer://") {
        emit directorySelected("computer://");
    }
}

void NavPanel::onItemExpanded(const QModelIndex& index) {
    QStandardItem* item = m_model->itemFromIndex(index);
    if (!item) return;

    // 如果只有一个 Loading 子项，则触发真实加载
    if (item->rowCount() == 1 && item->child(0)->text() == "Loading...") {
        fetchChildDirs(item);
    }
}

void NavPanel::fetchChildDirs(QStandardItem* parent) {
    QString path = parent->data(Qt::UserRole + 1).toString();
    if (path.isEmpty() || path == "computer://") return;

    parent->removeRows(0, parent->rowCount());

    QDir dir(path);
    QFileInfoList list = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    QFileIconProvider iconProvider;

    for (const QFileInfo& info : list) {
        QStandardItem* child = new QStandardItem(iconProvider.icon(info), info.fileName());
        child->setData(info.absoluteFilePath(), Qt::UserRole + 1);
        
        // 探测是否有子目录，有则加占位符
        QDir subDir(info.absoluteFilePath());
        if (!subDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty()) {
            child->appendRow(new QStandardItem("Loading..."));
        }
        parent->appendRow(child);
    }
}

} // namespace ArcMeta
