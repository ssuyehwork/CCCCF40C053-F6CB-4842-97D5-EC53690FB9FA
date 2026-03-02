#include "HelpWindow.h"
#include "StringUtils.h"
#include <QVBoxLayout>
#include <QScrollArea>

HelpWindow::HelpWindow(QWidget* parent) : FramelessDialog("使用说明", parent) {
    setObjectName("HelpWindow");
    loadWindowSettings();
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
    h2 { color: #4FACFE; margin-top: 25px; border-bottom: 1px solid #333; padding-bottom: 8px; font-size: 18px; }
    b { color: #F1C40F; font-weight: bold; }
    .key { background: #444; padding: 2px 8px; border-radius: 4px; font-family: 'Consolas', monospace; color: #FFF; font-weight: bold; font-size: 12px; border: 1px solid #555; }
    .section { margin-bottom: 30px; padding: 10px; background: rgba(255,255,255,0.03); border-radius: 8px; }
    ul { margin-left: -15px; list-style-type: none; }
    li { margin-bottom: 12px; line-height: 1.6; }
    .sub-title { color: #1abc9c; font-weight: bold; margin-top: 15px; display: block; font-size: 14px; border-left: 3px solid #1abc9c; padding-left: 8px; }
    .desc { color: #BBB; font-size: 13px; display: block; margin-top: 4px; }
    .cmd { color: #3498db; font-family: monospace; font-weight: bold; }
</style>

<div class="section">
    <h2>一、全局系统热键</h2>
    <ul>
        <li><span class="key">Alt + Space</span> : <b>呼出/隐藏极速窗口</b> <br/> <span class="desc">默认快捷键，可在设置中自定义。实现即用即走的极速交互体验。</span></li>
        <li><span class="key">Ctrl + S</span> : <b>浏览器智能采集</b> <br/> <span class="desc">仅限浏览器环境，自动提取所选文字的【标题】与【正文】并智能入库。</span></li>
        <li><span class="key">Ctrl + Alt + A</span> : <b>全能截屏/OCR</b> <br/> <span class="desc">支持识图取字、画笔标注、马赛克及【屏幕贴图】功能。</span></li>
        <li><span class="key">Ctrl + Shift + E</span> : <b>一键收藏最后捕获</b> <br/> <span class="desc">将剪贴板最后一条自动生成的灵感快速加入收藏夹。</span></li>
        <li><span class="key">快捷键 5 (需设置)</span> : <b>全局应用锁定</b> <br/> <span class="desc">一键进入 Eagle 风格的启动锁界面，保护数据隐私。</span></li>
    </ul>
</div>

<div class="section">
    <h2>二、极速窗口交互指南</h2>
    <span class="sub-title">搜索与内容处理</span>
    <ul>
        <li><b>智能搜索</b>：输入即过滤。无结果时按 <span class="key">Enter</span> 存入历史。双击搜索框查看历史。</li>
        <li><b>自动上屏</b>：选中项按 <span class="key">Enter</span> 或 <b>左键双击</b>，自动粘贴内容至目标软件。</li>
        <li><b>格式清洗</b>：按 <span class="key">Ctrl + T</span> 提取选中项的纯文本内容至剪贴板。</li>
        <li><b>快速预览</b>：按 <span class="key">Space</span> 呼出/关闭浮动预览大窗。</li>
    </ul>
    
    <span class="sub-title">高效管理快捷键</span>
    <ul>
        <li><b>分级标记</b>：<span class="key">Ctrl + 1~5</span> 设置评分；<span class="key">Ctrl + E</span> 收藏；<span class="key">Ctrl + P</span> 置顶。</li>
        <li><b>隐私控制</b>：<span class="key">Ctrl + S</span> 锁定单条记录；<span class="key">Ctrl + Shift + L</span> 立即锁定当前分类。</li>
        <li><b>标签系统</b>：底部框 <b>左键双击</b> 打开高级标签选择器。支持 <span class="key">Ctrl + Shift + C/V</span> 批量同步标签。</li>
        <li><b>翻页导航</b>：<span class="key">Alt + S</span> 上一页；<span class="key">Alt + X</span> 下一页。</li>
    </ul>

    <span class="sub-title">窗口与布局手势</span>
    <ul>
        <li><b>切换主界面</b>：按 <span class="key">Alt + W</span> 快速切换至主管理模式。</li>
        <li><b>侧边栏联动</b>：按 <span class="key">Ctrl + Q</span> 开关分类导航。支持 <b>拖拽列表项</b> 至侧边栏进行分类移动。</li>
        <li><b>置顶切换</b>：按 <span class="key">Alt + D</span> 切换窗口【始终最前】状态。</li>
    </ul>
</div>

<div class="section">
    <h2>三、主界面 (深度管理模式)</h2>
    <ul>
        <li><b>高级筛选面板</b> (<span class="key">Ctrl + G</span>)：支持类型（图片/链接/本地文件）、日期及评分的多维复合筛选。</li>
        <li><b>元数据/批量面板</b> (<span class="key">Ctrl + I</span>)：支持多选笔记后，一键批量修改标签。</li>
        <li><b>面板自由重组</b>：在各面板标题栏 <b>右键单击</b>，可选择【向左/向右移动】自定义布局顺序。</li>
        <li><b>分类管理</b>：侧边栏支持 <span class="key">Ctrl + Up/Down</span> 进行排序；<span class="key">Ctrl + Shift + Up/Down</span> 置顶/置底。</li>
    </ul>
</div>

<div class="section">
    <h2>四、编辑器与高级特性</h2>
    <ul>
        <li><b>扩展编辑</b>：在详情页 <b>左键双击标题栏</b>，可呼出无边框长标题编辑器。</li>
        <li><b>快捷保存</b>：编辑模式下 <span class="key">Ctrl + S</span> 立即保存修改；<span class="key">Ctrl + F</span> 开启内容内查找。</li>
        <li><b>自动化设置</b>：工具箱菜单中可开启【剪贴板自动归档】，捕获内容将自动归类至当前活跃分区。</li>
        <li><b>动态特效</b>：系统实时监听剪贴板，检测到新内容时将在鼠标位置触发【烟花动效】反馈。</li>
    </ul>
</div>

<div class="section">
    <h2>💡 进阶贴士</h2>
    <ul>
        <li><b>回收站保护</b>：<span class="key">Del</span> 进入回收站（可还原）；<span class="key">Ctrl + Shift + Del</span> 彻底销毁。</li>
        <li><b>多选模式</b>：列表支持标准的 <span class="key">Ctrl/Shift + 点击</span> 批量操作。</li>
    </ul>
</div>
<br/>
)html";
}
