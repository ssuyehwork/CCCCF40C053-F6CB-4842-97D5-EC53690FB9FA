#include "HelpWindow.h"
#include "StringUtils.h"

HelpWindow::HelpWindow(QWidget* parent) : FramelessDialog("使用说明", parent) {
    // [用户修改要求] 重构为双栏布局，调高窗口高度并隐藏不再需要的最大化按钮。
    setObjectName("HelpWindow");
    loadWindowSettings();
    setFixedSize(650, 750); // 进一步调高窗口高度
    if (m_maxBtn) m_maxBtn->hide(); // 根据用户要求，不需要最大化功能
    setupData();
    initUI();
}

void HelpWindow::setupData() {
    auto add = [&](const QString& cat, const QString& key, const QString& desc) {
        m_helpData.append({cat, key, desc});
    };

    add("全局", "Alt + Space", "呼出/隐藏快速窗口");
    add("全局", "Ctrl + Shift + E", "全局一键收藏 (将最后捕获内容存入收藏夹)");
    add("全局", "Alt + X", "全能截屏 (支持识图、标注、贴图)");
    add("全局", "Alt + C", "截图取文 (OCR)");
    add("全局", "Ctrl + S", "浏览器智能采集 (仅在浏览器活跃时生效)");
    add("全局", "Ctrl + Shift + L", "全局应用锁定");
    add("全局", "Ctrl + Shift + V", "纯文本粘贴");
    add("全局", "Ctrl + Shift + T", "全局呼出工具箱");

    add("快速笔记", "Enter / 双击", "选中项自动上屏 (粘贴至目标软件)");
    add("快速笔记", "Space", "快速预览内容");
    add("快速笔记", "Ctrl + F", "聚焦搜索框");
    add("快速笔记", "Ctrl + E", "切换收藏状态");
    add("快速笔记", "Ctrl + P", "置顶/取消置顶项目");
    add("快速笔记", "Ctrl + S", "锁定/解锁单条记录");
    add("快速笔记", "Ctrl + B", "编辑选中项");
    add("快速笔记", "Ctrl + N", "新建灵感笔记");
    add("快速笔记", "Ctrl + C", "提取纯文本内容");
    add("快速笔记", "Delete", "移至回收站");
    add("快速笔记", "Shift + Delete", "彻底删除项目");
    add("快速笔记", "Alt + Up / Down", "手动调整项目排序");
    add("快速笔记", "PgUp / PgDn", "列表翻页 (上一页/下一页)");
    add("快速笔记", "Alt + Q", "开启/关闭分类侧边栏");
    add("快速笔记", "Alt + D", "切换窗口始终最前");
    add("快速笔记", "Alt + W", "快速切换至主管理模式");
    add("快速笔记", "F5", "刷新数据列表");
    add("快速笔记", "Ctrl + 1~5", "设置评分 (0为取消)");
    add("快速笔记", "Ctrl + Shift + C/V", "批量复制/粘贴标签");

    add("主界面", "Ctrl + G", "开启高级筛选面板");
    add("主界面", "Ctrl + I", "开启元数据/批量面板");
    add("主界面", "F5", "刷新数据列表");
    add("主界面", "Ctrl + S", "保存当前修改或锁定分类");

    add("编辑器", "Ctrl + S", "保存修改");
    add("编辑器", "Ctrl + F", "内容内查找");
    add("编辑器", "Ctrl + W", "关闭当前窗口");
}

void HelpWindow::initUI() {
    auto* layout = new QHBoxLayout(m_contentArea);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // 左侧列表
    m_listWidget = new QListWidget();
    m_listWidget->setFixedWidth(220);
    m_listWidget->setStyleSheet(R"(
        QListWidget {
            background-color: #252526;
            border: none;
            border-right: 1px solid #333;
            color: #BBB;
            outline: none;
            padding: 5px;
        }
        QListWidget::item {
            height: 36px;
            padding-left: 10px;
            border-radius: 4px;
            margin: 2px 5px;
        }
        QListWidget::item:hover {
            background-color: #2D2D2D;
        }
        QListWidget::item:selected {
            background-color: #094771;
            color: white;
        }
    )");

    // 右侧详情
    m_detailView = new QTextBrowser();
    m_detailView->setFrameShape(QFrame::NoFrame);
    m_detailView->setStyleSheet(R"(
        QTextBrowser {
            background-color: #1e1e1e;
            color: #DDD;
            padding: 30px;
            line-height: 1.6;
            border: none;
        }
    )");

    layout->addWidget(m_listWidget);
    layout->addWidget(m_detailView);

    // 填充数据并添加分类标题
    QString currentCat;
    for (int i = 0; i < m_helpData.size(); ++i) {
        const auto& item = m_helpData[i];
        if (item.category != currentCat) {
            currentCat = item.category;
            auto* label = new QListWidgetItem(currentCat);
            label->setFlags(Qt::NoItemFlags);
            label->setForeground(QColor("#4FACFE"));
            label->setFont(QFont("Microsoft YaHei", 9, QFont::Bold));
            m_listWidget->addItem(label);
        }

        auto* listItem = new QListWidgetItem(item.key);
        listItem->setData(Qt::UserRole, i);
        m_listWidget->addItem(listItem);
    }

    connect(m_listWidget, &QListWidget::itemClicked, this, &HelpWindow::onItemSelected);

    // 默认选中第一项有效热键
    for (int i = 0; i < m_listWidget->count(); ++i) {
        if (m_listWidget->item(i)->flags() & Qt::ItemIsSelectable) {
            m_listWidget->setCurrentRow(i);
            onItemSelected(m_listWidget->item(i));
            break;
        }
    }
}

void HelpWindow::onItemSelected(QListWidgetItem* item) {
    if (!item || !(item->flags() & Qt::ItemIsSelectable)) return;

    int idx = item->data(Qt::UserRole).toInt();
    const auto& data = m_helpData[idx];

    QString html = QString(R"(
        <div style='margin-bottom: 20px;'>
            <span style='color: #4FACFE; font-weight: bold;'>[%1]</span>
        </div>
        <h1 style='color: #F1C40F; font-size: 24px; margin-bottom: 20px;'>%2</h1>
        <div style='color: #DDD; font-size: 14px; line-height: 1.8; border-top: 1px solid #333; padding-top: 20px;'>
            %3
        </div>
    )").arg(data.category).arg(data.key).arg(data.description);

    m_detailView->setHtml(html);
}
