#include "HelpWindow.h"
#include "StringUtils.h"
#include <QVBoxLayout>
#include <QScrollArea>

HelpWindow::HelpWindow(QWidget* parent) : FramelessDialog("使用说明", parent) {
    // 用户要求：更新过时的“使用说明”内容，准确阐述当前版本所有快捷键的用途。
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
</style>

<div class="section">
    <h2>一、全局系统热键</h2>
    <ul>
        <li><span class="key">Alt + Space</span> : <b>呼出/隐藏快速窗口</b></li>
        <li><span class="key">Ctrl + Shift + E</span> : <b>全局一键收藏</b> (将最后捕获内容存入收藏夹)</li>
        <li><span class="key">Alt + X</span> : <b>全能截屏</b> (支持识图、标注、贴图)</li>
        <li><span class="key">Alt + C</span> : <b>截图取文 (OCR)</b></li>
        <li><span class="key">Ctrl + S</span> : <b>浏览器智能采集</b> (仅在浏览器活跃时生效)</li>
        <li><span class="key">Ctrl + Shift + L</span> : <b>全局应用锁定</b></li>
        <li><span class="key">Ctrl + Shift + V</span> : <b>纯文本粘贴</b></li>
    </ul>
</div>

<div class="section">
    <h2>二、极速窗口 (Quick Window)</h2>
    <ul>
        <li><span class="key">Enter</span> / <b>左键双击</b> : <b>选中项自动上屏</b> (粘贴至目标软件)</li>
        <li><span class="key">Space</span> : <b>快速预览内容</b></li>
        <li><span class="key">Ctrl + F</span> : <b>聚焦搜索框</b></li>
        <li><span class="key">Ctrl + E</span> : <b>切换收藏状态</b></li>
        <li><span class="key">Ctrl + P</span> : <b>置顶/取消置顶项目</b></li>
        <li><span class="key">Ctrl + S</span> : <b>锁定/解锁单条记录</b></li>
        <li><span class="key">Ctrl + B</span> : <b>编辑选中项</b></li>
        <li><span class="key">Ctrl + N</span> : <b>新建灵感笔记</b></li>
        <li><span class="key">Ctrl + C</span> : <b>提取纯文本内容</b></li>
        <li><span class="key">Delete</span> / <span class="key">Shift + Delete</span> : <b>移至回收站 / 彻底删除</b></li>
        <li><span class="key">Alt + Up / Down</span> : <b>手动调整项目排序</b></li>
        <li><span class="key">PgUp / PgDn</span> : <b>列表翻页 (上一页/下一页)</b></li>
        <li><span class="key">Alt + Q</span> : <b>开启/关闭分类侧边栏</b></li>
        <li><span class="key">Alt + D</span> : <b>切换窗口始终最前</b></li>
        <li><span class="key">Alt + W</span> : <b>快速切换至主管理模式</b></li>
        <li><span class="key">Ctrl + 1~5</span> : <b>设置评分 (0为取消)</b></li>
        <li><span class="key">Ctrl + Shift + C / V</span> : <b>批量复制/粘贴标签</b></li>
    </ul>
</div>

<div class="section">
    <h2>三、主管理窗口</h2>
    <ul>
        <li><span class="key">Ctrl + G</span> : <b>开启高级筛选面板</b></li>
        <li><span class="key">Ctrl + I</span> : <b>开启元数据/批量面板</b></li>
        <li><span class="key">F5</span> : <b>刷新数据列表</b></li>
        <li><span class="key">Ctrl + S</span> : <b>保存当前修改或锁定分类</b></li>
    </ul>
</div>

<div class="section">
    <h2>四、编辑器与预览</h2>
    <ul>
        <li><span class="key">Ctrl + S</span> : <b>保存修改</b></li>
        <li><span class="key">Ctrl + F</span> : <b>内容内查找</b></li>
        <li><span class="key">Ctrl + W</span> : <b>关闭当前窗口</b></li>
    </ul>
</div>
<br/>
)html";
}
