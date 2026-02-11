#include "FileSearchWindow.h"
#include "StringUtils.h"

#include "IconHelper.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QDirIterator>
#include <QDesktopServices>
#include <QUrl>
#include <QFileInfo>
#include <QLabel>
#include <QProcess>
#include <QClipboard>
#include <QApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QDir>
#include <QFile>
#include <QToolTip>
#include <QSettings>
#include <QSplitter>
#include <QMenu>
#include <QAction>
#include <QToolButton>
#include <QMimeData>
#include <QDropEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QGraphicsDropShadowEffect>
#include <QPropertyAnimation>
#include <QScrollArea>
#include <functional>
#include <utility>
#include <QSet>
#include <QDateTime>

// ----------------------------------------------------------------------------
// 合并逻辑相关常量与辅助函数
// ----------------------------------------------------------------------------
static const QSet<QString> SUPPORTED_EXTENSIONS = {
    ".py", ".pyw", ".cpp", ".cc", ".cxx", ".c", ".h", ".hpp", ".hxx",
    ".java", ".js", ".jsx", ".ts", ".tsx", ".cs", ".go", ".rs", ".swift",
    ".kt", ".kts", ".php", ".rb", ".lua", ".r", ".m", ".scala", ".sh",
    ".bash", ".zsh", ".ps1", ".bat", ".cmd", ".html", ".htm", ".css",
    ".scss", ".sass", ".less", ".xml", ".svg", ".vue", ".json", ".yaml",
    ".yml", ".toml", ".ini", ".cfg", ".conf", ".env", ".properties",
    ".cmake", ".gradle", ".make", ".mk", ".dockerfile", ".md", ".markdown",
    ".txt", ".rst", ".qml", ".qrc", ".qss", ".ui", ".sql", ".graphql",
    ".gql", ".proto", ".asm", ".s", ".v", ".vh", ".vhdl", ".vhd"
};

static const QSet<QString> SPECIAL_FILENAMES = {
    "Makefile", "makefile", "Dockerfile", "dockerfile", "CMakeLists.txt",
    "Rakefile", "Gemfile", ".gitignore", ".dockerignore", ".editorconfig",
    ".eslintrc", ".prettierrc"
};

static QString getFileLanguage(const QString& filePath) {
    QFileInfo fi(filePath);
    QString basename = fi.fileName();
    QString ext = "." + fi.suffix().toLower();
    
    static const QMap<QString, QString> specialMap = {
        {"Makefile", "makefile"}, {"makefile", "makefile"},
        {"Dockerfile", "dockerfile"}, {"dockerfile", "dockerfile"},
        {"CMakeLists.txt", "cmake"}
    };
    if (specialMap.contains(basename)) return specialMap[basename];

    static const QMap<QString, QString> extMap = {
        {".py", "python"}, {".pyw", "python"}, {".cpp", "cpp"}, {".cc", "cpp"},
        {".cxx", "cpp"}, {".c", "c"}, {".h", "cpp"}, {".hpp", "cpp"},
        {".hxx", "cpp"}, {".java", "java"}, {".js", "javascript"},
        {".jsx", "jsx"}, {".ts", "typescript"}, {".tsx", "tsx"},
        {".cs", "csharp"}, {".go", "go"}, {".rs", "rust"}, {".swift", "swift"},
        {".kt", "kotlin"}, {".kts", "kotlin"}, {".php", "php"}, {".rb", "ruby"},
        {".lua", "lua"}, {".r", "r"}, {".m", "objectivec"}, {".scala", "scala"},
        {".sh", "bash"}, {".bash", "bash"}, {".zsh", "zsh"}, {".ps1", "powershell"},
        {".bat", "batch"}, {".cmd", "batch"}, {".html", "html"}, {".htm", "html"},
        {".css", "css"}, {".scss", "scss"}, {".sass", "sass"}, {".less", "less"},
        {".xml", "xml"}, {".svg", "svg"}, {".vue", "vue"}, {".json", "json"},
        {".yaml", "yaml"}, {".yml", "yaml"}, {".toml", "toml"}, {".ini", "ini"},
        {".cfg", "ini"}, {".conf", "conf"}, {".env", "bash"},
        {".properties", "properties"}, {".cmake", "cmake"}, {".gradle", "gradle"},
        {".make", "makefile"}, {".mk", "makefile"}, {".dockerfile", "dockerfile"},
        {".md", "markdown"}, {".markdown", "markdown"}, {".txt", "text"},
        {".rst", "restructuredtext"}, {".qml", "qml"}, {".qrc", "xml"},
        {".qss", "css"}, {".ui", "xml"}, {".sql", "sql"}, {".graphql", "graphql"},
        {".gql", "graphql"}, {".proto", "protobuf"}, {".asm", "asm"},
        {".s", "asm"}, {".v", "verilog"}, {".vh", "verilog"}, {".vhdl", "vhdl"},
        {".vhd", "vhdl"}
    };
    return extMap.value(ext, ext.mid(1).isEmpty() ? "text" : ext.mid(1));
}

static bool isSupportedFile(const QString& filePath) {
    QFileInfo fi(filePath);
    if (SPECIAL_FILENAMES.contains(fi.fileName())) return true;
    return SUPPORTED_EXTENSIONS.contains("." + fi.suffix().toLower());
}

// ----------------------------------------------------------------------------
// PathHistory 相关辅助类 (复刻 SearchHistoryPopup 逻辑)
// ----------------------------------------------------------------------------
class PathChip : public QFrame {
    Q_OBJECT
public:
    PathChip(const QString& text, QWidget* parent = nullptr) : QFrame(parent), m_text(text) {
        setAttribute(Qt::WA_StyledBackground);
        setCursor(Qt::PointingHandCursor);
        setObjectName("PathChip");
        
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(10, 6, 10, 6);
        layout->setSpacing(10);
        
        auto* lbl = new QLabel(text);
        lbl->setStyleSheet("border: none; background: transparent; color: #DDD; font-size: 13px;");
        layout->addWidget(lbl);
        layout->addStretch();
        
        auto* btnDel = new QPushButton();
        btnDel->setIcon(IconHelper::getIcon("close", "#666", 16));
        btnDel->setIconSize(QSize(10, 10));
        btnDel->setFixedSize(16, 16);
        btnDel->setCursor(Qt::PointingHandCursor);
        btnDel->setStyleSheet(
            "QPushButton { background-color: transparent; border-radius: 4px; padding: 0px; }"
            "QPushButton:hover { background-color: #E74C3C; }"
        );
        
        connect(btnDel, &QPushButton::clicked, this, [this](){ emit deleted(m_text); });
        layout->addWidget(btnDel);

        setStyleSheet(
            "#PathChip { background-color: transparent; border: none; border-radius: 4px; }"
            "#PathChip:hover { background-color: #3E3E42; }"
        );
    }
    
    void mousePressEvent(QMouseEvent* e) override { 
        if(e->button() == Qt::LeftButton) emit clicked(m_text); 
        QFrame::mousePressEvent(e);
    }

signals:
    void clicked(const QString& text);
    void deleted(const QString& text);
private:
    QString m_text;
};

// ----------------------------------------------------------------------------
// Sidebar ListWidget subclass for Drag & Drop
// ----------------------------------------------------------------------------
class FileSidebarListWidget : public QListWidget {
    Q_OBJECT
public:
    explicit FileSidebarListWidget(QWidget* parent = nullptr) : QListWidget(parent) {
        setAcceptDrops(true);
    }
signals:
    void folderDropped(const QString& path);
protected:
    void dragEnterEvent(QDragEnterEvent* event) override {
        if (event->mimeData()->hasUrls() || event->mimeData()->hasText()) {
            event->acceptProposedAction();
        }
    }
    void dragMoveEvent(QDragMoveEvent* event) override {
        event->acceptProposedAction();
    }
    void dropEvent(QDropEvent* event) override {
        QString path;
        if (event->mimeData()->hasUrls()) {
            path = event->mimeData()->urls().at(0).toLocalFile();
        } else if (event->mimeData()->hasText()) {
            path = event->mimeData()->text();
        }
        
        if (!path.isEmpty() && QDir(path).exists()) {
            emit folderDropped(path);
            event->acceptProposedAction();
        }
    }
};

class FileSearchHistoryPopup : public QWidget {
    Q_OBJECT
public:
    enum Type { Path, Filename };

    explicit FileSearchHistoryPopup(FileSearchWindow* window, QLineEdit* edit, Type type) 
        : QWidget(window->window(), Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint) 
    {
        m_window = window;
        m_edit = edit;
        m_type = type;
        setAttribute(Qt::WA_TranslucentBackground);
        
        auto* rootLayout = new QVBoxLayout(this);
        rootLayout->setContentsMargins(12, 12, 12, 12);
        
        auto* container = new QFrame();
        container->setObjectName("PopupContainer");
        container->setStyleSheet(
            "#PopupContainer { background-color: #252526; border: 1px solid #444; border-radius: 10px; }"
        );
        rootLayout->addWidget(container);

        auto* layout = new QVBoxLayout(container);
        layout->setContentsMargins(12, 12, 12, 12);
        layout->setSpacing(10);

        auto* top = new QHBoxLayout();
        auto* icon = new QLabel();
        icon->setPixmap(IconHelper::getIcon("clock", "#888").pixmap(14, 14));
        icon->setStyleSheet("border: none; background: transparent;");
        top->addWidget(icon);

        auto* title = new QLabel(m_type == Path ? "最近扫描路径" : "最近搜索文件名");
        title->setStyleSheet("color: #888; font-weight: bold; font-size: 11px; background: transparent; border: none;");
        top->addWidget(title);
        top->addStretch();
        auto* clearBtn = new QPushButton("清空");
        clearBtn->setCursor(Qt::PointingHandCursor);
        clearBtn->setStyleSheet("QPushButton { background: transparent; color: #666; border: none; font-size: 11px; } QPushButton:hover { color: #E74C3C; }");
        connect(clearBtn, &QPushButton::clicked, [this](){
            if (m_type == Path) m_window->clearHistory();
            else m_window->clearSearchHistory();
            refreshUI();
        });
        top->addWidget(clearBtn);
        layout->addLayout(top);

        auto* scroll = new QScrollArea();
        scroll->setWidgetResizable(true);
        scroll->setStyleSheet(
            "QScrollArea { background-color: transparent; border: none; }"
            "QScrollArea > QWidget > QWidget { background-color: transparent; }"
        );
        scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

        m_chipsWidget = new QWidget();
        m_chipsWidget->setStyleSheet("background-color: transparent;");
        m_vLayout = new QVBoxLayout(m_chipsWidget);
        m_vLayout->setContentsMargins(0, 0, 0, 0);
        m_vLayout->setSpacing(2);
        m_vLayout->addStretch();
        scroll->setWidget(m_chipsWidget);
        layout->addWidget(scroll);

        m_opacityAnim = new QPropertyAnimation(this, "windowOpacity");
        m_opacityAnim->setDuration(200);
    }

    void refreshUI() {
        QLayoutItem* item;
        while ((item = m_vLayout->takeAt(0))) {
            if(item->widget()) item->widget()->deleteLater();
            delete item;
        }
        m_vLayout->addStretch();
        
        QStringList history = (m_type == Path) ? m_window->getHistory() : m_window->getSearchHistory();
        if(history.isEmpty()) {
            auto* lbl = new QLabel("暂无历史记录");
            lbl->setAlignment(Qt::AlignCenter);
            lbl->setStyleSheet("color: #555; font-style: italic; margin: 20px; border: none;");
            m_vLayout->insertWidget(0, lbl);
        } else {
            for(const QString& val : std::as_const(history)) {
                auto* chip = new PathChip(val);
                chip->setFixedHeight(32);
                connect(chip, &PathChip::clicked, this, [this](const QString& v){ 
                    if (m_type == Path) m_window->useHistoryPath(v);
                    else m_edit->setText(v);
                    close(); 
                });
                connect(chip, &PathChip::deleted, this, [this](const QString& v){ 
                    if (m_type == Path) m_window->removeHistoryEntry(v);
                    else m_window->removeSearchHistoryEntry(v);
                    refreshUI(); 
                });
                m_vLayout->insertWidget(m_vLayout->count() - 1, chip);
            }
        }
        
        int targetWidth = m_edit->width();
        // 统一高度为 410px，确保视觉一致性，不论记录多少（如同图一的效果）
        int contentHeight = 410;
        resize(targetWidth + 24, contentHeight);
    }

    void showAnimated() {
        refreshUI();
        QPoint pos = m_edit->mapToGlobal(QPoint(0, m_edit->height()));
        move(pos.x() - 12, pos.y() - 7);
        setWindowOpacity(0);
        show();
        m_opacityAnim->setStartValue(0);
        m_opacityAnim->setEndValue(1);
        m_opacityAnim->start();
    }

private:
    FileSearchWindow* m_window;
    QLineEdit* m_edit;
    Type m_type;
    QWidget* m_chipsWidget;
    QVBoxLayout* m_vLayout;
    QPropertyAnimation* m_opacityAnim;
};

// ----------------------------------------------------------------------------
// ScannerThread 实现
// ----------------------------------------------------------------------------
ScannerThread::ScannerThread(const QString& folderPath, QObject* parent)
    : QThread(parent), m_folderPath(folderPath) {}

void ScannerThread::stop() {
    m_isRunning = false;
    wait();
}

void ScannerThread::run() {
    int count = 0;
    if (m_folderPath.isEmpty() || !QDir(m_folderPath).exists()) {
        emit finished(0);
        return;
    }

    QStringList ignored = {".git", ".idea", "__pycache__", "node_modules", "$RECYCLE.BIN", "System Volume Information"};
    
    // 使用 std::function 实现递归扫描，支持目录剪枝
    std::function<void(const QString&)> scanDir = [&](const QString& currentPath) {
        if (!m_isRunning) return;

        QDir dir(currentPath);
        // 1. 获取当前目录下所有文件
        QFileInfoList files = dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden);
        for (const auto& fi : std::as_const(files)) {
            if (!m_isRunning) return;
            bool hidden = fi.isHidden();
            // 在某些平台上，以 . 开头的文件可能没被标记为 hidden，但通常我们也视为隐性文件
            if (!hidden && fi.fileName().startsWith('.')) hidden = true;
            
            emit fileFound(fi.fileName(), fi.absoluteFilePath(), hidden);
            count++;
        }

        // 2. 获取子目录并递归 (排除忽略列表)
        QFileInfoList subDirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
        for (const auto& di : std::as_const(subDirs)) {
            if (!m_isRunning) return;
            if (!ignored.contains(di.fileName())) {
                scanDir(di.absoluteFilePath());
            }
        }
    };

    scanDir(m_folderPath);
    emit finished(count);
}

// ----------------------------------------------------------------------------
// ResizeHandle 实现
// ----------------------------------------------------------------------------
ResizeHandle::ResizeHandle(QWidget* target, QWidget* parent) 
    : QWidget(parent), m_target(target) 
{
    setFixedSize(20, 20);
    setCursor(Qt::SizeFDiagCursor);
}

void ResizeHandle::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_startPos = event->globalPosition().toPoint();
        m_startSize = m_target->size();
        event->accept();
    }
}

void ResizeHandle::mouseMoveEvent(QMouseEvent* event) {
    if (event->buttons() & Qt::LeftButton) {
        QPoint delta = event->globalPosition().toPoint() - m_startPos;
        int newW = qMax(m_startSize.width() + delta.x(), 600);
        int newH = qMax(m_startSize.height() + delta.y(), 400);
        m_target->resize(newW, newH);
        event->accept();
    }
}

// ----------------------------------------------------------------------------
// FileSearchWindow 实现
// ----------------------------------------------------------------------------
FileSearchWindow::FileSearchWindow(QWidget* parent) 
    : FramelessDialog("查找文件", parent) 
{
    resize(1000, 680);
    setupStyles();
    initUI();
    loadFavorites();
    m_resizeHandle = new ResizeHandle(this, this);
    m_resizeHandle->raise();
}

FileSearchWindow::~FileSearchWindow() {
    if (m_scanThread) {
        m_scanThread->stop();
        m_scanThread->deleteLater();
    }
}

void FileSearchWindow::setupStyles() {
    // 1:1 复刻 Python 脚本中的 STYLESHEET
    setStyleSheet(R"(
        QWidget {
            font-family: "Microsoft YaHei", "Segoe UI", sans-serif;
            font-size: 14px;
            color: #E0E0E0;
            outline: none;
        }
        QSplitter::handle {
            background-color: #333;
        }
        QListWidget {
            background-color: #252526; 
            border: 1px solid #333333;
            border-radius: 6px;
            padding: 4px;
        }
        QListWidget::item {
            height: 30px;
            padding-left: 8px;
            border-radius: 4px;
            color: #CCCCCC;
        }
        QListWidget::item:selected {
            background-color: #37373D;
            border-left: 3px solid #007ACC;
            color: #FFFFFF;
        }
        #SidebarList::item:selected {
            background-color: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #007ACC, stop:0.015 #007ACC, stop:0.016 #37373D, stop:1 #37373D);
            color: #FFFFFF;
            border-radius: 4px;
        }
        QListWidget::item:hover {
            background-color: #2A2D2E;
        }
        QLineEdit {
            background-color: #333333;
            border: 1px solid #444444;
            color: #FFFFFF;
            border-radius: 6px;
            padding: 8px;
            selection-background-color: #264F78;
        }
        QLineEdit:focus {
            border: 1px solid #007ACC;
            background-color: #2D2D2D;
        }
        #ActionBtn {
            background-color: #007ACC;
            color: white;
            border: none;
            border-radius: 6px;
            font-weight: bold;
        }
        #ActionBtn:hover {
            background-color: #0062A3;
        }
        QScrollBar:vertical {
            background: transparent;
            width: 8px;
            margin: 0px;
        }
        QScrollBar::handle:vertical {
            background: #555555;
            min-height: 20px;
            border-radius: 4px;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0px;
        }
    )");
}

void FileSearchWindow::initUI() {
    auto* mainLayout = new QHBoxLayout(m_contentArea);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(0);

    auto* splitter = new QSplitter(Qt::Horizontal);
    mainLayout->addWidget(splitter);

    // --- 左侧边栏 ---
    auto* sidebarWidget = new QWidget();
    auto* sidebarLayout = new QVBoxLayout(sidebarWidget);
    sidebarLayout->setContentsMargins(0, 0, 5, 0);
    sidebarLayout->setSpacing(10);

    auto* headerLayout = new QHBoxLayout();
    headerLayout->setSpacing(5);
    auto* sidebarIcon = new QLabel();
    sidebarIcon->setPixmap(IconHelper::getIcon("folder", "#888").pixmap(14, 14));
    sidebarIcon->setStyleSheet("border: none; background: transparent;");
    headerLayout->addWidget(sidebarIcon);

    auto* sidebarHeader = new QLabel("收藏夹 (可拖入)");
    sidebarHeader->setStyleSheet("color: #888; font-weight: bold; font-size: 12px; border: none; background: transparent;");
    headerLayout->addWidget(sidebarHeader);
    headerLayout->addStretch();
    sidebarLayout->addLayout(headerLayout);

    auto* sidebar = new FileSidebarListWidget();
    m_sidebar = sidebar;
    m_sidebar->setObjectName("SidebarList");
    m_sidebar->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_sidebar->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_sidebar->setMinimumWidth(200);
    m_sidebar->setDragEnabled(false);
    m_sidebar->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(sidebar, &FileSidebarListWidget::folderDropped, this, &FileSearchWindow::addFavorite);
    connect(m_sidebar, &QListWidget::itemClicked, this, &FileSearchWindow::onSidebarItemClicked);
    connect(m_sidebar, &QListWidget::customContextMenuRequested, this, &FileSearchWindow::showSidebarContextMenu);
    sidebarLayout->addWidget(m_sidebar);

    auto* btnAddFav = new QPushButton("收藏当前路径");
    btnAddFav->setFixedHeight(32);
    btnAddFav->setCursor(Qt::PointingHandCursor);
    btnAddFav->setStyleSheet(
        "QPushButton { background-color: #2D2D30; border: 1px solid #444; color: #AAA; border-radius: 4px; font-size: 12px; }"
        "QPushButton:hover { background-color: #3E3E42; color: #FFF; border-color: #666; }"
    );
    connect(btnAddFav, &QPushButton::clicked, this, [this](){
        QString p = m_pathInput->text().trimmed();
        if (QDir(p).exists()) addFavorite(p);
    });
    sidebarLayout->addWidget(btnAddFav);

    splitter->addWidget(sidebarWidget);

    // --- 右侧主区域 ---
    auto* rightWidget = new QWidget();
    auto* layout = new QVBoxLayout(rightWidget);
    layout->setContentsMargins(5, 0, 0, 0);
    layout->setSpacing(10);

    // 第一行：路径输入与浏览
    auto* pathLayout = new QHBoxLayout();
    m_pathInput = new QLineEdit();
    m_pathInput->setPlaceholderText("双击查看历史，或在此粘贴路径...");
    m_pathInput->setClearButtonEnabled(true);
    m_pathInput->installEventFilter(this);
    connect(m_pathInput, &QLineEdit::returnPressed, this, &FileSearchWindow::onPathReturnPressed);
    
    auto* btnScan = new QToolButton();
    btnScan->setIcon(IconHelper::getIcon("scan", "#1abc9c", 18));
    btnScan->setToolTip(StringUtils::wrapToolTip("开始扫描"));
    btnScan->setFixedSize(38, 38);
    btnScan->setCursor(Qt::PointingHandCursor);
    btnScan->setStyleSheet("QToolButton { border: 1px solid #444; background: #2D2D30; border-radius: 6px; }"
                           "QToolButton:hover { background-color: #3E3E42; border-color: #007ACC; }");
    connect(btnScan, &QToolButton::clicked, this, &FileSearchWindow::onPathReturnPressed);

    auto* btnBrowse = new QToolButton();
    btnBrowse->setObjectName("ActionBtn");
    btnBrowse->setIcon(IconHelper::getIcon("folder", "#ffffff", 18));
    btnBrowse->setToolTip(StringUtils::wrapToolTip("浏览文件夹"));
    btnBrowse->setFixedSize(38, 38);
    btnBrowse->setCursor(Qt::PointingHandCursor);
    connect(btnBrowse, &QToolButton::clicked, this, &FileSearchWindow::selectFolder);

    pathLayout->addWidget(m_pathInput);
    pathLayout->addWidget(btnScan);
    pathLayout->addWidget(btnBrowse);
    layout->addLayout(pathLayout);

    // 第二行：搜索过滤与后缀名
    auto* searchLayout = new QHBoxLayout();
    m_searchInput = new QLineEdit();
    m_searchInput->setPlaceholderText("输入文件名过滤...");
    m_searchInput->setClearButtonEnabled(true);
    m_searchInput->installEventFilter(this);
    connect(m_searchInput, &QLineEdit::textChanged, this, &FileSearchWindow::refreshList);
    connect(m_searchInput, &QLineEdit::returnPressed, this, [this](){
        addSearchHistoryEntry(m_searchInput->text().trimmed());
    });

    m_extInput = new QLineEdit();
    m_extInput->setPlaceholderText("后缀 (如 py)");
    m_extInput->setClearButtonEnabled(true);
    m_extInput->setFixedWidth(120);
    connect(m_extInput, &QLineEdit::textChanged, this, &FileSearchWindow::refreshList);

    searchLayout->addWidget(m_searchInput);
    searchLayout->addWidget(m_extInput);
    layout->addLayout(searchLayout);

    // 信息标签与显示隐藏文件勾选
    auto* infoLayout = new QHBoxLayout();
    m_infoLabel = new QLabel("等待操作...");
    m_infoLabel->setStyleSheet("color: #888888; font-size: 12px;");
    
    m_showHiddenCheck = new QCheckBox("显示隐性文件");
    m_showHiddenCheck->setStyleSheet(R"(
        QCheckBox { color: #888; font-size: 12px; spacing: 5px; }
        QCheckBox::indicator { width: 15px; height: 15px; border: 1px solid #444; border-radius: 3px; background: #2D2D30; }
        QCheckBox::indicator:checked { background-color: #007ACC; border-color: #007ACC; }
        QCheckBox::indicator:hover { border-color: #666; }
    )");
    connect(m_showHiddenCheck, &QCheckBox::toggled, this, &FileSearchWindow::refreshList);

    infoLayout->addWidget(m_infoLabel);
    infoLayout->addWidget(m_showHiddenCheck);
    infoLayout->addStretch();
    layout->addLayout(infoLayout);

    // 列表标题与复制全部按钮
    auto* listHeaderLayout = new QHBoxLayout();
    listHeaderLayout->setContentsMargins(0, 0, 0, 0);
    auto* listTitle = new QLabel("搜索结果");
    listTitle->setStyleSheet("color: #888; font-size: 11px; font-weight: bold; border: none; background: transparent;");
    
    auto* btnCopyAll = new QToolButton();
    btnCopyAll->setIcon(IconHelper::getIcon("copy", "#1abc9c", 14));
    btnCopyAll->setToolTip(StringUtils::wrapToolTip("复制全部搜索结果的路径"));
    btnCopyAll->setFixedSize(20, 20);
    btnCopyAll->setCursor(Qt::PointingHandCursor);
    btnCopyAll->setStyleSheet("QToolButton { border: none; background: transparent; padding: 2px; }"
                               "QToolButton:hover { background-color: #3E3E42; border-radius: 4px; }");
    connect(btnCopyAll, &QToolButton::clicked, this, [this](){
        if (m_fileList->count() == 0) {
            QToolTip::showText(QCursor::pos(), StringUtils::wrapToolTip("<b style='color:#e74c3c;'>✖ 结果列表为空</b>"), this, {}, 2000);
            return;
        }
        QStringList paths;
        for (int i = 0; i < m_fileList->count(); ++i) {
            QString p = m_fileList->item(i)->data(Qt::UserRole).toString();
            if (!p.isEmpty()) paths << p;
        }
        if (paths.isEmpty()) return;
        QApplication::clipboard()->setText(paths.join("\n"));
        QToolTip::showText(QCursor::pos(), StringUtils::wrapToolTip("<b style='color:#2ecc71;'>✔ 已复制全部搜索结果</b>"), this, {}, 2000);
    });

    listHeaderLayout->addWidget(listTitle);
    listHeaderLayout->addStretch();
    listHeaderLayout->addWidget(btnCopyAll);
    layout->addLayout(listHeaderLayout);

    // 文件列表
    m_fileList = new QListWidget();
    m_fileList->setObjectName("FileList");
    m_fileList->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_fileList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_fileList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_fileList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_fileList, &QListWidget::customContextMenuRequested, this, &FileSearchWindow::showFileContextMenu);
    
    // 快捷键支持
    auto* actionSelectAll = new QAction(this);
    actionSelectAll->setShortcut(QKeySequence("Ctrl+A"));
    actionSelectAll->setShortcutContext(Qt::WidgetShortcut);
    connect(actionSelectAll, &QAction::triggered, [this](){ m_fileList->selectAll(); });
    m_fileList->addAction(actionSelectAll);

    auto* actionCopy = new QAction(this);
    actionCopy->setShortcut(QKeySequence("Ctrl+C"));
    actionCopy->setShortcutContext(Qt::WidgetShortcut);
    connect(actionCopy, &QAction::triggered, this, [this](){ copySelectedFiles(); });
    m_fileList->addAction(actionCopy);

    auto* actionDelete = new QAction(this);
    actionDelete->setShortcut(QKeySequence(Qt::Key_Delete));
    connect(actionDelete, &QAction::triggered, this, [this](){ onDeleteFile(); });
    m_fileList->addAction(actionDelete);

    layout->addWidget(m_fileList);

    splitter->addWidget(rightWidget);
    splitter->setStretchFactor(1, 1);
}

void FileSearchWindow::selectFolder() {
    QString d = QFileDialog::getExistingDirectory(this, "选择文件夹");
    if (!d.isEmpty()) {
        m_pathInput->setText(d);
        startScan(d);
    }
}

void FileSearchWindow::onPathReturnPressed() {
    QString p = m_pathInput->text().trimmed();
    if (QDir(p).exists()) {
        startScan(p);
    } else {
        m_infoLabel->setText("路径不存在");
        m_pathInput->setStyleSheet("border: 1px solid #FF3333;");
    }
}

void FileSearchWindow::startScan(const QString& path) {
    m_pathInput->setStyleSheet("");
    if (m_scanThread) {
        m_scanThread->stop();
        m_scanThread->deleteLater();
    }

    m_fileList->clear();
    m_filesData.clear();
    m_visibleCount = 0;
    m_hiddenCount = 0;
    m_infoLabel->setText("正在扫描: " + path);

    m_scanThread = new ScannerThread(path, this);
    connect(m_scanThread, &ScannerThread::fileFound, this, &FileSearchWindow::onFileFound);
    connect(m_scanThread, &ScannerThread::finished, this, &FileSearchWindow::onScanFinished);
    m_scanThread->start();
}

void FileSearchWindow::onFileFound(const QString& name, const QString& path, bool isHidden) {
    m_filesData.append({name, path, isHidden});
    if (isHidden) m_hiddenCount++;
    else m_visibleCount++;

    if (m_filesData.size() % 300 == 0) {
        m_infoLabel->setText(QString("已发现 %1 个文件 (可见:%2 隐性:%3)...").arg(m_filesData.size()).arg(m_visibleCount).arg(m_hiddenCount));
    }
}

void FileSearchWindow::onScanFinished(int count) {
    m_infoLabel->setText(QString("扫描结束，共 %1 个文件 (可见:%2 隐性:%3)").arg(count).arg(m_visibleCount).arg(m_hiddenCount));
    addHistoryEntry(m_pathInput->text().trimmed());
    
    // 按文件名排序 (不按目录)
    std::sort(m_filesData.begin(), m_filesData.end(), [](const FileData& a, const FileData& b){
        return a.name.localeAwareCompare(b.name) < 0;
    });

    refreshList();
}

void FileSearchWindow::refreshList() {
    m_fileList->clear();
    QString txt = m_searchInput->text().toLower();
    QString ext = m_extInput->text().toLower().trimmed();
    if (ext.startsWith(".")) ext = ext.mid(1);

    bool showHidden = m_showHiddenCheck->isChecked();

    int limit = 500;
    int shown = 0;

    for (const auto& data : std::as_const(m_filesData)) {
        if (!showHidden && data.isHidden) continue;
        if (!ext.isEmpty() && !data.name.toLower().endsWith("." + ext)) continue;
        if (!txt.isEmpty() && !data.name.toLower().contains(txt)) continue;

        auto* item = new QListWidgetItem(data.name);
        item->setData(Qt::UserRole, data.path);
        item->setToolTip(StringUtils::wrapToolTip(data.path));
        m_fileList->addItem(item);
        
        shown++;
        if (shown >= limit) {
            auto* warn = new QListWidgetItem("--- 结果过多，仅显示前 500 条 ---");
            warn->setForeground(QColor(255, 170, 0));
            warn->setTextAlignment(Qt::AlignCenter);
            warn->setFlags(Qt::NoItemFlags);
            m_fileList->addItem(warn);
            break;
        }
    }
}

void FileSearchWindow::showFileContextMenu(const QPoint& pos) {
    auto selectedItems = m_fileList->selectedItems();
    if (selectedItems.isEmpty()) {
        auto* item = m_fileList->itemAt(pos);
        if (item) {
            item->setSelected(true);
            selectedItems << item;
        }
    }

    if (selectedItems.isEmpty()) return;

    QStringList paths;
    for (auto* item : std::as_const(selectedItems)) {
        QString p = item->data(Qt::UserRole).toString();
        if (!p.isEmpty()) paths << p;
    }

    if (paths.isEmpty()) return;

    QMenu menu(this);
    menu.setStyleSheet("QMenu { background-color: #2D2D30; border: 1px solid #444; color: #EEE; } QMenu::item:selected { background-color: #3E3E42; }");
    
    if (selectedItems.size() == 1) {
        QString filePath = paths.first();
        menu.addAction(IconHelper::getIcon("folder", "#F1C40F"), "定位文件夹", [filePath](){
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(filePath).absolutePath()));
        });
        menu.addAction(IconHelper::getIcon("search", "#4A90E2"), "定位文件", [filePath](){
#ifdef Q_OS_WIN
            QStringList args;
            args << "/select," << QDir::toNativeSeparators(filePath);
            QProcess::startDetached("explorer.exe", args);
#endif
        });
        menu.addAction(IconHelper::getIcon("edit", "#3498DB"), "编辑", [this](){ onEditFile(); });
        menu.addSeparator();
    }

    QString copyPathText = selectedItems.size() > 1 ? "复制选中路径" : "复制完整路径";
    menu.addAction(IconHelper::getIcon("copy", "#2ECC71"), copyPathText, [paths](){
        QApplication::clipboard()->setText(paths.join("\n"));
    });

    QString copyFileText = selectedItems.size() > 1 ? "复制选中文件" : "复制文件";
    menu.addAction(IconHelper::getIcon("file", "#4A90E2"), copyFileText, [this](){ copySelectedFiles(); });

    menu.addAction(IconHelper::getIcon("merge", "#3498DB"), "合并选中内容", [this](){ onMergeSelectedFiles(); });

    menu.addSeparator();
    menu.addAction(IconHelper::getIcon("cut", "#E67E22"), "剪切", [this](){ onCutFile(); });
    menu.addAction(IconHelper::getIcon("trash", "#E74C3C"), "删除", [this](){ onDeleteFile(); });

    menu.exec(m_fileList->mapToGlobal(pos));
}

void FileSearchWindow::onEditFile() {
    auto selectedItems = m_fileList->selectedItems();
    if (selectedItems.isEmpty()) {
        QToolTip::showText(QCursor::pos(), StringUtils::wrapToolTip("<b style='color:#e74c3c;'>✖ 请先选择要操作的内容</b>"), this, {}, 2000);
        return;
    }

    QStringList paths;
    for (auto* item : std::as_const(selectedItems)) {
        QString p = item->data(Qt::UserRole).toString();
        if (!p.isEmpty()) paths << p;
    }
    if (paths.isEmpty()) return;

    QSettings settings("RapidNotes", "ExternalEditor");
    QString editorPath = settings.value("EditorPath").toString();

    // 尝试寻找 Notepad++
    if (editorPath.isEmpty() || !QFile::exists(editorPath)) {
        QStringList commonPaths = {
            "C:/Program Files/Notepad++/notepad++.exe",
            "C:/Program Files (x86)/Notepad++/notepad++.exe"
        };
        for (const QString& p : commonPaths) {
            if (QFile::exists(p)) {
                editorPath = p;
                break;
            }
        }
    }

    // 如果还没找到，让用户选择
    if (editorPath.isEmpty() || !QFile::exists(editorPath)) {
        editorPath = QFileDialog::getOpenFileName(this, "选择编辑器 (推荐 Notepad++)", "C:/Program Files", "Executable (*.exe)");
        if (editorPath.isEmpty()) return;
        settings.setValue("EditorPath", editorPath);
    }

    for (const QString& filePath : paths) {
        QProcess::startDetached(editorPath, { QDir::toNativeSeparators(filePath) });
    }
}

void FileSearchWindow::copySelectedFiles() {
    auto selectedItems = m_fileList->selectedItems();
    if (selectedItems.isEmpty()) {
        QToolTip::showText(QCursor::pos(), StringUtils::wrapToolTip("<b style='color:#e74c3c;'>✖ 请先选择要操作的内容</b>"), this, {}, 2000);
        return;
    }

    QList<QUrl> urls;
    QStringList paths;
    for (auto* item : std::as_const(selectedItems)) {
        QString p = item->data(Qt::UserRole).toString();
        if (!p.isEmpty()) {
            urls << QUrl::fromLocalFile(p);
            paths << p;
        }
    }
    if (urls.isEmpty()) return;

    QMimeData* mimeData = new QMimeData();
    mimeData->setUrls(urls);
    mimeData->setText(paths.join("\n"));

    QApplication::clipboard()->setMimeData(mimeData);

    QString msg = selectedItems.size() > 1 ? QString("✔ 已复制 %1 个文件").arg(selectedItems.size()) : "✔ 已复制到剪贴板";
    QToolTip::showText(QCursor::pos(), StringUtils::wrapToolTip(QString("<b style='color: #2ecc71;'>%1</b>").arg(msg)), this);
}

void FileSearchWindow::onCutFile() {
    auto selectedItems = m_fileList->selectedItems();
    if (selectedItems.isEmpty()) {
        QToolTip::showText(QCursor::pos(), StringUtils::wrapToolTip("<b style='color:#e74c3c;'>✖ 请先选择要操作的内容</b>"), this, {}, 2000);
        return;
    }

    QList<QUrl> urls;
    for (auto* item : std::as_const(selectedItems)) {
        QString p = item->data(Qt::UserRole).toString();
        if (!p.isEmpty()) urls << QUrl::fromLocalFile(p);
    }
    if (urls.isEmpty()) return;

    QMimeData* mimeData = new QMimeData();
    mimeData->setUrls(urls);
    
#ifdef Q_OS_WIN
    // 设置 Preferred DropEffect 为 2 (DROPEFFECT_MOVE)，通知资源管理器这是“剪切”操作
    QByteArray data;
    data.resize(4);
    data[0] = 2; // DROPEFFECT_MOVE
    data[1] = 0;
    data[2] = 0;
    data[3] = 0;
    mimeData->setData("Preferred DropEffect", data);
#endif

    QApplication::clipboard()->setMimeData(mimeData);

    QString msg = selectedItems.size() > 1 ? QString("✔ 已剪切 %1 个文件").arg(selectedItems.size()) : "✔ 已剪切到剪贴板";
    QToolTip::showText(QCursor::pos(), StringUtils::wrapToolTip(QString("<b style='color: #2ecc71;'>%1</b>").arg(msg)), this);
}

void FileSearchWindow::onDeleteFile() {
    auto selectedItems = m_fileList->selectedItems();
    if (selectedItems.isEmpty()) {
        QToolTip::showText(QCursor::pos(), StringUtils::wrapToolTip("<b style='color:#e74c3c;'>✖ 请先选择要操作的内容</b>"), this, {}, 2000);
        return;
    }

    int successCount = 0;
    for (auto* item : std::as_const(selectedItems)) {
        QString filePath = item->data(Qt::UserRole).toString();
        if (filePath.isEmpty()) continue;

        if (QFile::moveToTrash(filePath)) {
            successCount++;
            // 从内存数据中移除
            for (int i = 0; i < m_filesData.size(); ++i) {
                if (m_filesData[i].path == filePath) {
                    m_filesData.removeAt(i);
                    break;
                }
            }
            delete item; // 从界面移除 (QListWidget 负责管理内存)
        }
    }

    if (successCount > 0) {
        QString msg = selectedItems.size() > 1 ? QString("✔ %1 个文件已移至回收站").arg(successCount) : "✔ 文件已移至回收站";
        QToolTip::showText(QCursor::pos(), StringUtils::wrapToolTip(QString("<b style='color: #2ecc71;'>%1</b>").arg(msg)), this);
        m_infoLabel->setText(msg);
    } else if (!selectedItems.isEmpty()) {
        QToolTip::showText(QCursor::pos(), StringUtils::wrapToolTip("<b style='color: #e74c3c;'>✖ 无法删除文件，请检查是否被占用</b>"), this);
    }
}

void FileSearchWindow::onMergeFiles(const QStringList& filePaths, const QString& rootPath) {
    if (filePaths.isEmpty()) {
        QToolTip::showText(QCursor::pos(), StringUtils::wrapToolTip("<b style='color:#e74c3c;'>✖ 没有可合并的文件</b>"), this, {}, 2000);
        return;
    }

    QString ts = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString outName = QString("%1_code_export.md").arg(ts);
    QString outPath = QDir(rootPath).filePath(outName);

    QFile outFile(outPath);
    if (!outFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QToolTip::showText(QCursor::pos(), StringUtils::wrapToolTip("<b style='color:#e74c3c;'>✖ 无法创建输出文件</b>"), this, {}, 2000);
        return;
    }

    QTextStream out(&outFile);
    out.setEncoding(QStringConverter::Utf8);

    out << "# 代码导出结果 - " << ts << "\n\n";
    out << "**项目路径**: `" << rootPath << "`\n\n";
    out << "**文件总数**: " << filePaths.size() << "\n\n";

    QMap<QString, int> fileStats;
    for (const QString& fp : filePaths) {
        QString lang = getFileLanguage(fp);
        fileStats[lang]++;
    }

    out << "## 文件类型统计\n\n";
    QStringList langs = fileStats.keys();
    std::sort(langs.begin(), langs.end(), [&](const QString& a, const QString& b){
        return fileStats.value(a) > fileStats.value(b);
    });
    for (const QString& lang : std::as_const(langs)) {
        out << "- **" << lang << "**: " << fileStats.value(lang) << " 个文件\n";
    }
    out << "\n---\n\n";

    for (const QString& fp : filePaths) {
        QString relPath = QDir(rootPath).relativeFilePath(fp);
        QString lang = getFileLanguage(fp);

        out << "## 文件: `" << relPath << "`\n\n";
        out << "```" << lang << "\n";

        QFile inFile(fp);
        if (inFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QByteArray content = inFile.readAll();
            out << QString::fromUtf8(content);
            if (!content.endsWith('\n')) out << "\n";
        } else {
            out << "# 读取文件失败\n";
        }
        out << "```\n\n";
    }

    outFile.close();
    
    QString msg = QString("✔ 已保存: %1 (%2个文件)").arg(outName).arg(filePaths.size());
    QToolTip::showText(QCursor::pos(), StringUtils::wrapToolTip(QString("<b style='color: #2ecc71;'>%1</b>").arg(msg)), this, {}, 3000);
}

void FileSearchWindow::onMergeSelectedFiles() {
    auto selectedItems = m_fileList->selectedItems();
    if (selectedItems.isEmpty()) return;

    QStringList paths;
    for (auto* item : std::as_const(selectedItems)) {
        QString p = item->data(Qt::UserRole).toString();
        if (!p.isEmpty() && isSupportedFile(p)) {
            paths << p;
        }
    }
    
    if (paths.isEmpty()) {
        QToolTip::showText(QCursor::pos(), StringUtils::wrapToolTip("<b style='color:#e74c3c;'>✖ 选中项中没有支持的文件类型</b>"), this, {}, 2000);
        return;
    }

    QString rootPath = m_pathInput->text().trimmed();
    if (!QDir(rootPath).exists()) {
        rootPath = QFileInfo(paths.first()).absolutePath();
    }

    onMergeFiles(paths, rootPath);
}

void FileSearchWindow::onMergeFolderContent() {
    QListWidgetItem* item = m_sidebar->currentItem();
    if (!item) return;

    QString folderPath = item->data(Qt::UserRole).toString();
    if (!QDir(folderPath).exists()) return;

    QStringList filePaths;
    QDirIterator it(folderPath, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString fp = it.next();
        if (isSupportedFile(fp)) {
            filePaths << fp;
        }
    }

    if (filePaths.isEmpty()) {
        QToolTip::showText(QCursor::pos(), StringUtils::wrapToolTip("<b style='color:#e74c3c;'>✖ 文件夹中没有支持的文件类型</b>"), this, {}, 2000);
        return;
    }

    onMergeFiles(filePaths, folderPath);
}

void FileSearchWindow::resizeEvent(QResizeEvent* event) {
    FramelessDialog::resizeEvent(event);
    if (m_resizeHandle) {
        m_resizeHandle->move(width() - 20, height() - 20);
    }
}

// ----------------------------------------------------------------------------
// 历史记录与收藏夹 逻辑实现
// ----------------------------------------------------------------------------
void FileSearchWindow::addHistoryEntry(const QString& path) {
    if (path.isEmpty() || !QDir(path).exists()) return;
    QSettings settings("RapidNotes", "FileSearchHistory");
    QStringList history = settings.value("list").toStringList();
    history.removeAll(path);
    history.prepend(path);
    while (history.size() > 10) history.removeLast();
    settings.setValue("list", history);
}

QStringList FileSearchWindow::getHistory() const {
    QSettings settings("RapidNotes", "FileSearchHistory");
    return settings.value("list").toStringList();
}

void FileSearchWindow::clearHistory() {
    QSettings settings("RapidNotes", "FileSearchHistory");
    settings.setValue("list", QStringList());
}

void FileSearchWindow::removeHistoryEntry(const QString& path) {
    QSettings settings("RapidNotes", "FileSearchHistory");
    QStringList history = settings.value("list").toStringList();
    history.removeAll(path);
    settings.setValue("list", history);
}

void FileSearchWindow::addSearchHistoryEntry(const QString& text) {
    if (text.isEmpty()) return;
    QSettings settings("RapidNotes", "FileSearchFilenameHistory");
    QStringList history = settings.value("list").toStringList();
    history.removeAll(text);
    history.prepend(text);
    while (history.size() > 10) history.removeLast();
    settings.setValue("list", history);
}

QStringList FileSearchWindow::getSearchHistory() const {
    QSettings settings("RapidNotes", "FileSearchFilenameHistory");
    return settings.value("list").toStringList();
}

void FileSearchWindow::removeSearchHistoryEntry(const QString& text) {
    QSettings settings("RapidNotes", "FileSearchFilenameHistory");
    QStringList history = settings.value("list").toStringList();
    history.removeAll(text);
    settings.setValue("list", history);
}

void FileSearchWindow::clearSearchHistory() {
    QSettings settings("RapidNotes", "FileSearchFilenameHistory");
    settings.setValue("list", QStringList());
}

void FileSearchWindow::useHistoryPath(const QString& path) {
    m_pathInput->setText(path);
    startScan(path);
}

void FileSearchWindow::onSidebarItemClicked(QListWidgetItem* item) {
    if (!item) return;
    QString path = item->data(Qt::UserRole).toString();
    m_pathInput->setText(path);
    startScan(path);
}

void FileSearchWindow::showSidebarContextMenu(const QPoint& pos) {
    QListWidgetItem* item = m_sidebar->itemAt(pos);
    if (!item) return;

    QMenu menu(this);
    menu.setStyleSheet("QMenu { background-color: #252526; border: 1px solid #444; color: #EEE; } QMenu::item:selected { background-color: #37373D; }");
    
    QAction* pinAct = menu.addAction(IconHelper::getIcon("pin", "#F1C40F"), "置顶文件夹");
    menu.addAction(IconHelper::getIcon("merge", "#3498DB"), "合并文件夹内容", [this](){ onMergeFolderContent(); });
    QAction* removeAct = menu.addAction(IconHelper::getIcon("close", "#E74C3C"), "取消收藏");
    
    QAction* selected = menu.exec(m_sidebar->mapToGlobal(pos));
    if (selected == pinAct) {
        int row = m_sidebar->row(item);
        if (row > 0) {
            QListWidgetItem* taken = m_sidebar->takeItem(row);
            m_sidebar->insertItem(0, taken);
            m_sidebar->setCurrentItem(taken);
            saveFavorites();
        }
    } else if (selected == removeAct) {
        delete m_sidebar->takeItem(m_sidebar->row(item));
        saveFavorites();
    }
}

void FileSearchWindow::addFavorite(const QString& path) {
    // 检查是否已存在
    for (int i = 0; i < m_sidebar->count(); ++i) {
        if (m_sidebar->item(i)->data(Qt::UserRole).toString() == path) return;
    }

    QFileInfo fi(path);
    QString displayName = fi.fileName();
    // [CRITICAL] 修复根目录（如 C:/）显示为空的问题：如果 fileName 为空，则显示本地化的完整路径。
    if (displayName.isEmpty()) displayName = QDir::toNativeSeparators(fi.absoluteFilePath());
    
    auto* item = new QListWidgetItem(IconHelper::getIcon("folder", "#F1C40F"), displayName);
    item->setData(Qt::UserRole, path);
    item->setToolTip(StringUtils::wrapToolTip(path));
    m_sidebar->addItem(item);
    saveFavorites();
}

void FileSearchWindow::loadFavorites() {
    QSettings settings("RapidNotes", "FileSearchFavorites");
    QStringList favs = settings.value("list").toStringList();
    for (const QString& path : std::as_const(favs)) {
        if (QDir(path).exists()) {
            QFileInfo fi(path);
            QString displayName = fi.fileName();
            // [CRITICAL] 同步修复加载时根目录显示为空的问题
            if (displayName.isEmpty()) displayName = QDir::toNativeSeparators(fi.absoluteFilePath());

            auto* item = new QListWidgetItem(IconHelper::getIcon("folder", "#F1C40F"), displayName);
            item->setData(Qt::UserRole, path);
            item->setToolTip(StringUtils::wrapToolTip(path));
            m_sidebar->addItem(item);
        }
    }
}

void FileSearchWindow::saveFavorites() {
    QStringList favs;
    for (int i = 0; i < m_sidebar->count(); ++i) {
        favs << m_sidebar->item(i)->data(Qt::UserRole).toString();
    }
    QSettings settings("RapidNotes", "FileSearchFavorites");
    settings.setValue("list", favs);
}

bool FileSearchWindow::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::MouseButtonDblClick) {
        if (watched == m_pathInput) {
            auto* popup = new FileSearchHistoryPopup(this, m_pathInput, FileSearchHistoryPopup::Path);
            popup->showAnimated();
            return true;
        } else if (watched == m_searchInput) {
            auto* popup = new FileSearchHistoryPopup(this, m_searchInput, FileSearchHistoryPopup::Filename);
            popup->showAnimated();
            return true;
        }
    }
    return FramelessDialog::eventFilter(watched, event);
}

#include "FileSearchWindow.moc"
