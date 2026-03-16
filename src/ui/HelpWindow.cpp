#include "HelpWindow.h"
#include "StringUtils.h"
#include <algorithm>

HelpWindow::HelpWindow(QWidget* parent) : FramelessDialog("使用说明", parent) {
    // [用户修改要求] 重构为双栏布局（左侧热键，右侧详情），调高高度并隐藏最大化按钮。
    setObjectName("HelpWindow");
    loadWindowSettings();
    setFixedSize(650, 750); // 调高窗口高度
    if (m_maxBtn) m_maxBtn->hide(); // 隐藏最大化按钮
    setupData();
    initUI();
}

void HelpWindow::setupData() {
    auto add = [&](const QString& key, const QString& desc) {
        m_helpData.append({key, desc});
    };

    // --- 全局热键 ---
    add("Alt + Space", "呼出/隐藏快速窗口");
    add("Alt + X", "全能截屏 (支持识图、标注、贴图)");
    add("Alt + C", "截图取文 (OCR)");
    add("Ctrl + S", "浏览器智能采集 (仅在浏览器活跃时生效)");
    add("Ctrl + Shift + E", "全局一键收藏 (捕获最后一次内容)");
    add("Ctrl + Shift + V", "纯文本粘贴 (由本项目处理的纯净粘贴)");
    add("Ctrl + Shift + Alt + S", "全局应用锁定");
    add("Ctrl + Shift + T", "全局呼出工具箱");

    // --- 快速窗口内部快捷键 ---
    add("Enter / 双击", "选中项自动上屏 (粘贴至目标软件)");
    add("Space", "快速预览内容");
    add("Ctrl + F", "聚焦搜索框");
    add("Ctrl + E", "切换收藏状态");
    add("Ctrl + P", "项目置顶/取消置顶");
    add("Ctrl + L", "锁定/解锁单条项目");
    add("Ctrl + B", "编辑选中项");
    add("Ctrl + N", "新建灵感笔记");
    add("Ctrl + C", "提取纯文本内容 (排除标题干扰)");
    add("Delete", "移至回收站");
    add("Shift + Delete", "彻底删除项目");
    add("F4", "重复上一次操作 (如批量移动、粘贴标签)");
    add("F5", "刷新数据列表");
    add("PgUp / PgDn", "列表翻页 (上一页/下一页)");
    add("Alt + Q", "显示/隐藏侧边栏");
    add("Alt + D", "切换窗口始终最前");
    add("Alt + W", "快速切换至主管理模式");
    add("Ctrl + 1~5", "设置评分 (0 为取消)");
    add("Ctrl + Alt + C/V", "批量复制/粘贴标签");
    add("Ctrl + Shift + A", "一键切换至‘全部数据’视图");
    add("波浪键 (~) / Backspace", "一键归位 (归位至‘全部数据’视图)");
    add("Tab", "在列表与侧边栏之间切换焦点");
    add("1 / 2", "列表快速跳转至首位/末位");
    add("3 / 4", "列表上下导航 (支持跨页回环)");
    add("F2", "重命名 (在列表中重命名标题，在侧边栏重命名分类)");
    add("Alt + Up / Down", "手动调整排序 (项目或分类)");
    add("Ctrl + W", "关闭当前窗口");

    // --- 主管理模式/编辑器补充 ---
    add("Ctrl + G", "开启高级筛选面板 (主窗口)");
    add("Ctrl + I", "开启元数据/批量面板 (主窗口)");
    add("Ctrl + S", "保存编辑器修改或锁定当前分类");

    // [用户修改要求] 2026-03-xx 按照字母顺序对所有快捷键进行全局排序，移除所有分组标题
    std::sort(m_helpData.begin(), m_helpData.end(), [](const HelpItem& a, const HelpItem& b) {
        return a.key.toLower() < b.key.toLower();
    });
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

    // [用户修改要求] 全局字母顺序排列，移除分类分组
    for (int i = 0; i < m_helpData.size(); ++i) {
        auto* listItem = new QListWidgetItem(m_helpData[i].key);
        listItem->setData(Qt::UserRole, i);
        m_listWidget->addItem(listItem);
    }

    connect(m_listWidget, &QListWidget::itemClicked, this, &HelpWindow::onItemSelected);

    // 默认选中第一项
    if (m_listWidget->count() > 0) {
        m_listWidget->setCurrentRow(0);
        onItemSelected(m_listWidget->item(0));
    }
}

void HelpWindow::onItemSelected(QListWidgetItem* item) {
    if (!item) return;

    int idx = item->data(Qt::UserRole).toInt();
    const auto& data = m_helpData[idx];

    // [用户修改要求] 详情页同样移除多余的分类显示，仅展示热键及用途描述
    QString html = QString(R"(
        <h1 style='color: #F1C40F; font-size: 24px; margin-bottom: 20px; font-family: Consolas, monospace;'>%1</h1>
        <div style='color: #DDD; font-size: 14px; line-height: 1.8; border-top: 1px solid #333; padding-top: 20px;'>
            %2
        </div>
    )").arg(data.key).arg(data.description);

    m_detailView->setHtml(html);
}
