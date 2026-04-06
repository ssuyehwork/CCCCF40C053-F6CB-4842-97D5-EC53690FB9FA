#include "HelpWindow.h"
#include "StringUtils.h"
#include <QVBoxLayout>
#include <QScrollArea>

HelpWindow::HelpWindow(QWidget* parent) : FramelessDialog("使用说明", parent) {
    setFixedSize(500, 600);
    initUI();
}

void HelpWindow::initUI() {
    auto* layout = new QVBoxLayout(m_contentArea);
    layout->setContentsMargins(15, 5, 15, 15);
    layout->setSpacing(0);

    QScrollArea* scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet("QScrollArea { background: transparent; } "
                          "QScrollBar:vertical { width: 8px; background: transparent; } "
                          "QScrollBar::handle:vertical { background: #555; border-radius: 4px; } "
                          "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }");

    QLabel* contentLabel = new QLabel();
    contentLabel->setWordWrap(true);
    contentLabel->setTextFormat(Qt::RichText);
    contentLabel->setText(getHelpHtml());
    contentLabel->setStyleSheet("color: #DDD; line-height: 1.6; font-size: 13px;");
    contentLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    scroll->setWidget(contentLabel);
    layout->addWidget(scroll);
}

QString HelpWindow::getHelpHtml() {
    return R"html(
<style>
    h2 { color: #4FACFE; margin-top: 20px; border-bottom: 1px solid #444; padding-bottom: 5px; font-size: 16px; }
    b { color: #F1C40F; }
    .key { background: #444; padding: 2px 6px; border-radius: 4px; font-family: 'Segoe UI', monospace; color: #FFF; font-weight: bold; }
    .section { margin-bottom: 25px; }
    ul { margin-left: -15px; list-style-type: none; }
    li { margin-bottom: 10px; }
    .sub-title { color: #1abc9c; font-weight: bold; margin-top: 10px; display: block; }
</style>

<div class="section">
    <h2>一、全局热键 (随时随地使用)</h2>
    <ul>
        <li><span class="key">Alt + Space</span> : 唤起/隐藏<b>极速窗口</b></li>
        <li><span class="key">Ctrl + Shift + S</span> : <b>智能采集</b> (自动提取浏览器选中的标题与正文)</li>
        <li><span class="key">Ctrl + Alt + A</span> : <b>全局截屏</b> (内置 OCR、画笔、马赛克、贴图)</li>
        <li><span class="key">Ctrl + Shift + E</span> : 快速<b>收藏</b>最后一条自动捕获的剪贴板内容</li>
    </ul>
</div>

<div class="section">
    <h2>二、极速窗口 (核心操作)</h2>
    <span class="sub-title">基础交互</span>
    <ul>
        <li><b>即写即存</b>：输入文字后按 <span class="key">Enter</span> 直接保存为新灵感</li>
        <li><b>极速上屏</b>：双击列表项或按 <span class="key">Enter</span> 自动粘贴内容到目标窗口</li>
        <li><span class="key">Esc</span> / <span class="key">Ctrl + W</span> : 隐藏窗口</li>
        <li><span class="key">Alt + W</span> : 切换到 <b>主管理界面</b></li>
        <li><span class="key">Alt + D</span> : 切换窗口 <b>置顶状态</b></li>
        <li><span class="key">Ctrl + Shift + T</span> : 呼出 <b>工具箱</b></li>
        <li><span class="key">Ctrl + Q</span> : 展开/收起 <b>侧边分类栏</b></li>
    </ul>
    
    <span class="sub-title">列表管理</span>
    <ul>
        <li><span class="key">Ctrl + F</span> : 快速定位并选中搜索框</li>
        <li><span class="key">Ctrl + E</span> : 收藏/取消收藏选中项</li>
        <li><span class="key">Ctrl + P</span> : 置顶/取消置顶选中项</li>
        <li><span class="key">Ctrl + S</span> : 锁定该项 (防止在未解锁时预览内容)</li>
        <li><span class="key">Ctrl + Shift + L</span> : 立即锁定当前所在的分类</li>
        <li><span class="key">Ctrl + B</span> : 进入 <b>详情编辑</b> 模式</li>
        <li><span class="key">Ctrl + T</span> : 对选中图片进行 <b>文字识别 (OCR)</b></li>
        <li><span class="key">Ctrl + 1 ~ 5</span> : 为灵感快速打分 (1-5星)</li>
        <li><span class="key">Alt + S / X</span> : 列表 <b>上一页 / 下一页</b></li>
        <li><span class="key">Ctrl + A</span> : 全选列表条目</li>
        <li><span class="key">Del</span> : 移至回收站 | <span class="key">Ctrl + Shift + Del</span> : 永久删除</li>
    </ul>
</div>

<div class="section">
    <h2>三、主界面 (深度管理)</h2>
    <ul>
        <li><span class="key">Ctrl + G</span> : 显示/隐藏 <b>高级筛选面板</b></li>
        <li><span class="key">Ctrl + I</span> : 显示/隐藏 <b>元数据/标签面板</b></li>
        <li><span class="key">F5</span> : 强制刷新当前列表数据</li>
        <li><span class="key">Ctrl + N</span> : 新建一条空白灵感</li>
        <li><span class="key">Ctrl + Shift + L</span> : 立即锁定当前选中的分类</li>
    </ul>
</div>

<div class="section">
    <h2>四、编辑窗口</h2>
    <ul>
        <li><span class="key">Ctrl + S</span> : 保存内容并关闭编辑窗口</li>
        <li><span class="key">Ctrl + F</span> : 在正文中查找关键字</li>
        <li><span class="key">Esc</span> / <span class="key">Ctrl + W</span> : 放弃修改并退出</li>
    </ul>
</div>

<div class="section">
    <h2>五、其他功能提示</h2>
    <ul>
        <li><b>悬浮球</b>：左键双击唤起极速窗口，右键菜单可快速进入所有功能。</li>
        <li><b>标签同步</b>：在极速窗口使用 <span class="key">Ctrl + Shift + C/V</span> 可批量复制/粘贴标签。</li>
        <li><b>自动分类</b>：开启极速窗口下方的“自动归档”后，剪贴板捕获的内容将自动存入当前分类。</li>
    </ul>
</div>
<br/>
)html";
}
