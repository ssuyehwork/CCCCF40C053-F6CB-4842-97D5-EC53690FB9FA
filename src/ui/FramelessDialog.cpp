#include "FramelessDialog.h"
#include "IconHelper.h"
#include "StringUtils.h"
#include <QGraphicsDropShadowEffect>
#include <QSettings>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QTimer>
#include <QPainter>
#include <QPen>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include <QMenu>
#include <QCursor>
#include "AdvancedTagSelector.h"
#include "../core/DatabaseManager.h"
#include "StringUtils.h"

Qt::WindowFlags FramelessDialog::getInitialFlags(const QString& objName) {
    Qt::WindowFlags flags = Qt::FramelessWindowHint | Qt::Window | Qt::NoDropShadowWindowHint;
    if (!objName.isEmpty()) {
        QSettings settings("RapidNotes", "WindowStates");
        if (settings.value(objName + "/StayOnTop", false).toBool()) {
            flags |= Qt::WindowStaysOnTopHint;
        }
    }
    return flags;
}

FramelessDialog::FramelessDialog(const QString& title, QWidget* parent, const QString& objName)
    : QDialog(parent, getInitialFlags(objName))
{
    if (!objName.isEmpty()) setObjectName(objName);

    // 1. 彻底禁用原生背景绘制
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setWindowOpacity(0.0); // 初始全透明

    // 2. 强制使用透明调色板，防止“白色矩形”在渲染前出现
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Qt::transparent);
    setPalette(pal);

    setAttribute(Qt::WA_AlwaysShowToolTips);
    setWindowTitle(title);
}

void FramelessDialog::initFrameless() {
    // 将原本在构造函数中的 UI 初始化逻辑移至此处
    // 这样子类可以先 setFixedSize，再进行布局，避免尺寸跳变导致的白框

    if (layout()) return; // 防止重复初始化

    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(20, 20, 20, 20);

    auto* container = new QWidget(this);
    container->setObjectName("DialogContainer");
    container->setAttribute(Qt::WA_StyledBackground);
    container->setStyleSheet(
        "#DialogContainer {"
        "  background-color: #1e1e1e;"
        "  border: 1px solid #333333;"
        "  border-radius: 12px;"
        "} " + StringUtils::getToolTipStyle()
    );
    outerLayout->addWidget(container);

    // 延迟阴影效果加载，进一步规避初始化闪烁
    auto* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(20);
    shadow->setXOffset(0);
    shadow->setYOffset(4);
    shadow->setColor(QColor(0, 0, 0, 120));
    container->setGraphicsEffect(shadow);

    m_mainLayout = new QVBoxLayout(container);
    m_mainLayout->setContentsMargins(0, 0, 0, 10);
    m_mainLayout->setSpacing(0);

    // 标题栏构建
    auto* titleBar = new QWidget();
    titleBar->setObjectName("TitleBar");
    titleBar->setMinimumHeight(38);
    titleBar->setStyleSheet("background-color: transparent; border-bottom: 1px solid #2D2D2D;");
    auto* titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(12, 0, 5, 0);
    titleLayout->setSpacing(4);

    m_titleLabel = new QLabel(windowTitle());
    m_titleLabel->setStyleSheet("color: #888; font-size: 12px; font-weight: bold; border: none;");
    titleLayout->addWidget(m_titleLabel);
    titleLayout->addStretch();

    m_btnPin = new QPushButton();
    m_btnPin->setObjectName("btnPin");
    m_btnPin->setFixedSize(28, 28);
    m_btnPin->setIconSize(QSize(18, 18));
    m_btnPin->setAutoDefault(false);
    m_btnPin->setCheckable(true);
    m_btnPin->setIcon(IconHelper::getIcon("pin_tilted", "#aaaaaa"));
    
    m_btnPin->blockSignals(true);
    m_btnPin->setChecked(m_isStayOnTop); 
    if (m_isStayOnTop) {
        m_btnPin->setIcon(IconHelper::getIcon("pin_vertical", "#ffffff"));
    }
    m_btnPin->blockSignals(false);
    m_btnPin->setStyleSheet(StringUtils::getToolTipStyle() + 
                          "QPushButton { border: none; background: transparent; border-radius: 4px; } "
                          "QPushButton:hover { background-color: rgba(255, 255, 255, 0.1); } "
                          "QPushButton:pressed { background-color: rgba(255, 255, 255, 0.2); } "
                          "QPushButton:checked { background-color: rgba(58, 144, 255, 0.3); }");
    m_btnPin->setToolTip(StringUtils::wrapToolTip("置顶"));
    connect(m_btnPin, &QPushButton::toggled, this, &FramelessDialog::toggleStayOnTop);
    titleLayout->addWidget(m_btnPin);

    m_minBtn = new QPushButton();
    m_minBtn->setObjectName("minBtn");
    m_minBtn->setFixedSize(28, 28);
    m_minBtn->setIconSize(QSize(18, 18));
    m_minBtn->setIcon(IconHelper::getIcon("minimize", "#888888"));
    m_minBtn->setAutoDefault(false);
    m_minBtn->setToolTip(StringUtils::wrapToolTip("最小化"));
    m_minBtn->setCursor(Qt::PointingHandCursor);
    m_minBtn->setStyleSheet(StringUtils::getToolTipStyle() + 
        "QPushButton { background: transparent; border: none; border-radius: 4px; } "
        "QPushButton:hover { background-color: rgba(255, 255, 255, 0.1); }"
    );
    connect(m_minBtn, &QPushButton::clicked, this, &QDialog::showMinimized);
    titleLayout->addWidget(m_minBtn);

    m_closeBtn = new QPushButton();
    m_closeBtn->setObjectName("closeBtn");
    m_closeBtn->setFixedSize(28, 28);
    m_closeBtn->setIconSize(QSize(18, 18));
    m_closeBtn->setIcon(IconHelper::getIcon("close", "#888888"));
    m_closeBtn->setAutoDefault(false);
    m_closeBtn->setToolTip(StringUtils::wrapToolTip("关闭"));
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    m_closeBtn->setStyleSheet(StringUtils::getToolTipStyle() + 
        "QPushButton { background: transparent; border: none; border-radius: 4px; } "
        "QPushButton:hover { background-color: #E81123; }"
    );
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    titleLayout->addWidget(m_closeBtn);

    m_mainLayout->addWidget(titleBar);

    m_contentArea = new QWidget();
    m_contentArea->setObjectName("DialogContentArea");
    m_contentArea->setAttribute(Qt::WA_StyledBackground);
    m_contentArea->setStyleSheet("QWidget#DialogContentArea { background: transparent; border: none; }");
    m_mainLayout->addWidget(m_contentArea, 1);
}

void FramelessDialog::setStayOnTop(bool stay) {
    if (m_btnPin) m_btnPin->setChecked(stay);
}

void FramelessDialog::toggleStayOnTop(bool checked) {
    m_isStayOnTop = checked;
    saveWindowSettings();

    if (isVisible()) {
#ifdef Q_OS_WIN
        HWND hwnd = (HWND)winId();
        // 使用 SWP_NOACTIVATE 防止夺取焦点，移除 SWP_SHOWWINDOW 避免意外弹出
        SetWindowPos(hwnd, checked ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
#else
        Qt::WindowFlags f = windowFlags();
        if (checked) f |= Qt::WindowStaysOnTopHint;
        else f &= ~Qt::WindowStaysOnTopHint;
        setWindowFlags(f);
        show();
#endif
    }

    if (m_btnPin) {
        m_btnPin->setIcon(IconHelper::getIcon(checked ? "pin_vertical" : "pin_tilted", checked ? "#ffffff" : "#aaaaaa"));
    }
}

void FramelessDialog::showEvent(QShowEvent* event) {
    // 窗口显示瞬间恢复透明度，确保出生即完整
    setWindowOpacity(1.0);
    QDialog::showEvent(event);

#ifdef Q_OS_WIN
    // Windows 特有的置顶强化，确保在某些置顶竞争中胜出
    if (m_isStayOnTop) {
        HWND hwnd = (HWND)winId();
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
#endif
}

void FramelessDialog::loadWindowSettings() {
    if (objectName().isEmpty()) return;
    QSettings settings("RapidNotes", "WindowStates");
    bool stay = settings.value(objectName() + "/StayOnTop", false).toBool();
    
    m_isStayOnTop = stay;

    // 核心优化：不再在此处调用 setWindowFlags(flags)，因为构造函数已经预置了正确标志。
    // 这样可以避免 Windows 句柄重造导致的白框闪烁。
    
    if (m_btnPin) {
        m_btnPin->blockSignals(true);
        m_btnPin->setChecked(stay);
        m_btnPin->setIcon(IconHelper::getIcon(stay ? "pin_vertical" : "pin_tilted", stay ? "#ffffff" : "#aaaaaa"));
        m_btnPin->blockSignals(false);
    }
}

void FramelessDialog::saveWindowSettings() {
    if (objectName().isEmpty()) return;
    QSettings settings("RapidNotes", "WindowStates");
    settings.setValue(objectName() + "/StayOnTop", m_isStayOnTop);
}

void FramelessDialog::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_dragPos = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
    }
}

void FramelessDialog::mouseMoveEvent(QMouseEvent* event) {
    if (event->buttons() & Qt::LeftButton) {
        move(event->globalPosition().toPoint() - m_dragPos);
        event->accept();
    }
}

void FramelessDialog::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
}

void FramelessDialog::keyPressEvent(QKeyEvent* event) {
    if (event->modifiers() == Qt::ControlModifier && event->key() == Qt::Key_W) {
        reject();
    } else {
        QDialog::keyPressEvent(event);
    }
}

// ----------------------------------------------------------------------------
// FramelessInputDialog
// ----------------------------------------------------------------------------
FramelessInputDialog::FramelessInputDialog(const QString& title, const QString& label, 
                                           const QString& initial, QWidget* parent)
    : FramelessDialog(title, parent) 
{
    resize(500, 220);
    setMinimumSize(400, 200);
    initFrameless();

    auto* layout = new QVBoxLayout(m_contentArea);
    layout->setContentsMargins(20, 15, 20, 20);
    layout->setSpacing(12);

    auto* lbl = new QLabel(label);
    lbl->setStyleSheet("color: #eee; font-size: 13px;");
    layout->addWidget(lbl);

    m_edit = new QLineEdit(initial);
    m_edit->setStyleSheet(
        "QLineEdit {"
        "  background-color: #2D2D2D; border: 1px solid #444; border-radius: 4px;"
        "  padding: 8px; color: white; selection-background-color: #4a90e2;"
        "}"
        "QLineEdit:focus { border: 1px solid #4a90e2; }"
    );
    layout->addWidget(m_edit);

    // 【新增】回车确认逻辑
    connect(m_edit, &QLineEdit::returnPressed, this, &QDialog::accept);

    // 【新增】双击弹出历史标签：如果是标签相关的输入框
    if (title.contains("标签") || label.contains("标签")) {
        m_edit->setToolTip(StringUtils::wrapToolTip("提示：双击可调出历史标签面板"));
        m_edit->installEventFilter(this);
    }

    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    
    auto* btnOk = new QPushButton("确定");
    btnOk->setAutoDefault(false);
    btnOk->setCursor(Qt::PointingHandCursor);
    btnOk->setStyleSheet("QPushButton { background-color: #4a90e2; color: white; border: none; border-radius: 4px; padding: 6px 20px; font-weight: bold; } QPushButton:hover { background-color: #357abd; }");
    connect(btnOk, &QPushButton::clicked, this, &QDialog::accept);
    btnLayout->addWidget(btnOk);

    layout->addLayout(btnLayout);

    m_edit->setFocus();
    m_edit->selectAll();
}

bool FramelessInputDialog::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_edit && event->type() == QEvent::MouseButtonDblClick) {
        // 调用项目现有的高级标签选择器
        auto* selector = new AdvancedTagSelector(this);
        
        // 准备数据：获取最近标签和所有标签
        auto recentTags = DatabaseManager::instance().getRecentTagsWithCounts(20);
        QStringList allTags = DatabaseManager::instance().getAllTags();
        QStringList selected = m_edit->text().split(QRegularExpression("[,，]"), Qt::SkipEmptyParts);
        for(QString& s : selected) s = s.trimmed();

        selector->setup(recentTags, allTags, selected);
        
        // 监听确认信号
        connect(selector, &AdvancedTagSelector::tagsConfirmed, [this](const QStringList& tags){
            if (!tags.isEmpty()) {
                m_edit->setText(tags.join(", "));
                m_edit->setFocus();
            }
        });

        selector->showAtCursor();
        return true;
    }
    return FramelessDialog::eventFilter(watched, event);
}

void FramelessInputDialog::showEvent(QShowEvent* event) {
    FramelessDialog::showEvent(event);
    // 增加延迟至 100ms 确保焦点稳定
    QTimer::singleShot(100, m_edit, qOverload<>(&QWidget::setFocus));
}

// ----------------------------------------------------------------------------
// FramelessMessageBox
// ----------------------------------------------------------------------------
FramelessMessageBox::FramelessMessageBox(const QString& title, const QString& text, QWidget* parent)
    : FramelessDialog(title, parent)
{
    resize(500, 220);
    setMinimumSize(400, 200);
    initFrameless();

    auto* layout = new QVBoxLayout(m_contentArea);
    layout->setContentsMargins(25, 20, 25, 25);
    layout->setSpacing(20);

    auto* lbl = new QLabel(text);
    lbl->setWordWrap(true);
    lbl->setStyleSheet("color: #eee; font-size: 14px; line-height: 150%;");
    layout->addWidget(lbl);

    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();

    auto* btnCancel = new QPushButton("取消");
    btnCancel->setAutoDefault(false);
    btnCancel->setCursor(Qt::PointingHandCursor);
    btnCancel->setStyleSheet("QPushButton { background-color: transparent; color: #888; border: 1px solid #555; border-radius: 4px; padding: 6px 15px; } QPushButton:hover { color: #eee; border-color: #888; }");
    connect(btnCancel, &QPushButton::clicked, this, [this](){ emit cancelled(); reject(); });
    btnLayout->addWidget(btnCancel);

    auto* btnOk = new QPushButton("确定");
    btnOk->setAutoDefault(false);
    btnOk->setCursor(Qt::PointingHandCursor);
    btnOk->setStyleSheet("QPushButton { background-color: #e74c3c; color: white; border: none; border-radius: 4px; padding: 6px 20px; font-weight: bold; } QPushButton:hover { background-color: #c0392b; }");
    connect(btnOk, &QPushButton::clicked, this, [this](){ emit confirmed(); accept(); });
    btnLayout->addWidget(btnOk);

    layout->addLayout(btnLayout);
}
