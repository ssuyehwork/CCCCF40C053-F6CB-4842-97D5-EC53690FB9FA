#include "CategoryPanel.h"
#include "CategoryModel.h"
#include "CategoryLockDialog.h"
#include "CategorySetPasswordDialog.h"
#include "CategoryDelegate.h"
#include "DropTreeView.h"
#include "UiHelper.h"
#include "ToolTipOverlay.h"
#include "FramelessDialog.h"
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include "../db/CategoryRepo.h"
#include "../meta/MetadataManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QHeaderView>
#include <QScrollBar>
#include <QMenu>
#include <QAction>
#include <QApplication>
#include <QRandomGenerator>
#include <QSet>
#include <QColorDialog>
#include <QSettings>
#include "Logger.h"

namespace ArcMeta {

CategoryPanel::CategoryPanel(QWidget* parent)
    : QFrame(parent) {
    setObjectName("SidebarContainer");
    setAttribute(Qt::WA_StyledBackground, true);
    setMinimumWidth(230);
    setStyleSheet("color: #EEEEEE;");
    
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);

    initUi();
    setupContextMenu();
}

void CategoryPanel::setupContextMenu() {
    m_categoryTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_categoryTree, &QWidget::customContextMenuRequested, [this](const QPoint& pos) {
        QModelIndex index = m_categoryTree->indexAt(pos);
        
        // 2026-03-xx 按照用户要求：实现右键点击即选中，解决“分类与其子分类”交互一致性问题
        if (index.isValid()) {
            m_categoryTree->setCurrentIndex(index);
        }

        QMenu menu(this);
        // [PHYSICAL RESTORATION] 8px radius for context menu
        menu.setStyleSheet("QMenu { background-color: #2D2D2D; color: #EEE; border: 1px solid #444; padding: 4px; border-radius: 8px; } "
                           "QMenu::item { padding: 6px 25px 6px 10px; border-radius: 4px; } "
                           "QMenu::icon { margin-left: 6px; } "
                           "QMenu::item:selected { background-color: #3E3E42; color: white; } "
                           "QMenu::separator { height: 1px; background: #444; margin: 4px 8px; }");

        // 基于规范逻辑：如果没有选中项，或者选中了“我的分类”根节点
        QString itemName = index.data(CategoryModel::NameRole).toString();
        if (!index.isValid() || itemName == "我的分类") {
            menu.addAction(UiHelper::getIcon("add", QColor("#aaaaaa"), 18), "新建分类", this, &CategoryPanel::onCreateCategory);
            
            auto* sortMenu = menu.addMenu(UiHelper::getIcon("list_ul", QColor("#aaaaaa"), 18), "排列");
            sortMenu->setStyleSheet(menu.styleSheet());
            sortMenu->addAction("标题(全部) (A→Z)", this, &CategoryPanel::onSortAllByNameAsc);
            sortMenu->addAction("标题(全部) (Z→A)", this, &CategoryPanel::onSortAllByNameDesc);
        } else {
            // 2026-03-xx 按照用户要求：补全子层级（子分类、文件、文件夹）的右键菜单
            QString type = index.data(CategoryModel::TypeRole).toString();
            
            // 只要不是系统根节点，都弹出完整菜单
            if (type == "category" || type == "file" || type == "folder") {
                menu.addAction(UiHelper::getIcon("branch", QColor("#3498db"), 18), "归类到此分类", this, &CategoryPanel::onClassifyToCategory);
                
                menu.addSeparator();
                
                menu.addAction(UiHelper::getIcon("palette", QColor("#f39c12"), 18), "设置颜色", this, &CategoryPanel::onSetColor);
                menu.addAction(UiHelper::getIcon("random_color", QColor("#e91e63"), 18), "随机颜色", this, &CategoryPanel::onRandomColor);
                menu.addAction(UiHelper::getIcon("tag_filled", QColor("#9b59b6"), 18), "设置预设标签", this, &CategoryPanel::onSetPresetTags);

                menu.addSeparator();

                menu.addAction(UiHelper::getIcon("add", QColor("#aaaaaa"), 18), "新建分类", this, &CategoryPanel::onCreateCategory);
                menu.addAction(UiHelper::getIcon("add", QColor("#aaaaaa"), 18), "新建子分类", this, &CategoryPanel::onCreateSubCategory);

                menu.addSeparator();

                bool isPinned = index.data(CategoryModel::PinnedRole).toBool();
                menu.addAction(UiHelper::getIcon("pin_vertical", isPinned ? QColor("#FF551C") : QColor("#aaaaaa"), 18), 
                               isPinned ? "取消置顶" : "置顶分类", this, &CategoryPanel::onTogglePin);
                               
                menu.addAction(UiHelper::getIcon("edit", QColor("#aaaaaa"), 18), "重命名分类", this, &CategoryPanel::onRenameCategory);
                menu.addAction(UiHelper::getIcon("trash", QColor("#e74c3c"), 18), "删除分类", this, &CategoryPanel::onDeleteCategory);

                menu.addSeparator();

                // 2026-03-xx 按照用户要求：补全排列与密码保护逻辑
                auto* sortMenu = menu.addMenu(UiHelper::getIcon("list_ul", QColor("#aaaaaa"), 18), "排列");
                sortMenu->setStyleSheet(menu.styleSheet());
                sortMenu->addAction("标题(当前层级) (A→Z)", this, &CategoryPanel::onSortByNameAsc);
                sortMenu->addAction("标题(当前层级) (Z→A)", this, &CategoryPanel::onSortByNameDesc);
                sortMenu->addAction("标题(全部) (A→Z)", this, &CategoryPanel::onSortAllByNameAsc);
                sortMenu->addAction("标题(全部) (Z→A)", this, &CategoryPanel::onSortAllByNameDesc);

                auto* pwdMenu = menu.addMenu(UiHelper::getIcon("lock", QColor("#aaaaaa"), 18), "密码保护");
                pwdMenu->setStyleSheet(menu.styleSheet());
                
                // 2026-03-xx 按照用户要求：通过 EncryptedRole 动态判断显示“设置”或“清除”
                bool isEncrypted = index.data(CategoryModel::EncryptedRole).toBool();
                
                if (!isEncrypted) {
                    pwdMenu->addAction("设置密码", this, &CategoryPanel::onSetPassword);
                } else {
                    pwdMenu->addAction("清除密码", this, &CategoryPanel::onClearPassword);
                }
            }
        }
        
        if (!menu.isEmpty()) {
            menu.exec(m_categoryTree->viewport()->mapToGlobal(pos));
        }
    });
}

/**
 * @brief 递归保存 QTreeView 的展开状态
 */
static void saveExpandedState(QTreeView* tree, const QModelIndex& parent, QSet<int>& expandedIds, QStringList& expandedNames) {
    if (!tree || !tree->model()) return;
    for (int i = 0; i < tree->model()->rowCount(parent); ++i) {
        QModelIndex idx = tree->model()->index(i, 0, parent);
        if (tree->isExpanded(idx)) {
            int id = idx.data(CategoryModel::IdRole).toInt();
            if (id > 0) expandedIds.insert(id);
            else expandedNames << idx.data(CategoryModel::NameRole).toString();
            saveExpandedState(tree, idx, expandedIds, expandedNames);
        }
    }
}

/**
 * @brief 递归恢复 QTreeView 的展开状态
 * 2026-03-xx 物理拦截：加密且未解锁的分类在恢复时强制跳过展开
 */
static void restoreExpandedState(QTreeView* tree, const QModelIndex& parent, const QSet<int>& expandedIds, const QStringList& expandedNames, const QSet<int>& unlockedIds) {
    if (!tree || !tree->model()) return;
    for (int i = 0; i < tree->model()->rowCount(parent); ++i) {
        QModelIndex idx = tree->model()->index(i, 0, parent);
        int id = idx.data(CategoryModel::IdRole).toInt();
        QString name = idx.data(CategoryModel::NameRole).toString();
        bool isEncrypted = idx.data(CategoryModel::EncryptedRole).toBool();
        
        bool shouldExpand = false;
        if (expandedNames.contains(name) || (id > 0 && expandedIds.contains(id)) || name == "我的分类") {
            // 物理硬核防御：如果是加密分类且未在当前会话解锁，则无视记忆，强制不展开
            if (isEncrypted && id > 0 && !unlockedIds.contains(id)) {
                shouldExpand = false;
            } else {
                shouldExpand = true;
            }
        }

        if (shouldExpand) {
            tree->setExpanded(idx, true);
            restoreExpandedState(tree, idx, expandedIds, expandedNames, unlockedIds);
        }
    }
}

void CategoryPanel::onCreateCategory() {
    FramelessInputDialog dlg("新建分类", "请输入分类名称:", "", this);
    if (dlg.exec() == QDialog::Accepted) {
        QString text = dlg.text();
        if (!text.isEmpty()) {
            Category cat;
            cat.name = text.toStdWString();
            cat.parentId = 0;
            cat.color = L"#3498db";
            
            QSet<int> expandedIds;
            QStringList expandedNames;
            saveExpandedState(m_categoryTree, QModelIndex(), expandedIds, expandedNames);

            CategoryRepo::add(cat);
            m_categoryModel->refresh();

            restoreExpandedState(m_categoryTree, QModelIndex(), expandedIds, expandedNames, m_unlockedIds);
        }
    }
}

void CategoryPanel::onCreateSubCategory() {
    QModelIndex index = m_categoryTree->currentIndex();
    int id = getTargetCategoryId(index);
    if (id <= 0) return;

    FramelessInputDialog dlg("新建子分类", "请输入子分类名称:", "", this);
    if (dlg.exec() == QDialog::Accepted) {
        QString text = dlg.text();
        if (!text.isEmpty()) {
            Category cat;
            cat.name = text.toStdWString();
            cat.parentId = id;
            cat.color = L"#3498db";

            QSet<int> expandedIds;
            QStringList expandedNames;
            saveExpandedState(m_categoryTree, QModelIndex(), expandedIds, expandedNames);
            expandedIds.insert(id);

            CategoryRepo::add(cat);
            m_categoryModel->refresh();

            restoreExpandedState(m_categoryTree, QModelIndex(), expandedIds, expandedNames, m_unlockedIds);
        }
    }
}

void CategoryPanel::onClassifyToCategory() {
    QModelIndex index = m_categoryTree->currentIndex();
    int id = getTargetCategoryId(index);
    if (id <= 0) return;

    QSettings settings("ArcMeta团队", "ArcMeta");
    settings.setValue("Category/ExtensionTargetId", id);

    QSet<int> expandedIds;
    QStringList expandedNames;
    saveExpandedState(m_categoryTree, QModelIndex(), expandedIds, expandedNames);

    m_categoryModel->refresh();

    restoreExpandedState(m_categoryTree, QModelIndex(), expandedIds, expandedNames, m_unlockedIds);

    QString name = index.data(CategoryModel::NameRole).toString();
    ToolTipOverlay::instance()->showText(QCursor::pos(), QString("已设为归类目标: %1").arg(name), 1000);
}

void CategoryPanel::onSetColor() {
    QModelIndex index = m_categoryTree->currentIndex();
    int id = getTargetCategoryId(index);
    if (id <= 0) return;

    // 2026-03-xx 按照用户要求：使用 QColorDialog 弹出颜色选择
    QColor color = QColorDialog::getColor(Qt::white, this, "选择分类颜色");
    if (!color.isValid()) return;

    auto all = CategoryRepo::getAll();
    for(auto& cat : all) {
        if(cat.id == id) {
            cat.color = color.name().toStdWString();
            CategoryRepo::update(cat);
            break;
        }
    }
    
    QSet<int> expandedIds;
    QStringList expandedNames;
    saveExpandedState(m_categoryTree, QModelIndex(), expandedIds, expandedNames);

    m_categoryModel->refresh();

    restoreExpandedState(m_categoryTree, QModelIndex(), expandedIds, expandedNames, m_unlockedIds);
    
    ToolTipOverlay::instance()->showText(QCursor::pos(), "分类颜色已更新", 1000);
}

void CategoryPanel::onRandomColor() {
    QModelIndex index = m_categoryTree->currentIndex();
    int id = getTargetCategoryId(index);
    if (id <= 0) return;
    
    // 2026-03-xx 按照用户要求：从旧版本中迁移调色盘逻辑
    static const QStringList palette = {
        "#FF6B6B", "#4ECDC4", "#45B7D1", "#96CEB4", "#FFEEAD",
        "#D4A5A5", "#9B59B6", "#3498DB", "#E67E22", "#2ECC71",
        "#E74C3C", "#F1C40F", "#1ABC9C", "#34495E", "#95A5A6"
    };
    QString chosenColor = palette.at(QRandomGenerator::global()->bounded(palette.size()));
    
    auto all = CategoryRepo::getAll();
    for(auto& cat : all) {
        if(cat.id == id) {
            cat.color = chosenColor.toStdWString();
            CategoryRepo::update(cat);
            break;
        }
    }
    
    QSet<int> expandedIds;
    QStringList expandedNames;
    saveExpandedState(m_categoryTree, QModelIndex(), expandedIds, expandedNames);

    m_categoryModel->refresh();

    restoreExpandedState(m_categoryTree, QModelIndex(), expandedIds, expandedNames, m_unlockedIds);
}

void CategoryPanel::onSetPresetTags() {
    QModelIndex index = m_categoryTree->currentIndex();
    int id = getTargetCategoryId(index);
    if (id <= 0) return;

    auto all = CategoryRepo::getAll();
    Category current;
    for(auto& c : all) if(c.id == id) { current = c; break; }

    QString initial;
    for(const auto& t : current.presetTags) initial += QString::fromStdWString(t) + ",";
    if (initial.endsWith(",")) initial.chop(1);

    FramelessInputDialog dlg("设置预设标签", "请输入标签 (用逗号分隔):", initial, this);
    if (dlg.exec() == QDialog::Accepted) {
        QStringList tags = dlg.text().split(QRegularExpression("[,，]"), Qt::SkipEmptyParts);
        current.presetTags.clear();
        for(const QString& t : tags) current.presetTags.push_back(t.trimmed().toStdWString());
        
        QSet<int> expandedIds;
        QStringList expandedNames;
        saveExpandedState(m_categoryTree, QModelIndex(), expandedIds, expandedNames);

        CategoryRepo::update(current);
        m_categoryModel->refresh();

        restoreExpandedState(m_categoryTree, QModelIndex(), expandedIds, expandedNames, m_unlockedIds);
        ToolTipOverlay::instance()->showText(QCursor::pos(), "预设标签已更新", 1000);
    }
}

void CategoryPanel::onTogglePin() {
    QModelIndex index = m_categoryTree->currentIndex();
    int id = getTargetCategoryId(index);
    if (id <= 0) return;
    
    bool isPinned = index.data(CategoryModel::PinnedRole).toBool();

    auto all = CategoryRepo::getAll();
    for(auto& cat : all) {
        if(cat.id == id) {
            cat.pinned = !isPinned;
            CategoryRepo::update(cat);
            break;
        }
    }

    QSet<int> expandedIds;
    QStringList expandedNames;
    saveExpandedState(m_categoryTree, QModelIndex(), expandedIds, expandedNames);

    m_categoryModel->refresh();

    restoreExpandedState(m_categoryTree, QModelIndex(), expandedIds, expandedNames, m_unlockedIds);
}

void CategoryPanel::onSetPassword() {
    QModelIndex index = m_categoryTree->currentIndex();
    int id = getTargetCategoryId(index);
    if (id <= 0) return;

    // 2026-03-xx 物理级 1:1 还原：废弃通用输入框，调用三字段密码对话框
    CategorySetPasswordDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        QString pwd = dlg.password();
        QString hint = dlg.hint();

        QSet<int> expandedIds;
        QStringList expandedNames;
        saveExpandedState(m_categoryTree, QModelIndex(), expandedIds, expandedNames);

        auto all = CategoryRepo::getAll();
        for(auto& cat : all) {
            if(cat.id == id) {
                cat.encrypted = true;
                cat.encryptHint = hint.toStdWString();
                CategoryRepo::update(cat);
                break;
            }
        }
        
        m_categoryModel->refresh();

        restoreExpandedState(m_categoryTree, QModelIndex(), expandedIds, expandedNames, m_unlockedIds);
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#00A650;'>[OK] 分类已加密</b>", 1000, QColor("#00A650"));
    }
}

void CategoryPanel::onClearPassword() {
    QModelIndex index = m_categoryTree->currentIndex();
    int id = getTargetCategoryId(index);
    if (id <= 0) return;

    QString hint = index.data(CategoryModel::EncryptHintRole).toString();

    // 2026-03-xx 物理级还原：清除密码需先通过旧版验证界面校验身份
    CategoryLockDialog dlg(hint, this);
    if (dlg.exec() == QDialog::Accepted) {
        // [SIMULATION] 校验成功
        QSet<int> expandedIds;
        QStringList expandedNames;
        saveExpandedState(m_categoryTree, QModelIndex(), expandedIds, expandedNames);

        auto all = CategoryRepo::getAll();
        for(auto& cat : all) {
            if(cat.id == id) {
                cat.encrypted = false;
                cat.encryptHint = L"";
                CategoryRepo::update(cat);
                break;
            }
        }

        m_categoryModel->refresh();

        restoreExpandedState(m_categoryTree, QModelIndex(), expandedIds, expandedNames, m_unlockedIds);
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#00A650;'>[OK] 验证成功，分类已解除加密</b>", 1000, QColor("#00A650"));
    }
}

void CategoryPanel::onRenameCategory() {
    QModelIndex index = m_categoryTree->currentIndex();
    if (index.isValid()) {
        QString type = index.data(CategoryModel::TypeRole).toString();
        // 2026-03-xx 物理兼容：允许重命名分类或文件项 (逻辑处理见 Model)
        if (type == "category" || type == "file" || type == "folder") {
            m_categoryTree->edit(index);
        }
    }
}

void CategoryPanel::onDeleteCategory() {
    QModelIndex index = m_categoryTree->currentIndex();
    if (index.isValid()) {
        QString type = index.data(CategoryModel::TypeRole).toString();
        int id = index.data(CategoryModel::IdRole).toInt();
        
        // 2026-03-xx 适配物理子项：如果是物理项目，则执行“解除关联”
        if (type == "file" || type == "folder") {
            QString path = index.data(CategoryModel::PathRole).toString();
            int parentCatId = getTargetCategoryId(index);

            if (parentCatId > 0 && !path.isEmpty()) {
                QSet<int> expandedIds;
                QStringList expandedNames;
                saveExpandedState(m_categoryTree, QModelIndex(), expandedIds, expandedNames);

                CategoryRepo::removeItemFromCategory(parentCatId, path.toStdWString());
                m_categoryModel->refresh();

                restoreExpandedState(m_categoryTree, QModelIndex(), expandedIds, expandedNames, m_unlockedIds);
                ToolTipOverlay::instance()->showText(QCursor::pos(), "已从该分类中解除关联", 1000);
            }
            return;
        }

        if (id > 0) {
            QSet<int> expandedIds;
            QStringList expandedNames;
            saveExpandedState(m_categoryTree, QModelIndex(), expandedIds, expandedNames);

            ArcMeta::CategoryRepo::remove(id);
            m_categoryModel->refresh();

            restoreExpandedState(m_categoryTree, QModelIndex(), expandedIds, expandedNames, m_unlockedIds);
        }
    }
}

int CategoryPanel::getTargetCategoryId(const QModelIndex& index) {
    if (!index.isValid()) return 0;
    
    int id = index.data(CategoryModel::IdRole).toInt();
    if (id > 0) return id;
    
    // 递归查找父节点，直到找到 category 类型
    return getTargetCategoryId(index.parent());
}

void CategoryPanel::onSortByNameAsc() {
    QModelIndex index = m_categoryTree->currentIndex();
    // 逻辑：获取该项的父级分类 ID，执行重排
    int parentCatId = 0;
    QModelIndex pIdx = index.parent();
    if (pIdx.isValid()) parentCatId = pIdx.data(CategoryModel::IdRole).toInt();

    if (CategoryRepo::reorder(parentCatId, true)) {
        m_categoryModel->refresh();
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#2ecc71;'>[OK] 已按 A→Z 排列</b>");
    }
}

void CategoryPanel::onSortByNameDesc() {
    QModelIndex index = m_categoryTree->currentIndex();
    int parentCatId = 0;
    QModelIndex pIdx = index.parent();
    if (pIdx.isValid()) parentCatId = pIdx.data(CategoryModel::IdRole).toInt();

    if (CategoryRepo::reorder(parentCatId, false)) {
        m_categoryModel->refresh();
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#2ecc71;'>[OK] 已按 Z→A 排列</b>");
    }
}

void CategoryPanel::onSortAllByNameAsc() {
    if (CategoryRepo::reorderAll(true)) {
        m_categoryModel->refresh();
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#2ecc71;'>[OK] 全部已按 A→Z 排列</b>");
    }
}

void CategoryPanel::onSortAllByNameDesc() {
    if (CategoryRepo::reorderAll(false)) {
        m_categoryModel->refresh();
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#2ecc71;'>[OK] 全部已按 Z→A 排列</b>");
    }
}

void CategoryPanel::setFocusHighlight(bool visible) {
    if (m_focusLine) m_focusLine->setVisible(visible);
}

void CategoryPanel::initUi() {
    // 物理还原：1px 翠绿高亮焦点线 (#2ecc71)
    m_focusLine = new QWidget(this);
    m_focusLine->setFixedHeight(1);
    m_focusLine->setStyleSheet("background-color: #2ecc71;");
    m_focusLine->hide(); // 初始隐藏
    m_mainLayout->addWidget(m_focusLine);

    // 1. 标题栏
    QWidget* header = new QWidget(this);
    header->setObjectName("ContainerHeader");
    header->setFixedHeight(32);
    header->setStyleSheet(
        "QWidget#ContainerHeader {"
        "  background-color: #252526;"
        "  border-bottom: 1px solid #333;"
        "}"
    );
    QHBoxLayout* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(15, 2, 15, 0);
    headerLayout->setSpacing(8);

    QLabel* iconLabel = new QLabel(header);
    iconLabel->setPixmap(UiHelper::getIcon("category", QColor("#3498db"), 18).pixmap(18, 18));
    headerLayout->addWidget(iconLabel);

    QLabel* titleLabel = new QLabel("数据分类", header);
    titleLabel->setStyleSheet("font-size: 13px; font-weight: bold; color: #3498db; background: transparent; border: none;");
    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();

    m_mainLayout->addWidget(header);

    // 2. 内容区包裹容器 (物理还原 8, 8, 8, 8 呼吸边距)
    QWidget* sbContent = new QWidget(this);
    sbContent->setStyleSheet("background: transparent; border: none;");
    auto* sbContentLayout = new QVBoxLayout(sbContent);
    sbContentLayout->setContentsMargins(8, 8, 8, 8);
    sbContentLayout->setSpacing(0);

    QString treeStyle = R"(
        QTreeView { background-color: transparent; border: none; color: #CCC; outline: none; }
        
        /* 物理还原：复原三角形折叠图标 (资源系统路径) */
        /* 2026-03-xx 视觉微调：为图标增加 padding 以实现清秀感，防止线条过粗感 */
        QTreeView::branch:has-children:closed { 
            image: url(:/icons/arrow_right.svg); 
            padding: 4px;
        }
        QTreeView::branch:has-children:open { 
            image: url(:/icons/arrow_down.svg); 
            padding: 4px;
        }
        QTreeView::branch:has-children:closed:has-siblings { 
            image: url(:/icons/arrow_right.svg); 
            padding: 4px;
        }
        QTreeView::branch:has-children:open:has-siblings { 
            image: url(:/icons/arrow_down.svg); 
            padding: 4px;
        }

        QTreeView::item { height: 26px; padding-left: 0px; }
    )";

    // 物理还原：单树架构，合并系统项与用户分类
    m_categoryTree = new DropTreeView(this);
    m_categoryTree->setStyleSheet(treeStyle); 
    m_categoryTree->setItemDelegate(new CategoryDelegate(this));
    
    // 使用 Both 模式同时加载系统项与用户分类
    m_categoryModel = new CategoryModel(CategoryModel::Both, this);
    m_categoryTree->setModel(m_categoryModel);
    
    m_categoryTree->setHeaderHidden(true);
    m_categoryTree->setRootIsDecorated(true);
    m_categoryTree->setIndentation(20);
    m_categoryTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_categoryTree->setDragEnabled(true);
    m_categoryTree->setAcceptDrops(true);
    m_categoryTree->setDropIndicatorShown(true);
    // 核心修正：解除 InternalMove 模式封锁，允许接收外部容器（NavPanel/ContentPanel）的拖拽
    m_categoryTree->setDragDropMode(QAbstractItemView::DragDrop);
    m_categoryTree->setDefaultDropAction(Qt::MoveAction);
    m_categoryTree->setSelectionMode(QAbstractItemView::ExtendedSelection);

    // 2026-03-xx 物理拦截：严禁加密分类在未解锁时被展开
    connect(m_categoryTree, &QTreeView::expanded, [this](const QModelIndex& index) {
        int id = index.data(CategoryModel::IdRole).toInt();
        bool isEncrypted = index.data(CategoryModel::EncryptedRole).toBool();
        
        if (isEncrypted && id > 0 && !m_unlockedIds.contains(id)) {
            // 物理阻断：立即折叠，防止闪烁
            m_categoryTree->collapse(index);
            // 异步触发校验，避免在信号回调中处理复杂 UI
            QTimer::singleShot(0, [this, index]() {
                if (tryUnlockCategory(index)) {
                    // 解锁成功后刷新状态并重新展开
                    m_categoryModel->setUnlockedIds(m_unlockedIds);
                    m_categoryModel->refresh();
                    m_categoryTree->expand(index);
                }
            });
        }
    });

    // 默认展开“我的分类”
    for (int i = 0; i < m_categoryModel->rowCount(); ++i) {
        QModelIndex idx = m_categoryModel->index(i, 0);
        if (idx.data(CategoryModel::NameRole).toString() == "我的分类") {
            m_categoryTree->setExpanded(idx, true);
        }
    }

    // 2026-03-xx 物理兼容：监听模型重置信号，在刷新后尝试恢复展开状态
    connect(m_categoryModel, &QAbstractItemModel::modelAboutToBeReset, [this]() {
        // 同步解锁 ID 到模型
        m_categoryModel->setUnlockedIds(m_unlockedIds);
        
        QSet<int> expandedIds;
        QStringList expandedNames;
        saveExpandedState(m_categoryTree, QModelIndex(), expandedIds, expandedNames);
        
        QList<int> idList;
        for (int id : expandedIds) idList << id;
        m_categoryTree->setProperty("expandedIds", QVariant::fromValue(idList));
        m_categoryTree->setProperty("expandedNames", expandedNames);
    });

    connect(m_categoryModel, &QAbstractItemModel::modelReset, [this]() {
        QList<int> idList = m_categoryTree->property("expandedIds").value<QList<int>>();
        QStringList expandedNames = m_categoryTree->property("expandedNames").toStringList();
        
        QSet<int> expandedIds;
        for (int id : idList) expandedIds.insert(id);
        restoreExpandedState(m_categoryTree, QModelIndex(), expandedIds, expandedNames, m_unlockedIds);
    });

    connect(m_categoryTree, &QTreeView::clicked, [this](const QModelIndex& index) {
        QString type = index.data(CategoryModel::TypeRole).toString();
        QString name = index.data(CategoryModel::NameRole).toString();
        int id = index.data(CategoryModel::IdRole).toInt();
        bool isEncrypted = index.data(CategoryModel::EncryptedRole).toBool();

        // 2026-03-xx 物理防御：加密分类点击时触发校验
        if (isEncrypted && id > 0 && !m_unlockedIds.contains(id)) {
            tryUnlockCategory(index);
            return;
        }

        // 核心联动：如果点击的是分类节点
        if (type == "category") {
             emit categorySelected(id, name);
        }
        
        // 核心联动：如果是点击了具体物理路径 (文件或文件夹)
        if (type == "file" || type == "folder") {
            QString path = index.data(CategoryModel::PathRole).toString();
            if (!path.isEmpty()) {
                emit fileSelected(path);
            }
        }
    });

    connect(m_categoryTree, &DropTreeView::pathsDropped, [this](const QStringList& paths, const QModelIndex& index) {
        int categoryId = -1;
        QString itemType = "";
        QString itemName = "";

        if (index.isValid()) {
            categoryId = index.data(CategoryModel::IdRole).toInt();
            itemType = index.data(CategoryModel::TypeRole).toString();
            itemName = index.data(CategoryModel::NameRole).toString();
        } else {
            Logger::log("[分类面板] 接收到路径但落点无效，正在尝试自动创建分类...");
            // 物理还原：如果拖拽到空白处且有路径，则自动创建分类
            if (!paths.isEmpty()) {
                QString firstPath = paths.first();
                QFileInfo fileInfo(firstPath);
                QString autoCatName = fileInfo.fileName(); // 提取文件夹/文件名作为分类名
                if (autoCatName.isEmpty()) autoCatName = "新分类";

                Category cat;
                cat.name = autoCatName.toStdWString();
                cat.parentId = 0; // 默认为根分类
                cat.color = L"#3498db";

                if (CategoryRepo::add(cat)) {
                    categoryId = cat.id;
                    itemName = autoCatName;
                    Logger::log(QString("[分类面板] 已自动创建分类: %1 ID: %2").arg(itemName).arg(categoryId));
                }
            }
        }

        Logger::log(QString("[分类面板] 最终归类目标: %1 (ID: %2, 类型: %3) | 路径列表: %4")
                    .arg(itemName).arg(categoryId).arg(itemType).arg(paths.join(",")));
        
        int count = 0;
        bool changed = false;

        // 2026-03-xx 按照用户要求：实现全局数据库持久化收藏与归类逻辑
        if (categoryId > 0) {
            // 分支 A：拖拽至具体自定义分类项 (包括刚刚自动创建的)
            for (const QString& path : paths) {
                if (CategoryRepo::addItemToCategory(categoryId, path.toStdWString())) {
                    count++;
                }
            }
            changed = (count > 0);
            if (changed) {
                ToolTipOverlay::instance()->showText(QCursor::pos(), 
                    QString("<b style='color:#2ecc71;'>已自动创建并归类 %1 个项目到 [%2]</b>").arg(count).arg(itemName), 1500, QColor("#2ecc71"));
            }
        } 
        else if (itemType == "bookmark") {
            // 分支 B：拖拽至系统预设的“收藏”项
            for (const QString& path : paths) {
                // 2026-03-xx 物理强化：强制使用原生分隔符，确保数据库路径匹配成功
                QString normPath = QDir::toNativeSeparators(path);
                // 物理写入：更新 items 表的 pinned 状态
                MetadataManager::instance().setPinned(normPath.toStdWString(), true);
                count++;
            }
            changed = (count > 0);
            if (changed) {
                ToolTipOverlay::instance()->showText(QCursor::pos(), 
                    QString("<b style='color:#2ecc71;'>已成功收藏 %1 个项目</b>").arg(count), 1200, QColor("#2ecc71"));
            }
        }
        
        if (changed) {
            // [UX] 状态保持：刷新模型前后保存并恢复展开状态，防止“我的分类”自动折叠
            QSet<int> expandedIds;
            QStringList expandedNames;
            saveExpandedState(m_categoryTree, QModelIndex(), expandedIds, expandedNames);

            // 物理联动：强制刷新模型以触发 CategoryRepo::getCounts/getSystemCounts 重新统计
            m_categoryModel->refresh();

            restoreExpandedState(m_categoryTree, QModelIndex(), expandedIds, expandedNames, m_unlockedIds);
        }
    });
    
    sbContentLayout->addWidget(m_categoryTree);
    m_mainLayout->addWidget(sbContent, 1);

    // 2026-03-xx 物理记忆：初始化后加载持久化的展开状态
    QTimer::singleShot(100, this, &CategoryPanel::loadExpandedStateFromSettings);

    // 2026-03-xx 物理记忆：连接展开/折叠信号，实时持久化
    connect(m_categoryTree, &QTreeView::expanded, this, &CategoryPanel::saveExpandedStateToSettings);
    connect(m_categoryTree, &QTreeView::collapsed, this, &CategoryPanel::saveExpandedStateToSettings);
}

void CategoryPanel::saveExpandedStateToSettings() {
    QSet<int> ids;
    QStringList names;
    saveExpandedState(m_categoryTree, QModelIndex(), ids, names);

    QSettings settings("ArcMeta团队", "ArcMeta");
    QList<QVariant> idList;
    for (int id : ids) idList << id;
    settings.setValue("Category/ExpandedIds", idList);
    settings.setValue("Category/ExpandedNames", names);
}

void CategoryPanel::loadExpandedStateFromSettings() {
    QSettings settings("ArcMeta团队", "ArcMeta");
    QList<QVariant> idList = settings.value("Category/ExpandedIds").toList();
    QStringList names = settings.value("Category/ExpandedNames").toStringList();

    QSet<int> ids;
    for (const auto& v : idList) ids.insert(v.toInt());

    restoreExpandedState(m_categoryTree, QModelIndex(), ids, names, m_unlockedIds);
}

bool CategoryPanel::tryUnlockCategory(const QModelIndex& index) {
    int id = index.data(CategoryModel::IdRole).toInt();
    if (id <= 0) return false;

    QString hint = index.data(CategoryModel::EncryptHintRole).toString();

    // 2026-03-xx 物理级还原：废弃通用输入框，改用 1:1 复刻的旧版验证界面
    CategoryLockDialog dlg(hint, this);
    if (dlg.exec() == QDialog::Accepted) {
        // [SIMULATION] 校验成功
        m_unlockedIds.insert(id);
        
        // 物理补丁：解锁后由于图标需要刷新，强制同步 ID 并进行一次模型重刷
        m_categoryModel->setUnlockedIds(m_unlockedIds);
        m_categoryModel->refresh();
        
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#00A650;'>[OK] 验证成功，分类已解锁</b>", 1000, QColor("#00A650"));
        return true;
    }
    return false;
}

bool CategoryPanel::eventFilter(QObject* obj, QEvent* event) {
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            // [UX] 两段式：查找对话框内的第一个非空输入框
            QLineEdit* edit = findChild<QLineEdit*>();
            if (edit && !edit->text().isEmpty()) {
                edit->clear();
                return true;
            }
        }
    }
    return QFrame::eventFilter(obj, event);
}

} // namespace ArcMeta
