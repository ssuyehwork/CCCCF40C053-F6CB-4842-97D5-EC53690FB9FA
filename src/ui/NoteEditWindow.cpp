#include "NoteEditWindow.h"
#include "StringUtils.h"
#include "AdvancedTagSelector.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include "../core/DatabaseManager.h"
#include "IconHelper.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QTextEdit>
#include <QLabel>
#include <QGridLayout>
#include <QPainter>
#include <QGraphicsDropShadowEffect>
#include <QWindow>
#include <QMouseEvent>
#include <QShortcut>
#include <QKeySequence>
#include <QApplication>
#include <QScreen>
#include <QTextListFormat>
#include <QCompleter>
#include <QStringListModel>
#include <QDialog>

namespace {
// ==========================================
// TitleEditorDialog (Copied from MetadataPanel)
// ==========================================
class TitleEditorDialog : public QDialog {
public:
    TitleEditorDialog(const QString& currentText, QWidget* parent = nullptr) : QDialog(parent) {
        setWindowFlags(Qt::FramelessWindowHint | Qt::Popup | Qt::NoDropShadowWindowHint);
        setAttribute(Qt::WA_TranslucentBackground);
        setFixedSize(400, 170);

        auto* layout = new QVBoxLayout(this);
        // [CRITICAL] 边距调整为 20px 以容纳阴影，防止出现“断崖式”阴影截止
        layout->setContentsMargins(20, 20, 20, 20);

        auto* container = new QWidget(this);
        container->setObjectName("container");
        container->setStyleSheet("QWidget#container { background-color: #1e1e1e; border: 1px solid #333; border-radius: 10px; }");
        layout->addWidget(container);

        auto* innerLayout = new QVBoxLayout(container);
        innerLayout->setContentsMargins(12, 12, 12, 10);
        innerLayout->setSpacing(8);

        m_textEdit = new QTextEdit();
        m_textEdit->setText(currentText);
        m_textEdit->setPlaceholderText("请输入标题...");
        m_textEdit->setStyleSheet("QTextEdit { background-color: #252526; border: 1px solid #444; border-radius: 6px; color: white; font-size: 14px; padding: 8px; } QTextEdit:focus { border: 1px solid #4a90e2; }");
        innerLayout->addWidget(m_textEdit);

        auto* btnLayout = new QHBoxLayout();
        btnLayout->addStretch();
        auto* btnSave = new QPushButton("完成");
        btnSave->setFixedSize(64, 30);
        btnSave->setCursor(Qt::PointingHandCursor);
        btnSave->setStyleSheet("QPushButton { background-color: #4a90e2; color: white; border: none; border-radius: 4px; font-weight: bold; } QPushButton:hover { background-color: #357abd; }");
        connect(btnSave, &QPushButton::clicked, this, &QDialog::accept);
        btnLayout->addWidget(btnSave);
        innerLayout->addLayout(btnLayout);

        // 1:1 匹配 QuickWindow 阴影规范 (同步修复模糊截止问题)
        auto* shadow = new QGraphicsDropShadowEffect(this);
        shadow->setBlurRadius(20);
        shadow->setColor(QColor(0, 0, 0, 120));
        shadow->setOffset(0, 4);
        container->setGraphicsEffect(shadow);
    }

    QString getText() const { return m_textEdit->toPlainText().trimmed(); }

    void showAtCursor() {
        QPoint pos = QCursor::pos();
        // 尝试居中显示在鼠标点击位置附近
        move(pos.x() - width() / 2, pos.y() - 40);
        show();
        m_textEdit->setFocus();
        m_textEdit->selectAll();
    }

private:
    QTextEdit* m_textEdit;
};
}

NoteEditWindow::NoteEditWindow(int noteId, QWidget* parent) 
    : QWidget(parent, Qt::Window | Qt::FramelessWindowHint), m_noteId(noteId) 
{
    setWindowTitle(m_noteId > 0 ? "编辑笔记" : "记录灵感");
    setAttribute(Qt::WA_TranslucentBackground); 
    // 增加窗口物理尺寸以容纳外围阴影，防止 UpdateLayeredWindowIndirect 参数错误
    resize(980, 680); 
    initUI();
    setupShortcuts();
    
    if (m_noteId > 0) {
        loadNoteData(m_noteId);
    }
}

void NoteEditWindow::setDefaultCategory(int catId) {
    m_catId = catId;
}

void NoteEditWindow::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
#ifdef Q_OS_WIN
    if (m_isStayOnTop) {
        HWND hwnd = (HWND)winId();
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
#else
    Qt::WindowFlags f = windowFlags();
    if (m_isStayOnTop) f |= Qt::WindowStaysOnTopHint;
    else f &= ~Qt::WindowStaysOnTopHint;
    if (windowFlags() != f) {
        setWindowFlags(f);
        show();
    }
#endif
}

void NoteEditWindow::paintEvent(QPaintEvent* event) {
    // 由于使用了 mainContainer 承载背景和圆角，窗口本身只需保持透明
    Q_UNUSED(event);
}

void NoteEditWindow::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        if (event->pos().y() < 40) {
            m_dragPos = event->globalPosition().toPoint() - frameGeometry().topLeft();
            event->accept();
        }
    }
}

void NoteEditWindow::mouseMoveEvent(QMouseEvent* event) {
    if (event->buttons() & Qt::LeftButton) {
        if (!m_dragPos.isNull()) {
            move(event->globalPosition().toPoint() - m_dragPos);
            event->accept();
        }
    }
}

void NoteEditWindow::mouseReleaseEvent(QMouseEvent* event) {
    m_dragPos = QPoint();
}

bool NoteEditWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched->property("isCloseBtn").toBool()) {
        QPushButton* btn = qobject_cast<QPushButton*>(watched);
        if (btn) {
            if (event->type() == QEvent::Enter) {
                btn->setIcon(IconHelper::getIcon("close", "#ffffff", 20));
            } else if (event->type() == QEvent::Leave) {
                btn->setIcon(IconHelper::getIcon("close", "#aaaaaa", 20));
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void NoteEditWindow::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->pos().y() < 40) {
        toggleMaximize();
    }
}

void NoteEditWindow::initUI() {
    auto* windowLayout = new QVBoxLayout(this);
    windowLayout->setObjectName("WindowLayout");
    windowLayout->setContentsMargins(15, 15, 15, 15); // 留出阴影空间
    windowLayout->setSpacing(0);

    // 主容器：承载圆角、背景和阴影
    auto* mainContainer = new QWidget();
    mainContainer->setObjectName("MainContainer");
    mainContainer->setStyleSheet("QWidget#MainContainer { background-color: #1E1E1E; border-radius: 12px; }");
    windowLayout->addWidget(mainContainer);

    auto* outerLayout = new QVBoxLayout(mainContainer);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    // 自定义标题栏
    m_titleBar = new QWidget();
    m_titleBar->setFixedHeight(32); 
    m_titleBar->setStyleSheet("background-color: #252526; border-top-left-radius: 12px; border-top-right-radius: 12px; border-bottom: 1px solid #333;");
    auto* tbLayout = new QHBoxLayout(m_titleBar);
    tbLayout->setContentsMargins(12, 0, 0, 0); // 右边距设为 0
    tbLayout->setSpacing(0); // 按钮间距设为 0

    QLabel* titleIcon = new QLabel();
    titleIcon->setPixmap(IconHelper::getIcon("edit", "#4FACFE", 18).pixmap(18, 18));
    tbLayout->addWidget(titleIcon);

    m_winTitleLabel = new QLabel(m_noteId > 0 ? "编辑笔记" : "记录灵感");
    m_winTitleLabel->setStyleSheet("font-weight: bold; color: #ddd; font-size: 13px; margin-left: 5px;");
    tbLayout->addWidget(m_winTitleLabel);
    tbLayout->addStretch();

    // 统一控制按钮样式：32x32px（对齐主窗口），图标 20px，锁定比例以消除离谱内边距
    QString ctrlBtnStyle = "QPushButton { background: transparent; border: none; border-radius: 5px; padding: 0px; } "
                           "QPushButton:hover { background-color: rgba(255, 255, 255, 0.1); }";
    
    QPushButton* btnMin = new QPushButton();
    btnMin->setIcon(IconHelper::getIcon("minimize", "#aaaaaa", 20));
    btnMin->setIconSize(QSize(20, 20));
    btnMin->setFixedSize(32, 32);
    btnMin->setStyleSheet(ctrlBtnStyle);
    connect(btnMin, &QPushButton::clicked, this, &QWidget::showMinimized);
    
    m_maxBtn = new QPushButton();
    m_maxBtn->setIcon(IconHelper::getIcon("maximize", "#aaaaaa", 20));
    m_maxBtn->setIconSize(QSize(20, 20));
    m_maxBtn->setFixedSize(32, 32);
    m_maxBtn->setStyleSheet(ctrlBtnStyle);
    connect(m_maxBtn, &QPushButton::clicked, this, &NoteEditWindow::toggleMaximize);

    m_btnStayOnTop = new QPushButton();
    m_btnStayOnTop->setIcon(IconHelper::getIcon("pin_tilted", "#aaaaaa", 20));
    m_btnStayOnTop->setIconSize(QSize(20, 20));
    m_btnStayOnTop->setFixedSize(32, 32);
    m_btnStayOnTop->setCheckable(true);
    m_btnStayOnTop->setStyleSheet(ctrlBtnStyle + " QPushButton:checked { background-color: #f1c40f; }");
    connect(m_btnStayOnTop, &QPushButton::toggled, this, &NoteEditWindow::toggleStayOnTop);
    
    QPushButton* btnClose = new QPushButton();
    btnClose->setIcon(IconHelper::getIcon("close", "#aaaaaa", 20));
    btnClose->setIconSize(QSize(20, 20));
    btnClose->setFixedSize(32, 32);
    btnClose->setStyleSheet("QPushButton { background: transparent; border: none; border-radius: 5px; padding: 0px; } QPushButton:hover { background-color: #E81123; }");
    connect(btnClose, &QPushButton::clicked, this, &QWidget::close);

    // 为关闭按钮实现 Hover 图标变白逻辑
    btnClose->installEventFilter(this);
    btnClose->setProperty("isCloseBtn", true);

    tbLayout->addWidget(m_btnStayOnTop);
    tbLayout->addWidget(btnMin);
    tbLayout->addWidget(m_maxBtn);
    tbLayout->addWidget(btnClose);
    outerLayout->addWidget(m_titleBar);

    // 主内容区使用 Splitter
    m_splitter = new QSplitter(Qt::Horizontal);
    m_splitter->setStyleSheet("QSplitter::handle { background-color: #252526; width: 2px; } QSplitter::handle:hover { background-color: #4FACFE; }");

    // 左侧面板
    QWidget* leftContainer = new QWidget();
    auto* leftLayout = new QVBoxLayout(leftContainer);
    leftLayout->setContentsMargins(15, 15, 15, 15);
    setupLeftPanel(leftLayout);

    // 右侧面板
    QWidget* rightContainer = new QWidget();
    auto* rightLayout = new QVBoxLayout(rightContainer);
    rightLayout->setContentsMargins(10, 15, 15, 15);
    setupRightPanel(rightLayout);

    m_splitter->addWidget(leftContainer);
    m_splitter->addWidget(rightContainer);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({300, 650});

    outerLayout->addWidget(m_splitter);

    // 阴影应用在内部容器上，确保不超出窗口边界
    auto* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(15);
    shadow->setColor(QColor(0, 0, 0, 180));
    shadow->setOffset(0, 2);
    mainContainer->setGraphicsEffect(shadow);
}

void NoteEditWindow::setupLeftPanel(QVBoxLayout* layout) {
    QString labelStyle = "color: #888; font-size: 12px; font-weight: bold; margin-bottom: 4px;";
    QString inputStyle = "QLineEdit, QComboBox { background: #252526; border: 1px solid #333; border-radius: 4px; padding: 8px; color: #eee; font-size: 13px; } QLineEdit:focus, QComboBox:focus { border: 1px solid #4FACFE; }";

    QLabel* lblTitle = new QLabel("标题");
    lblTitle->setStyleSheet(labelStyle);
    m_titleEdit = new ClickableLineEdit();
    m_titleEdit->setPlaceholderText("请输入灵感标题...");
    m_titleEdit->setStyleSheet(inputStyle);
    m_titleEdit->setAlignment(Qt::AlignLeft);
    connect(m_titleEdit, &ClickableLineEdit::doubleClicked, this, &NoteEditWindow::openExpandedTitleEditor);
    layout->addWidget(lblTitle);
    layout->addWidget(m_titleEdit);

    QLabel* lblTags = new QLabel("标签");
    lblTags->setStyleSheet(labelStyle);
    m_tagEdit = new ClickableLineEdit();
    m_tagEdit->setPlaceholderText("使用逗号分隔，如: 工作, 待办 (双击显示历史)");
    m_tagEdit->setStyleSheet(inputStyle);
    connect(m_tagEdit, &ClickableLineEdit::doubleClicked, this, &NoteEditWindow::openTagSelector);
    
    // 智能补全标签
    QStringList allTags = DatabaseManager::instance().getAllTags();
    QCompleter* completer = new QCompleter(allTags, this);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setFilterMode(Qt::MatchContains);
    m_tagEdit->setCompleter(completer);

    layout->addWidget(lblTags);
    layout->addWidget(m_tagEdit);

    QLabel* lblColor = new QLabel("标记颜色");
    lblColor->setStyleSheet(labelStyle);
    layout->addWidget(lblColor);

    QWidget* colorGrid = new QWidget();
    QGridLayout* grid = new QGridLayout(colorGrid);
    grid->setContentsMargins(0, 10, 0, 10);
    
    m_colorGroup = new QButtonGroup(this);
    QStringList colors = {"#FF9800", "#444444", "#2196F3", "#4CAF50", "#F44336", "#9C27B0"};
    for(int i=0; i<colors.size(); ++i) {
        QPushButton* btn = createColorBtn(colors[i], i);
        grid->addWidget(btn, i/3, i%3);
        m_colorGroup->addButton(btn, i);
    }
    if(m_colorGroup->button(0)) m_colorGroup->button(0)->setChecked(true);
    
    layout->addWidget(colorGrid);
    
    m_defaultColorCheck = new QCheckBox("设为默认颜色");
    m_defaultColorCheck->setStyleSheet("QCheckBox { color: #858585; font-size: 12px; margin-top: 5px; }");
    layout->addWidget(m_defaultColorCheck);

    layout->addStretch(); 

    QPushButton* saveBtn = new QPushButton();
    saveBtn->setIcon(IconHelper::getIcon("save", "#ffffff"));
    saveBtn->setText("  保存 (Ctrl+S)");
    saveBtn->setCursor(Qt::PointingHandCursor);
    saveBtn->setFixedHeight(50);
    saveBtn->setStyleSheet("QPushButton { background-color: #4FACFE; color: white; border: none; border-radius: 6px; font-weight: bold; font-size: 13px; } QPushButton:hover { background-color: #357abd; }");
    connect(saveBtn, &QPushButton::clicked, this, &NoteEditWindow::saveNote);
    layout->addWidget(saveBtn);
}

QPushButton* NoteEditWindow::createColorBtn(const QString& color, int id) {
    QPushButton* btn = new QPushButton();
    btn->setCheckable(true);
    btn->setFixedSize(30, 30);
    btn->setProperty("color", color);
    btn->setStyleSheet(QString(
        "QPushButton { background-color: %1; border-radius: 15px; border: 2px solid transparent; }"
        "QPushButton:checked { border: 2px solid white; }"
    ).arg(color));
    return btn;
}

void NoteEditWindow::setupRightPanel(QVBoxLayout* layout) {
    QHBoxLayout* headerLayout = new QHBoxLayout();
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(1);

    QLabel* titleLabel = new QLabel("详细内容");
    titleLabel->setStyleSheet("color: #888; font-size: 11px; font-weight: bold;");
    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();

    QHBoxLayout* toolBar = new QHBoxLayout();
    toolBar->setContentsMargins(0, 0, 0, 0);
    toolBar->setSpacing(0); // 彻底消除按钮间距，实现紧凑布局

    // 标准化工具栏样式：对齐 HeaderBar 参数
    QString btnStyle = "QPushButton { background: transparent; border: none; border-radius: 5px; padding: 0px; } "
                       "QPushButton:hover { background-color: rgba(255, 255, 255, 0.1); } "
                       "QPushButton:checked { background-color: rgba(255, 255, 255, 0.2); }";
    
    auto addTool = [&](const QString& iconName, const QString& tip, std::function<void()> callback) {
        QPushButton* btn = new QPushButton();
        btn->setIcon(IconHelper::getIcon(iconName, "#aaaaaa", 20)); // 图标增大到 20px
        btn->setIconSize(QSize(20, 20));
        btn->setToolTip(StringUtils::wrapToolTip(tip));
        btn->setFixedSize(32, 32); // 尺寸标准化为 32x32
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(btnStyle);
        connect(btn, &QPushButton::clicked, callback);
        toolBar->addWidget(btn);
        return btn;
    };

    addTool("undo", "撤销 (Ctrl+Z)", [this](){ m_contentEdit->undo(); });
    addTool("redo", "重做 (Ctrl+Y)", [this](){ m_contentEdit->redo(); });
    
    QFrame* sep1 = new QFrame();
    sep1->setFixedWidth(1);
    sep1->setFixedHeight(16);
    sep1->setStyleSheet("background-color: #333; margin-left: 2px; margin-right: 2px;");
    toolBar->addWidget(sep1);

    addTool("list_ul", "无序列表", [this](){ m_contentEdit->toggleList(false); });
    addTool("list_ol", "有序列表", [this](){ m_contentEdit->toggleList(true); });
    
    QPushButton* btnTodo = new QPushButton();
    btnTodo->setIcon(IconHelper::getIcon("todo", "#aaaaaa", 20));
    btnTodo->setIconSize(QSize(20, 20));
    btnTodo->setFixedSize(32, 32); 
    btnTodo->setToolTip(StringUtils::wrapToolTip("插入待办事项"));
    btnTodo->setStyleSheet(btnStyle);
    btnTodo->setCursor(Qt::PointingHandCursor);
    connect(btnTodo, &QPushButton::clicked, [this](){ m_contentEdit->insertTodo(); });
    toolBar->addWidget(btnTodo);

    QPushButton* btnPre = new QPushButton();
    btnPre->setIcon(IconHelper::getIcon("eye", "#aaaaaa", 20));
    btnPre->setIconSize(QSize(20, 20));
    btnPre->setFixedSize(32, 32);
    btnPre->setToolTip(StringUtils::wrapToolTip("切换 Markdown 预览/编辑"));
    btnPre->setStyleSheet(btnStyle);
    btnPre->setCursor(Qt::PointingHandCursor);
    btnPre->setCheckable(true);
    connect(btnPre, &QPushButton::toggled, [this](bool checked){ m_contentEdit->togglePreview(checked); });
    toolBar->addWidget(btnPre);

    addTool("edit_clear", "清除格式", [this](){ m_contentEdit->clearFormatting(); });
    
    QFrame* sep2 = new QFrame();
    sep2->setFixedWidth(1);
    sep2->setFixedHeight(16);
    sep2->setStyleSheet("background-color: #333; margin-left: 2px; margin-right: 2px;");
    toolBar->addWidget(sep2);

    // 高亮颜色
    QStringList hColors = {"#c0392b", "#f1c40f", "#27ae60", "#2980b9"};
    for (const auto& color : hColors) {
        QPushButton* hBtn = new QPushButton();
        hBtn->setFixedSize(20, 20);
        hBtn->setStyleSheet(QString("QPushButton { background-color: %1; border: 1px solid rgba(0,0,0,0.2); border-radius: 4px; } "
                                    "QPushButton:hover { border-color: white; }").arg(color));
        hBtn->setCursor(Qt::PointingHandCursor);
        connect(hBtn, &QPushButton::clicked, [this, color](){ m_contentEdit->highlightSelection(QColor(color)); });
        toolBar->addWidget(hBtn);
    }

    // 清除高亮按钮
    QPushButton* btnNoColor = new QPushButton();
    btnNoColor->setIcon(IconHelper::getIcon("no_color", "#aaaaaa", 14));
    btnNoColor->setFixedSize(24, 24);
    btnNoColor->setToolTip(StringUtils::wrapToolTip("清除高亮"));
    btnNoColor->setStyleSheet("QPushButton { background: transparent; border: 1px solid #444; border-radius: 4px; margin-left: 4px; } "
                              "QPushButton:hover { background-color: rgba(255, 255, 255, 0.1); border-color: #888; }");
    btnNoColor->setCursor(Qt::PointingHandCursor);
    connect(btnNoColor, &QPushButton::clicked, [this](){ m_contentEdit->highlightSelection(Qt::transparent); });
    toolBar->addWidget(btnNoColor);
    
    headerLayout->addLayout(toolBar);
    layout->addLayout(headerLayout);

    // 搜索栏 (默认隐藏)
    m_searchBar = new QWidget();
    m_searchBar->setVisible(false);
    m_searchBar->setStyleSheet("background-color: #252526; border-radius: 6px; padding: 2px;");
    auto* sbLayout = new QHBoxLayout(m_searchBar);
    sbLayout->setContentsMargins(5, 2, 5, 2);
    m_searchEdit = new QLineEdit();
    m_searchEdit->setPlaceholderText("查找内容...");
    m_searchEdit->setStyleSheet("border: none; background: transparent; color: #fff;");
    connect(m_searchEdit, &QLineEdit::returnPressed, [this](){ m_contentEdit->findText(m_searchEdit->text()); });
    
    QPushButton* btnPrev = new QPushButton();
    btnPrev->setIcon(IconHelper::getIcon("nav_prev", "#ccc"));
    btnPrev->setFixedSize(24, 24);
    btnPrev->setStyleSheet("background: transparent; border: none;");
    connect(btnPrev, &QPushButton::clicked, [this](){ m_contentEdit->findText(m_searchEdit->text(), true); });
    
    QPushButton* btnNext = new QPushButton();
    btnNext->setIcon(IconHelper::getIcon("nav_next", "#ccc"));
    btnNext->setFixedSize(24, 24);
    btnNext->setStyleSheet("background: transparent; border: none;");
    connect(btnNext, &QPushButton::clicked, [this](){ m_contentEdit->findText(m_searchEdit->text(), false); });
    
    QPushButton* btnCls = new QPushButton();
    btnCls->setIcon(IconHelper::getIcon("close", "#ccc"));
    btnCls->setFixedSize(24, 24);
    btnCls->setStyleSheet("background: transparent; border: none;");
    connect(btnCls, &QPushButton::clicked, [this](){ m_searchBar->hide(); });

    sbLayout->addWidget(m_searchEdit);
    sbLayout->addWidget(btnPrev);
    sbLayout->addWidget(btnNext);
    sbLayout->addWidget(btnCls);
    layout->addWidget(m_searchBar);

    layout->addSpacing(5);
    m_contentEdit = new Editor(); 
    m_contentEdit->setPlaceholderText("在这里记录详细内容（支持 Markdown 和粘贴图片）...");
    layout->addWidget(m_contentEdit);
}

void NoteEditWindow::setupShortcuts() {
    new QShortcut(QKeySequence("Ctrl+S"), this, SLOT(saveNote()));
    new QShortcut(QKeySequence("Escape"), this, SLOT(close()));
    new QShortcut(QKeySequence("Ctrl+W"), this, SLOT(close()));
    
    QShortcut* scSearch = new QShortcut(QKeySequence("Ctrl+F"), this);
    connect(scSearch, &QShortcut::activated, this, &NoteEditWindow::toggleSearchBar);
}

void NoteEditWindow::toggleStayOnTop() {
    m_isStayOnTop = m_btnStayOnTop->isChecked();
    m_btnStayOnTop->setIcon(IconHelper::getIcon(m_isStayOnTop ? "pin_vertical" : "pin_tilted", m_isStayOnTop ? "#ffffff" : "#aaaaaa", 20));

    if (isVisible()) {
#ifdef Q_OS_WIN
        HWND hwnd = (HWND)winId();
        SetWindowPos(hwnd, m_isStayOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
#else
        Qt::WindowFlags f = windowFlags();
        if (m_isStayOnTop) f |= Qt::WindowStaysOnTopHint;
        else f &= ~Qt::WindowStaysOnTopHint;
        setWindowFlags(f);
        show();
#endif
    }
}

void NoteEditWindow::toggleMaximize() {
    auto* windowLayout = findChild<QVBoxLayout*>("WindowLayout");
    auto* mainContainer = findChild<QWidget*>("MainContainer");

    if (m_isMaximized) {
        showNormal();
        if (windowLayout) windowLayout->setContentsMargins(15, 15, 15, 15);
        if (mainContainer) mainContainer->setStyleSheet("QWidget#MainContainer { background-color: #1E1E1E; border-radius: 12px; }");
        m_maxBtn->setIcon(IconHelper::getIcon("maximize", "#aaaaaa", 20));
        m_titleBar->setStyleSheet("background-color: #252526; border-top-left-radius: 12px; border-top-right-radius: 12px; border-bottom: 1px solid #333;");
    } else {
        m_normalGeometry = geometry();
        showMaximized();
        if (windowLayout) windowLayout->setContentsMargins(0, 0, 0, 0);
        if (mainContainer) mainContainer->setStyleSheet("QWidget#MainContainer { background-color: #1E1E1E; border-radius: 0px; }");
        m_maxBtn->setIcon(IconHelper::getIcon("restore", "#aaaaaa", 20));
        m_titleBar->setStyleSheet("background-color: #252526; border-radius: 0px; border-bottom: 1px solid #333;");
    }
    m_isMaximized = !m_isMaximized;
    update();
}

void NoteEditWindow::saveNote() {
    QString title = m_titleEdit->text();
    if(title.isEmpty()) title = "未命名灵感";
    QString content = m_contentEdit->toHtml();
    QString tags = m_tagEdit->text();
    int catId = m_catId;
    QString color = m_colorGroup->checkedButton() ? m_colorGroup->checkedButton()->property("color").toString() : "";
    
    if (m_noteId == 0) {
        DatabaseManager::instance().addNoteAsync(title, content, tags.split(","), color, catId);
    } else {
        DatabaseManager::instance().updateNote(m_noteId, title, content, tags.split(","), color, catId);
        DatabaseManager::instance().recordAccess(m_noteId);
    }
    emit noteSaved();
    close();
}

void NoteEditWindow::toggleSearchBar() {
    m_searchBar->setVisible(!m_searchBar->isVisible());
    if (m_searchBar->isVisible()) {
        m_searchEdit->setFocus();
        m_searchEdit->selectAll();
    }
}

void NoteEditWindow::openTagSelector() {
    QStringList currentTags = m_tagEdit->text().split(",", Qt::SkipEmptyParts);
    for (QString& t : currentTags) t = t.trimmed();

    auto* selector = new AdvancedTagSelector(this);
    auto recentTags = DatabaseManager::instance().getRecentTagsWithCounts(20);
    auto allTags = DatabaseManager::instance().getAllTags();
    selector->setup(recentTags, allTags, currentTags);

    connect(selector, &AdvancedTagSelector::tagsConfirmed, [this](const QStringList& tags){
        m_tagEdit->setText(tags.join(", "));
    });

    selector->showAtCursor();
}

void NoteEditWindow::openExpandedTitleEditor() {
    TitleEditorDialog dialog(m_titleEdit->text(), this);
    // 设置初始位置在鼠标附近
    QPoint pos = QCursor::pos();
    dialog.move(pos.x() - 160, pos.y() - 40);
    
    if (dialog.exec() == QDialog::Accepted) {
        QString newTitle = dialog.getText();
        if (!newTitle.isEmpty() && newTitle != m_titleEdit->text()) {
            m_titleEdit->setText(newTitle);
        }
    }
}

void NoteEditWindow::loadNoteData(int id) {
    QVariantMap note = DatabaseManager::instance().getNoteById(id);
    if (!note.isEmpty()) {
        m_titleEdit->setText(note.value("title").toString());
        m_contentEdit->setNote(note, false); // 编辑模式不注入预览标题
        m_tagEdit->setText(note.value("tags").toString());
        
        m_catId = note["category_id"].toInt();
        
        QString color = note["color"].toString();
        for (int i = 0; i < m_colorGroup->buttons().size(); ++i) {
            if (m_colorGroup->button(i)->property("color").toString() == color) {
                m_colorGroup->button(i)->setChecked(true);
                break;
            }
        }
    }
}