#include "FilterPanel.h"
#include "../core/DatabaseManager.h"
#include "IconHelper.h"
#include "ToolTipOverlay.h"
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPainter>
#include <QApplication>
#include <QTimer>
#include <QtConcurrent>

FilterPanel::FilterPanel(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_StyledBackground, true);
    setMouseTracking(true);
    // 2026-04-xx 按照用户要求：直接照搬侧边栏分类宽度的参数 (163px)
    setMinimumSize(163, 350);
    initUI();
    setupTree();

    connect(&m_statsWatcher, &QFutureWatcher<QVariantMap>::finished, this, &FilterPanel::onStatsReady);
}

void FilterPanel::initUI() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // 内容容器
    auto* contentWidget = new QWidget();
    contentWidget->setStyleSheet(
        "QWidget { "
        "  background-color: transparent; "
        "  border: none; "
        "  border-bottom-left-radius: 0px; "
        "  border-bottom-right-radius: 0px; "
        "}"
    );
    auto* contentLayout = new QVBoxLayout(contentWidget);
    // 2026-04-xx 按照用户要求：压缩高级筛选器内容边距与间距，实现极致精简
    contentLayout->setContentsMargins(0, 5, 0, 5);
    contentLayout->setSpacing(2);

    // 树形筛选器
    m_tree = new QTreeWidget();
    m_tree->setHeaderHidden(true);
    // 2026-04-06 按照用户要求：精准对齐侧边栏缩进参数 (12px)
    m_tree->setIndentation(12);
    m_tree->setFocusPolicy(Qt::NoFocus);
    // 2026-04-06 按照用户要求：显示展开箭头，复刻侧边栏层级感
    m_tree->setRootIsDecorated(true);
    m_tree->setUniformRowHeights(true);
    m_tree->setAnimated(true);
    m_tree->setAllColumnsShowFocus(true);
    m_tree->setStyleSheet(
        "QTreeWidget {"
        "  background-color: transparent;"
        "  color: #ddd;"
        "  border: none;"
        "  font-size: 12px;"
        "}"
        "QTreeWidget::branch:has-children:closed { image: url(:/icons/arrow_right.svg); }"
        "QTreeWidget::branch:has-children:open   { image: url(:/icons/arrow_down.svg); }"
        "QTreeWidget::item {"
        "  height: 22px;" // 2026-04-xx 按照用户要求，同步侧边栏分类高度
        "  border-radius: 4px;"
        "  margin-left: 0px;" // 2026-04-06 按照用户要求：移除冗余间距，实现极致精简
        "  margin-right: 0px;"
        "  padding: 0px;"
        "}"
        "QTreeWidget::item:hover { background-color: #2a2d2e; }" // 2026-04-xx 按照用户要求，同步侧边栏悬停色
        "QTreeWidget::item:selected { background-color: #2a2d2e; color: white; }" 
        "QTreeWidget::indicator {"
        "  width: 14px;"
        "  height: 14px;"
        "  margin-left: 8px;"
        "}"
        "QScrollBar:vertical { border: none; background: transparent; width: 6px; margin: 0px; }"
        "QScrollBar::handle:vertical { background: #444; border-radius: 3px; min-height: 20px; }"
        "QScrollBar::handle:vertical:hover { background: #555; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }"
    );
    connect(m_tree, &QTreeWidget::itemChanged, this, &FilterPanel::onItemChanged);
    connect(m_tree, &QTreeWidget::itemClicked, this, &FilterPanel::onItemClicked);
    m_tree->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    contentLayout->addWidget(m_tree);

    // 底部区域
    auto* bottomLayout = new QHBoxLayout();
    // 2026-04-xx 按照用户要求：极致压缩控制栏高度，顶边距设为 0 使其紧贴列表
    bottomLayout->setContentsMargins(0, 0, 0, 2);
    bottomLayout->setSpacing(8);

    bottomLayout->addStretch(); // 左侧弹簧

    // 2026-04-xx 按照用户要求：标准化按钮样式，移除文字，仅保留图标
    QString btnStyle = "QPushButton { background: transparent; border: none; border-radius: 4px; padding: 0px; } "
                       "QPushButton:hover { background-color: #3e3e42; }";

    m_btnReset = new QPushButton();
    m_btnReset->setIcon(IconHelper::getIcon("refresh", "#aaaaaa", 18));
    m_btnReset->setFixedSize(24, 24);
    m_btnReset->setStyleSheet(btnStyle);
    // 2026-04-06 按照用户要求：物理移除错误的 F5 快捷键提示，F5 仅限用于刷新数据
    m_btnReset->setProperty("tooltipText", "重置所有筛选条件"); m_btnReset->installEventFilter(this);
    connect(m_btnReset, &QPushButton::clicked, this, &FilterPanel::resetFilters);
    bottomLayout->addWidget(m_btnReset);

    auto* btnCollapse = new QPushButton();
    btnCollapse->setIcon(IconHelper::getIcon("chevrons_up", "#aaaaaa", 18));
    btnCollapse->setFixedSize(24, 24);
    btnCollapse->setStyleSheet(btnStyle);
    // 2026-04-xx 按照宪法规范：补全提示文本。注：Ctrl + G 为全局切换逻辑，此处仅保留功能描述以防歧义
    btnCollapse->setProperty("tooltipText", "全部折叠"); btnCollapse->installEventFilter(this);
    connect(btnCollapse, &QPushButton::clicked, this, [this](){
        for(auto* root : m_roots) root->setExpanded(false);
    });
    bottomLayout->addWidget(btnCollapse);

    auto* btnExpand = new QPushButton();
    btnExpand->setIcon(IconHelper::getIcon("chevrons_down", "#aaaaaa", 18));
    btnExpand->setFixedSize(24, 24);
    btnExpand->setStyleSheet(btnStyle);
    // 2026-04-xx 按照宪法规范：补全提示文本
    btnExpand->setProperty("tooltipText", "全部展开"); btnExpand->installEventFilter(this);
    connect(btnExpand, &QPushButton::clicked, this, [this](){
        for(auto* root : m_roots) root->setExpanded(true);
    });
    bottomLayout->addWidget(btnExpand);

    bottomLayout->addStretch();

    contentLayout->addLayout(bottomLayout);
    mainLayout->addWidget(contentWidget);
}

void FilterPanel::setupTree() {
    struct Section {
        QString key;
        QString label;
        QString icon;
        QString color;
    };

    // 2026-04-xx 按照用户要求：物理位置跟在评级下方，图标语意化
    QList<Section> sections = {
        {"stars", "评级", "star_filled", "#f39c12"},
        {"word_count", "字数", "type", "#3498db"},
        {"date_create", "创建日期", "today", "#2ecc71"},
        {"date_update", "修改日期", "clock", "#9b59b6"},
        {"colors", "颜色", "palette", "#e91e63"},
        {"types", "类型", "folder", "#3498db"},
        {"tags", "标签", "tag", "#e67e22"}
    };

    QFont headerFont = m_tree->font();
    headerFont.setBold(true);

    for (const auto& sec : sections) {
        auto* item = new QTreeWidgetItem(m_tree);
        item->setText(0, sec.label);
        item->setIcon(0, IconHelper::getIcon(sec.icon, sec.color));
        item->setExpanded(true);
        item->setFlags(Qt::ItemIsEnabled);
        item->setFont(0, headerFont);
        item->setForeground(0, QBrush(Qt::gray));
        m_roots[sec.key] = item;
    }
}

void FilterPanel::updateStats(const QString& keyword, const QString& type, const QVariant& value) {
    // [PERF] 性能优化：将耗时的 FTS5 聚合统计移至后台线程，防止搜索时 UI 线程假死。
    if (m_statsWatcher.isRunning()) {
        m_statsWatcher.cancel();
    }

    m_pendingKeyword = keyword;
    m_pendingType = type;
    m_pendingValue = value;

    auto future = QtConcurrent::run([keyword, type, value]() {
        return DatabaseManager::instance().getFilterStats(keyword, type, value);
    });
    m_statsWatcher.setFuture(future);
}

void FilterPanel::onStatsReady() {
    QVariantMap stats = m_statsWatcher.result();
    if (stats.isEmpty()) return;

    m_tree->blockSignals(true);
    m_blockItemClick = true;

    // 1. 评级
    // 2026-04-xx 按照用户要求：重构评级排序顺序，无星级置顶，随后1-5星升序
    QList<QVariantMap> starData;
    QVariantMap starStats = stats["stars"].toMap();
    
    // 无评级优先
    if (starStats.contains("0")) {
        int count = starStats["0"].toInt();
        if (count > 0) {
            QVariantMap item;
            item["key"] = "0";
            item["label"] = "无评级";
            item["count"] = count;
            starData.append(item);
        }
    }
    
    // 1-5星升序
    for (int i = 1; i <= 5; ++i) {
        int count = starStats[QString::number(i)].toInt();
        if (count > 0) {
            QVariantMap item;
            item["key"] = QString::number(i);
            item["label"] = QString(i, QChar(0x2605)); // ★
            item["count"] = count;
            starData.append(item);
        }
    }
    refreshNode("stars", starData);

    // 1.5 字数区间展示 (2026-04-xx 按照用户要求：极致精准展示)
    QList<QVariantMap> wordData;
    QVariantMap wordStats = stats["word_count"].toMap();
    // 按照 10, 20, ..., 101 的顺序强制物理遍历，确保 UI 排序的绝对有序性
    for (int i = 10; i <= 110; i += 10) {
        QString key = (i == 110) ? "101" : QString::number(i);
        if (wordStats.contains(key)) {
            int count = wordStats[key].toInt();
            if (count > 0) {
                QVariantMap item;
                item["key"] = key;
                QString label;
                if (i == 10) label = "0-10 字";
                else if (i == 110) label = "101 字以上";
                else label = QString("%1-%2 字").arg(i - 9).arg(i);
                item["label"] = label;
                item["count"] = count;
                wordData.append(item);
            }
        }
    }
    refreshNode("word_count", wordData);

    // 2. 颜色
    QList<QVariantMap> colorData;
    QVariantMap colorStats = stats["colors"].toMap();
    for (auto it = colorStats.begin(); it != colorStats.end(); ++it) {
        int count = it.value().toInt();
        if (count > 0) {
            QVariantMap item;
            item["key"] = it.key();
            item["label"] = it.key();
            item["count"] = count;
            colorData.append(item);
        }
    }
    refreshNode("colors", colorData, true);

    // 3. 业务类型与子后缀 (2026-04-06 按照用户要求：三级树形精细化展示)
    QList<QVariantMap> typeData;
    QVariantMap typeStats = stats["types"].toMap();
    const QStringList displayOrder = {"音频", "视频", "图形/图像", "程序/脚本", "文档", "其他"};
    
    for (const QString& catLabel : displayOrder) {
        if (typeStats.contains(catLabel)) {
            QVariantMap extStats = typeStats[catLabel].toMap();
            int totalCount = 0;
            QList<QVariantMap> children;
            
            // 提取各后缀数据
            QStringList exts = extStats.keys();
            exts.sort();
            for (const QString& ext : exts) {
                int c = extStats[ext].toInt();
                totalCount += c;
                QVariantMap child;
                child["key"] = catLabel + "|" + ext;
                child["label"] = ext;
                child["count"] = c;
                children.append(child);
            }

            if (totalCount > 0) {
                QVariantMap item;
                item["key"] = catLabel;
                item["label"] = catLabel;
                item["count"] = totalCount;
                item["children"] = QVariant::fromValue(children);
                typeData.append(item);
            }
        }
    }
    refreshNode("types", typeData);

    // 4. 标签
    QList<QVariantMap> tagData;
    QVariantMap tagStats = stats["tags"].toMap();
    for (auto it = tagStats.begin(); it != tagStats.end(); ++it) {
        QVariantMap item;
        item["key"] = it.key();
        item["label"] = it.key();
        item["count"] = it.value().toInt();
        tagData.append(item);
    }
    refreshNode("tags", tagData);

    // 5. 创建日期与修改日期辅助逻辑
    QDate today = QDate::currentDate();
    auto processDateStats = [&](const QString& key, const QString& statsKey) {
        QList<QVariantMap> dateData;
        QVariantMap dateStats = stats[statsKey].toMap();
        QStringList sortedDates = dateStats.keys();
        std::sort(sortedDates.begin(), sortedDates.end(), std::greater<QString>());

        for (const QString& dateStr : sortedDates) {
            QDate date = QDate::fromString(dateStr, "yyyy-MM-dd");
            QString label;
            qint64 daysTo = date.daysTo(today);
            if (daysTo == 0) label = "今天";
            else if (daysTo == 1) label = "昨天";
            else if (daysTo == 2) label = "2 天前";
            else label = date.toString("yyyy/M/d");

            QVariantMap item;
            item["key"] = dateStr;
            item["label"] = label;
            item["count"] = dateStats[dateStr].toInt();
            dateData.append(item);
        }
        refreshNode(key, dateData);
    };

    processDateStats("date_create", "date_create");
    processDateStats("date_update", "date_update");

    m_blockItemClick = false;
    m_tree->blockSignals(false);
}

void FilterPanel::refreshNode(const QString& key, const QList<QVariantMap>& items, bool isCol) {
    if (!m_roots.contains(key)) return;
    auto* root = m_roots[key];
    bool isTypeNode = (key == "types");

    // 建立现有的 key -> item 映射
    QMap<QString, QTreeWidgetItem*> existingItems;
    for (int i = 0; i < root->childCount(); ++i) {
        auto* child = root->child(i);
        existingItems[child->data(0, Qt::UserRole).toString()] = child;
    }

    QSet<QString> currentKeys;
    for (int i = 0; i < items.size(); ++i) {
        const auto& data = items[i];
        QString itemKey = data["key"].toString();
        QString label = data["label"].toString();
        int count = data["count"].toInt();
        currentKeys.insert(itemKey);

        QString newText = QString("%1 (%2)").arg(label).arg(count);
        QTreeWidgetItem* child = nullptr;

        if (existingItems.contains(itemKey)) {
            child = existingItems[itemKey];
            if (child->text(0) != newText) {
                child->setText(0, newText);
            }
            
            // 2026-04-xx 按照用户要求：物理级修复评级排序逻辑，确保项的显示顺序与统计列表严格一致
            int currentIndex = root->indexOfChild(child);
            if (currentIndex != i) {
                root->takeChild(currentIndex);
                root->insertChild(i, child);
            }
        } else {
            child = new QTreeWidgetItem();
            child->setText(0, newText);
            child->setData(0, Qt::UserRole, itemKey);
            child->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            child->setCheckState(0, Qt::Unchecked);
            
            if (isCol) {
                child->setIcon(0, IconHelper::getIcon("circle_filled", itemKey));
            }
            root->insertChild(i, child);
            
            // 2026-04-06 按照用户要求：业务大类默认展开，让具体后缀子选项直接呈现
            if (isTypeNode) child->setExpanded(true);
        }

        // [NEW] 处理三级后缀子项
        if (isTypeNode && data.contains("children")) {
            QList<QVariantMap> childData = data["children"].value<QList<QVariantMap>>();
            QSet<QString> currentChildKeys;
            
            for (int j = 0; j < childData.size(); ++j) {
                const auto& cData = childData[j];
                QString cKey = cData["key"].toString();
                QString cText = QString("%1 (%2)").arg(cData["label"].toString()).arg(cData["count"].toInt());
                currentChildKeys.insert(cKey);

                QTreeWidgetItem* subChild = nullptr;
                for (int k = 0; k < child->childCount(); ++k) {
                    if (child->child(k)->data(0, Qt::UserRole).toString() == cKey) {
                        subChild = child->child(k);
                        break;
                    }
                }

                if (subChild) {
                    if (subChild->text(0) != cText) subChild->setText(0, cText);
                } else {
                    subChild = new QTreeWidgetItem(child);
                    subChild->setText(0, cText);
                    subChild->setData(0, Qt::UserRole, cKey);
                    subChild->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
                    subChild->setCheckState(0, Qt::Unchecked);
                }
            }

            // 移除多余子项
            for (int j = child->childCount() - 1; j >= 0; --j) {
                if (!currentChildKeys.contains(child->child(j)->data(0, Qt::UserRole).toString())) {
                    delete child->takeChild(j);
                }
            }
        }
    }

    // 移除不再需要的项目
    for (int i = root->childCount() - 1; i >= 0; --i) {
        auto* child = root->child(i);
        if (!currentKeys.contains(child->data(0, Qt::UserRole).toString())) {
            delete root->takeChild(i);
        }
    }
}


QVariantMap FilterPanel::getCheckedCriteria() const {
    QVariantMap criteria;
    for (auto it = m_roots.begin(); it != m_roots.end(); ++it) {
        QStringList checked;
        QTreeWidgetItem* root = it.value();
        for (int i = 0; i < root->childCount(); ++i) {
            auto* item = root->child(i);
            if (item->checkState(0) == Qt::Checked) {
                checked << item->data(0, Qt::UserRole).toString();
            } else if (item->checkState(0) == Qt::PartiallyChecked) {
                // 如果是部分选中，搜集下属已选中的具体后缀
                for (int j = 0; j < item->childCount(); ++j) {
                    if (item->child(j)->checkState(0) == Qt::Checked) {
                        checked << item->child(j)->data(0, Qt::UserRole).toString();
                    }
                }
            }
        }
        if (!checked.isEmpty()) {
            criteria[it.key()] = checked;
        }
    }
    return criteria;
}

void FilterPanel::resetFilters() {
    m_tree->blockSignals(true);
    for (auto* root : m_roots) {
        for (int i = 0; i < root->childCount(); ++i) {
            auto* item = root->child(i);
            item->setCheckState(0, Qt::Unchecked);
            // 2026-04-06 按照用户要求：同步重置所有三级子选项
            for (int j = 0; j < item->childCount(); ++j) {
                item->child(j)->setCheckState(0, Qt::Unchecked);
            }
        }
    }
    m_tree->blockSignals(false);
    emit filterChanged();
}

void FilterPanel::onItemChanged(QTreeWidgetItem* item, int column) {
    if (m_blockItemClick) return;
    
    // 记录最近改变的项，用于防止 onItemClicked 重复处理
    m_lastChangedItem = item;
    QTimer::singleShot(100, [this]() { m_lastChangedItem = nullptr; });
    
    emit filterChanged();
}

void FilterPanel::onItemClicked(QTreeWidgetItem* item, int column) {
    if (!item) return;

    // 如果该项刚刚由 Qt 原生机制改变了状态（点击了复选框），则忽略此次点击事件
    if (m_lastChangedItem == item) return;

    // 2026-04-06 按照用户要求：点击任何带子项的节点均执行展开/折叠切换
    if (item->childCount() > 0) {
        item->setExpanded(!item->isExpanded());
    }

    if (item->flags() & Qt::ItemIsUserCheckable) {
        m_blockItemClick = true;
        
        // 1. 切换自身状态
        Qt::CheckState newState = (item->checkState(0) == Qt::Checked) ? Qt::Unchecked : Qt::Checked;
        item->setCheckState(0, newState);

        // 2. 递归更新子项 (全选/全取消)
        for (int i = 0; i < item->childCount(); ++i) {
            item->child(i)->setCheckState(0, newState);
        }

        // 3. 递归更新父项状态
        QTreeWidgetItem* p = item->parent();
        while (p && p->parent() != nullptr) { // 排除顶级组根节点
            int checkedCount = 0;
            for (int i = 0; i < p->childCount(); ++i) {
                if (p->child(i)->checkState(0) == Qt::Checked) checkedCount++;
            }
            if (checkedCount == 0) p->setCheckState(0, Qt::Unchecked);
            else if (checkedCount == p->childCount()) p->setCheckState(0, Qt::Checked);
            else p->setCheckState(0, Qt::PartiallyChecked);
            p = p->parent();
        }

        m_blockItemClick = false;
        emit filterChanged();
    }
}

bool FilterPanel::eventFilter(QObject* watched, QEvent* event) {
    // 2026-04-xx 修正：物理级拦截并重定向 ToolTip 到项目指定的 ToolTipOverlay
    if (event->type() == QEvent::ToolTip) {
        QString text = watched->property("tooltipText").toString();
        if (!text.isEmpty()) {
            ToolTipOverlay::instance()->showText(QCursor::pos(), text, 2000);
        }
        return true; 
    }
    return QWidget::eventFilter(watched, event);
}

void FilterPanel::toggleAllGroups() {
    // 2026-04-xx 按照用户要求：快捷键触发各组折叠/展开切换（循环逻辑）
    if (m_roots.isEmpty()) return;
    
    bool anyCollapsed = false;
    for (auto* root : m_roots) {
        if (!root->isExpanded()) {
            anyCollapsed = true;
            break;
        }
    }
    
    // 如果有任何一个组是折叠的，则全部展开；否则全部折叠
    for (auto* root : m_roots) {
        root->setExpanded(anyCollapsed);
    }
}
