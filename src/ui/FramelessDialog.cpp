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

// ============================================================================
// FramelessDialog 基类实现
// ============================================================================
FramelessDialog::FramelessDialog(const QString& title, QWidget* parent) 
    : QDialog(parent, Qt::FramelessWindowHint | Qt::Window) 
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_AlwaysShowToolTips);
    setMinimumWidth(40);
    setWindowTitle(title);

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
        "} "
    );
    outerLayout->addWidget(container);

    auto* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(20);
    shadow->setXOffset(0);
    shadow->setYOffset(4);
    shadow->setColor(QColor(0, 0, 0, 120));
    container->setGraphicsEffect(shadow);

    m_mainLayout = new QVBoxLayout(container);
    m_mainLayout->setContentsMargins(0, 0, 0, 10);
    m_mainLayout->setSpacing(0);

    // --- 标题栏 ---
    auto* titleBar = new QWidget();
    titleBar->setObjectName("TitleBar");
    titleBar->setMinimumHeight(38);
    titleBar->setStyleSheet("background-color: transparent; border-bottom: 1px solid #2D2D2D;");
    auto* titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(12, 0, 5, 0);
    titleLayout->setSpacing(4);

    m_titleLabel = new QLabel(title);
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
    
    // 初始化同步 UI 状态
    m_btnPin->blockSignals(true);
    m_btnPin->setChecked(m_isStayOnTop); 
    if (m_isStayOnTop) {
        m_btnPin->setIcon(IconHelper::getIcon("pin_vertical", "#FF551C"));
    }
    m_btnPin->blockSignals(false);
    m_btnPin->setStyleSheet("QPushButton { border: none; background: transparent; border-radius: 4px; } "
                          "QPushButton:hover { background-color: rgba(255, 255, 255, 0.1); } "
                          "QPushButton:pressed { background-color: rgba(255, 255, 255, 0.2); } "
                          "QPushButton:checked { background-color: rgba(255, 85, 28, 0.3); }");
    m_btnPin->setToolTip("置顶");
    connect(m_btnPin, &QPushButton::toggled, this, &FramelessDialog::toggleStayOnTop);
    titleLayout->addWidget(m_btnPin);

    m_minBtn = new QPushButton();
    m_minBtn->setObjectName("minBtn");
    m_minBtn->setFixedSize(28, 28);
    m_minBtn->setIconSize(QSize(18, 18));
    m_minBtn->setIcon(IconHelper::getIcon("minimize", "#888888"));
    m_minBtn->setAutoDefault(false);
    m_minBtn->setToolTip("最小化");
    m_minBtn->setCursor(Qt::PointingHandCursor);
    m_minBtn->setStyleSheet("QPushButton { background: transparent; border: none; border-radius: 4px; } "
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
    m_closeBtn->setToolTip("关闭");
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    m_closeBtn->setStyleSheet("QPushButton { background: transparent; border: none; border-radius: 4px; } "
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
        m_btnPin->setIcon(IconHelper::getIcon(checked ? "pin_vertical" : "pin_tilted", checked ? "#FF551C" : "#aaaaaa"));
    }
}

void FramelessDialog::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
#ifdef Q_OS_WIN
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
    if (m_isStayOnTop) setWindowFlag(Qt::WindowStaysOnTopHint, true);
    
    if (m_btnPin) {
        m_btnPin->blockSignals(true);
        m_btnPin->setChecked(stay);
        m_btnPin->setIcon(IconHelper::getIcon(stay ? "pin_vertical" : "pin_tilted", stay ? "#FF551C" : "#aaaaaa"));
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
    } else if (event->button() == Qt::RightButton) {
        // [CRITICAL] 显式吃掉右键，防止穿透或触发系统默认行为
        event->accept();
    }
}

void FramelessDialog::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::RightButton) {
        event->accept();
    }
}

void FramelessDialog::mouseMoveEvent(QMouseEvent* event) {
    if (event->buttons() & Qt::LeftButton) {
        move(event->globalPosition().toPoint() - m_dragPos);
        event->accept();
    } else if (event->buttons() & Qt::RightButton) {
        // 同样在移动中拦截右键
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

// ============================================================================
// FramelessInputDialog 实现
// ============================================================================
FramelessInputDialog::FramelessInputDialog(const QString& title, const QString& label, 
                                           const QString& initial, QWidget* parent)
    : FramelessDialog(title, parent) 
{
    // 保持高度，确保有足够空间让按钮沉底
    resize(500, 260);
    setMinimumSize(400, 240);
    
    auto* layout = new QVBoxLayout(m_contentArea);
    layout->setContentsMargins(20, 15, 20, 20);
    
    // 【关键修改】将全局间距设置为 7px，确保“标签”文字和输入框紧凑
    layout->setSpacing(7);

    auto* lbl = new QLabel(label);
    lbl->setStyleSheet("color: #eee; font-size: 13px;");
    layout->addWidget(lbl);

    m_edit = new QLineEdit(initial);
    // 设置最小高度，防止截断
    m_edit->setMinimumHeight(38);
    m_edit->setStyleSheet(
        "QLineEdit {"
        "  background-color: #2D2D2D; border: 1px solid #444; border-radius: 4px;"
        "  padding: 0px 10px; color: white; selection-background-color: #4a90e2;"
        "  font-size: 14px;"
        "}"
        "QLineEdit:focus { border: 1px solid #4a90e2; }"
    );
    layout->addWidget(m_edit);

    // 使用 PlaceholderText 显示提示
    if (title.contains("标签") || label.contains("标签")) {
        m_edit->setPlaceholderText("双击调出历史标签"); 
        m_edit->installEventFilter(this);
    }

    connect(m_edit, &QLineEdit::returnPressed, this, &QDialog::accept);

    // 【关键】增加 Stretch，强制将下方的按钮布局挤到底部
    // 这样输入框和上面的文字间距是 7px，而输入框和按钮的间距会自动拉大
    layout->addStretch();

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
        auto* selector = new AdvancedTagSelector(this);
        
        auto recentTags = DatabaseManager::instance().getRecentTagsWithCounts(20);
        QStringList allTags = DatabaseManager::instance().getAllTags();
        QStringList selected = m_edit->text().split(QRegularExpression("[,，]"), Qt::SkipEmptyParts);
        for(QString& s : selected) s = s.trimmed();

        selector->setup(recentTags, allTags, selected);
        
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
    QTimer::singleShot(100, m_edit, qOverload<>(&QWidget::setFocus));
}

// ============================================================================
// FramelessMessageBox 实现
// ============================================================================
FramelessMessageBox::FramelessMessageBox(const QString& title, const QString& text, QWidget* parent)
    : FramelessDialog(title, parent)
{
    resize(500, 220);
    setMinimumSize(400, 200);

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