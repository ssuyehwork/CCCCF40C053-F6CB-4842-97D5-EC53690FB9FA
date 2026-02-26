#include "ToolTipOverlay.h"
#include "FileSearchWidget.h"
#include "StringUtils.h"
#include "../core/ShortcutManager.h"

#include "IconHelper.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QDirIterator>
#include <QDesktopServices>
#include <algorithm>
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
#include <QCoreApplication>

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
// PathHistory 相关辅助类
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

    explicit FileSearchHistoryPopup(FileSearchWidget* widget, QLineEdit* edit, Type type)
        : QWidget(widget->window(), Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint)
    {
        m_widget = widget;
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
            if (m_type == Path) m_widget->clearHistory();
            else m_widget->clearSearchHistory();
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
        
        QStringList history = (m_type == Path) ? m_widget->getHistory() : m_widget->getSearchHistory();
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
                    if (m_type == Path) m_widget->useHistoryPath(v);
                    else m_edit->setText(v);
                    close(); 
                });
                connect(chip, &PathChip::deleted, this, [this](const QString& v){ 
                    if (m_type == Path) m_widget->removeHistoryEntry(v);
                    else m_widget->removeSearchHistoryEntry(v);
                    refreshUI(); 
                });
                m_vLayout->insertWidget(m_vLayout->count() - 1, chip);
            }
        }
        
        int targetWidth = m_edit->width();
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
    FileSearchWidget* m_widget;
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
    
    std::function<void(const QString&)> scanDir = [&](const QString& currentPath) {
        if (!m_isRunning) return;

        QDir dir(currentPath);
        QFileInfoList files = dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden);
        for (const auto& fi : std::as_const(files)) {
            if (!m_isRunning) return;
            bool hidden = fi.isHidden();
            if (!hidden && fi.fileName().startsWith('.')) hidden = true;
            
            emit fileFound(fi.fileName(), fi.absoluteFilePath(), hidden);
            count++;
        }

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
// FileCollectionListWidget 实现
// ----------------------------------------------------------------------------
FileCollectionListWidget::FileCollectionListWidget(QWidget* parent) : QListWidget(parent) {
    setAcceptDrops(true);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
}

void FileCollectionListWidget::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls() || event->mimeData()->hasText()) {
        event->acceptProposedAction();
    }
}

void FileCollectionListWidget::dragMoveEvent(QDragMoveEvent* event) {
    event->acceptProposedAction();
}

void FileCollectionListWidget::dropEvent(QDropEvent* event) {
    QStringList paths;
    if (event->mimeData()->hasUrls()) {
        for (const QUrl& url : event->mimeData()->urls()) {
            QString p = url.toLocalFile();
            if (!p.isEmpty() && QFileInfo(p).isFile()) paths << p;
        }
    } else if (event->mimeData()->hasText()) {
        QStringList texts = event->mimeData()->text().split("\n", Qt::SkipEmptyParts);
        for (const QString& t : texts) {
            QString p = t.trimmed();
            if (!p.isEmpty() && QFileInfo(p).isFile()) paths << p;
        }
    }

    if (!paths.isEmpty()) {
        emit filesDropped(paths);
        event->acceptProposedAction();
    } else if (event->source() && event->source() != this) {
        QListWidget* sourceList = qobject_cast<QListWidget*>(event->source());
        if (sourceList) {
            QStringList sourcePaths;
            for (auto* item : sourceList->selectedItems()) {
                QString p = item->data(Qt::UserRole).toString();
                if (!p.isEmpty()) sourcePaths << p;
            }
            if (!sourcePaths.isEmpty()) {
                emit filesDropped(sourcePaths);
                event->acceptProposedAction();
            }
        }
    }
}

// ----------------------------------------------------------------------------
// FileSearchWidget 实现
// ----------------------------------------------------------------------------
FileSearchWidget::FileSearchWidget(QWidget* parent) : QWidget(parent) {
    setupStyles();
    initUI();
    loadFavorites();
    loadCollection();
}

FileSearchWidget::~FileSearchWidget() {
    if (m_scanThread) {
        m_scanThread->stop();
        m_scanThread->deleteLater();
    }
}

void FileSearchWidget::setupStyles() {
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

void FileSearchWidget::initUI() {
    auto* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(0);

    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->setHandleWidth(1);
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
    connect(sidebar, &FileSidebarListWidget::folderDropped, this, &FileSearchWidget::addFavorite);
    connect(m_sidebar, &QListWidget::itemClicked, this, &FileSearchWidget::onSidebarItemClicked);
    connect(m_sidebar, &QListWidget::customContextMenuRequested, this, &FileSearchWidget::showSidebarContextMenu);
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

    // --- 中间主区域 ---
    auto* centerWidget = new QWidget();
    auto* layout = new QVBoxLayout(centerWidget);
    layout->setContentsMargins(5, 0, 5, 0);
    layout->setSpacing(10);

    auto* pathLayout = new QHBoxLayout();
    m_pathInput = new QLineEdit();
    m_pathInput->setPlaceholderText("双击查看历史，或在此粘贴路径...");
    m_pathInput->setClearButtonEnabled(true);
    m_pathInput->installEventFilter(this);
    connect(m_pathInput, &QLineEdit::returnPressed, this, &FileSearchWidget::onPathReturnPressed);
    
    auto* btnScan = new QToolButton();
    btnScan->setIcon(IconHelper::getIcon("scan", "#1abc9c", 18));
    btnScan->setToolTip("开始扫描");
    btnScan->setFixedSize(38, 38);
    btnScan->setCursor(Qt::PointingHandCursor);
    btnScan->setStyleSheet("QToolButton { border: 1px solid #444; background: #2D2D30; border-radius: 6px; }"
                           "QToolButton:hover { background-color: #3E3E42; border-color: #007ACC; }");
    connect(btnScan, &QToolButton::clicked, this, &FileSearchWidget::onPathReturnPressed);

    auto* btnBrowse = new QToolButton();
    btnBrowse->setObjectName("ActionBtn");
    btnBrowse->setIcon(IconHelper::getIcon("folder", "#ffffff", 18));
    btnBrowse->setToolTip("浏览文件夹");
    btnBrowse->setFixedSize(38, 38);
    btnBrowse->setCursor(Qt::PointingHandCursor);
    connect(btnBrowse, &QToolButton::clicked, this, &FileSearchWidget::selectFolder);

    pathLayout->addWidget(m_pathInput);
    pathLayout->addWidget(btnScan);
    pathLayout->addWidget(btnBrowse);
    layout->addLayout(pathLayout);

    auto* searchLayout = new QHBoxLayout();
    m_searchInput = new QLineEdit();
    m_searchInput->setPlaceholderText("输入文件名过滤...");
    m_searchInput->setClearButtonEnabled(true);
    m_searchInput->installEventFilter(this);
    connect(m_searchInput, &QLineEdit::textChanged, this, &FileSearchWidget::refreshList);
    connect(m_searchInput, &QLineEdit::returnPressed, this, [this](){
        addSearchHistoryEntry(m_searchInput->text().trimmed());
    });

    m_extInput = new QLineEdit();
    m_extInput->setPlaceholderText("后缀 (如 py)");
    m_extInput->setClearButtonEnabled(true);
    m_extInput->setFixedWidth(120);
    connect(m_extInput, &QLineEdit::textChanged, this, &FileSearchWidget::refreshList);

    searchLayout->addWidget(m_searchInput);
    searchLayout->addWidget(m_extInput);
    layout->addLayout(searchLayout);

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
    connect(m_showHiddenCheck, &QCheckBox::toggled, this, &FileSearchWidget::refreshList);

    infoLayout->addWidget(m_infoLabel);
    infoLayout->addWidget(m_showHiddenCheck);
    infoLayout->addStretch();
    layout->addLayout(infoLayout);

    auto* listHeaderLayout = new QHBoxLayout();
    listHeaderLayout->setContentsMargins(0, 0, 0, 0);
    auto* listTitle = new QLabel("搜索结果");
    listTitle->setStyleSheet("color: #888; font-size: 11px; font-weight: bold; border: none; background: transparent;");
    
    auto* btnCopyAll = new QToolButton();
    btnCopyAll->setIcon(IconHelper::getIcon("copy", "#1abc9c", 14));
    btnCopyAll->setToolTip("复制全部搜索结果的路径");
    btnCopyAll->setFixedSize(20, 20);
    btnCopyAll->setCursor(Qt::PointingHandCursor);
    btnCopyAll->setStyleSheet("QToolButton { border: none; background: transparent; padding: 2px; }"
                               "QToolButton:hover { background-color: #3E3E42; border-radius: 4px; }");
    connect(btnCopyAll, &QToolButton::clicked, this, [this](){
        if (m_fileList->count() == 0) {
            ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#e74c3c;'>✖ 结果列表为空</b>");
            return;
        }
        QStringList paths;
        for (int i = 0; i < m_fileList->count(); ++i) {
            QString p = m_fileList->item(i)->data(Qt::UserRole).toString();
            if (!p.isEmpty()) paths << p;
        }
        if (paths.isEmpty()) return;
        QApplication::clipboard()->setText(paths.join("\n"));
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#2ecc71;'>✔ 已复制全部搜索结果</b>");
    });

    listHeaderLayout->addWidget(listTitle);
    listHeaderLayout->addStretch();
    listHeaderLayout->addWidget(btnCopyAll);
    layout->addLayout(listHeaderLayout);

    m_fileList = new QListWidget();
    m_fileList->setObjectName("FileList");
    m_fileList->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_fileList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_fileList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_fileList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_fileList->setDragEnabled(true);
    m_fileList->setDragDropMode(QAbstractItemView::DragOnly);
    connect(m_fileList, &QListWidget::customContextMenuRequested, this, &FileSearchWidget::showFileContextMenu);
    
    m_actionSelectAll = new QAction(this);
    m_actionSelectAll->setShortcutContext(Qt::WidgetShortcut);
    connect(m_actionSelectAll, &QAction::triggered, [this](){ m_fileList->selectAll(); });
    m_fileList->addAction(m_actionSelectAll);

    m_actionCopy = new QAction(this);
    m_actionCopy->setShortcutContext(Qt::WidgetShortcut);
    connect(m_actionCopy, &QAction::triggered, this, [this](){ copySelectedFiles(); });
    m_fileList->addAction(m_actionCopy);

    m_actionDelete = new QAction(this);
    m_actionDelete->setShortcutContext(Qt::WidgetShortcut);
    connect(m_actionDelete, &QAction::triggered, this, [this](){ onDeleteFile(); });
    m_fileList->addAction(m_actionDelete);

    m_actionScan = new QAction(this);
    connect(m_actionScan, &QAction::triggered, this, &FileSearchWidget::onPathReturnPressed);
    addAction(m_actionScan);

    updateShortcuts();
    connect(&ShortcutManager::instance(), &ShortcutManager::shortcutsChanged, this, &FileSearchWidget::updateShortcuts);

    layout->addWidget(m_fileList);
    splitter->addWidget(centerWidget);

    // --- 右侧边栏 (文件收藏) ---
    auto* collectionWidget = new QWidget();
    auto* collectionLayout = new QVBoxLayout(collectionWidget);
    collectionLayout->setContentsMargins(5, 0, 0, 0);
    collectionLayout->setSpacing(10);

    auto* collHeaderLayout = new QHBoxLayout();
    collHeaderLayout->setSpacing(5);
    auto* collIcon = new QLabel();
    collIcon->setPixmap(IconHelper::getIcon("file", "#888").pixmap(14, 14));
    collIcon->setStyleSheet("border: none; background: transparent;");
    collHeaderLayout->addWidget(collIcon);

    auto* collHeader = new QLabel("文件收藏 (可多选/拖入)");
    collHeader->setStyleSheet("color: #888; font-weight: bold; font-size: 12px; border: none; background: transparent;");
    collHeaderLayout->addWidget(collHeader);
    collHeaderLayout->addStretch();
    collectionLayout->addLayout(collHeaderLayout);

    m_collectionSidebar = new FileCollectionListWidget();
    m_collectionSidebar->setObjectName("SidebarList");
    m_collectionSidebar->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_collectionSidebar->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_collectionSidebar->setMinimumWidth(200);
    m_collectionSidebar->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_collectionSidebar, &FileCollectionListWidget::filesDropped, this, [this](const QStringList& paths){
        for(const QString& p : paths) addCollectionItem(p);
    });
    connect(m_collectionSidebar, &QListWidget::itemClicked, this, &FileSearchWidget::onCollectionItemClicked);
    connect(m_collectionSidebar, &QListWidget::customContextMenuRequested, this, &FileSearchWidget::showCollectionContextMenu);
    collectionLayout->addWidget(m_collectionSidebar);

    auto* btnMergeColl = new QPushButton("合并收藏内容");
    btnMergeColl->setFixedHeight(32);
    btnMergeColl->setCursor(Qt::PointingHandCursor);
    btnMergeColl->setStyleSheet(
        "QPushButton { background-color: #2D2D30; border: 1px solid #444; color: #AAA; border-radius: 4px; font-size: 12px; }"
        "QPushButton:hover { background-color: #3E3E42; color: #FFF; border-color: #666; }"
    );
    connect(btnMergeColl, &QPushButton::clicked, this, &FileSearchWidget::onMergeCollectionFiles);
    collectionLayout->addWidget(btnMergeColl);

    splitter->addWidget(collectionWidget);

    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 0);
}

void FileSearchWidget::selectFolder() {
    QString d = QFileDialog::getExistingDirectory(this, "选择文件夹");
    if (!d.isEmpty()) {
        m_pathInput->setText(d);
        startScan(d);
    }
}

void FileSearchWidget::onPathReturnPressed() {
    QString p = m_pathInput->text().trimmed();
    if (QDir(p).exists()) {
        startScan(p);
    } else {
        m_infoLabel->setText("路径不存在");
        m_pathInput->setStyleSheet("border: 1px solid #FF3333;");
    }
}

void FileSearchWidget::startScan(const QString& path) {
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
    connect(m_scanThread, &ScannerThread::fileFound, this, &FileSearchWidget::onFileFound);
    connect(m_scanThread, &ScannerThread::finished, this, &FileSearchWidget::onScanFinished);
    m_scanThread->start();
}

void FileSearchWidget::onFileFound(const QString& name, const QString& path, bool isHidden) {
    m_filesData.append({name, path, isHidden});
    if (isHidden) m_hiddenCount++;
    else m_visibleCount++;

    if (m_filesData.size() % 300 == 0) {
        m_infoLabel->setText(QString("已发现 %1 个文件 (可见:%2 隐性:%3)...").arg(m_filesData.size()).arg(m_visibleCount).arg(m_hiddenCount));
    }
}

void FileSearchWidget::onScanFinished(int count) {
    m_infoLabel->setText(QString("扫描结束，共 %1 个文件 (可见:%2 隐性:%3)").arg(count).arg(m_visibleCount).arg(m_hiddenCount));
    addHistoryEntry(m_pathInput->text().trimmed());
    
    std::sort(m_filesData.begin(), m_filesData.end(), [](const FileData& a, const FileData& b){
        return a.name.localeAwareCompare(b.name) < 0;
    });

    refreshList();
}

void FileSearchWidget::refreshList() {
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
        item->setToolTip(data.path);
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

void FileSearchWidget::showFileContextMenu(const QPoint& pos) {
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
    IconHelper::setupMenu(&menu);
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

    if (selectedItems.size() == 1) {
        QString fileName = QFileInfo(paths.first()).fileName();
        menu.addAction(IconHelper::getIcon("copy", "#F39C12"), "复制文件名", [fileName](){
            QApplication::clipboard()->setText(fileName);
            ToolTipOverlay::instance()->showText(QCursor::pos(), "✔ 已复制文件名");
        });
    }

    QString copyFileText = selectedItems.size() > 1 ? "复制选中文件" : "复制文件";
    menu.addAction(IconHelper::getIcon("file", "#4A90E2"), copyFileText, [this](){ copySelectedFiles(); });

    menu.addAction(IconHelper::getIcon("merge", "#3498DB"), "合并选中内容", [this](){ onMergeSelectedFiles(); });

    menu.addSeparator();

    menu.addAction(IconHelper::getIcon("star", "#F1C40F"), "加入收藏", [this](){
        auto selectedItems = m_fileList->selectedItems();
        for (auto* item : selectedItems) {
            QString p = item->data(Qt::UserRole).toString();
            if (!p.isEmpty()) addCollectionItem(p);
        }
    });

    menu.addSeparator();
    menu.addAction(IconHelper::getIcon("cut", "#E67E22"), "剪切", [this](){ onCutFile(); });
    menu.addAction(IconHelper::getIcon("trash", "#E74C3C"), "删除", [this](){ onDeleteFile(); });

    menu.exec(m_fileList->mapToGlobal(pos));
}

void FileSearchWidget::onEditFile() {
    auto selectedItems = m_fileList->selectedItems();
    if (selectedItems.isEmpty()) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#e74c3c;'>✖ 请先选择要操作的内容</b>");
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

    if (editorPath.isEmpty() || !QFile::exists(editorPath)) {
        editorPath = QFileDialog::getOpenFileName(this, "选择编辑器 (推荐 Notepad++)", "C:/Program Files", "Executable (*.exe)");
        if (editorPath.isEmpty()) return;
        settings.setValue("EditorPath", editorPath);
    }

    for (const QString& filePath : paths) {
        QProcess::startDetached(editorPath, { QDir::toNativeSeparators(filePath) });
    }
}

void FileSearchWidget::copySelectedFiles() {
    auto selectedItems = m_fileList->selectedItems();
    if (selectedItems.isEmpty()) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#e74c3c;'>✖ 请先选择要操作的内容</b>");
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
    ToolTipOverlay::instance()->showText(QCursor::pos(), msg);
}

void FileSearchWidget::onCutFile() {
    auto selectedItems = m_fileList->selectedItems();
    if (selectedItems.isEmpty()) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#e74c3c;'>✖ 请先选择要操作的内容</b>");
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
    ToolTipOverlay::instance()->showText(QCursor::pos(), msg);
}

void FileSearchWidget::onDeleteFile() {
    auto selectedItems = m_fileList->selectedItems();
    if (selectedItems.isEmpty()) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#e74c3c;'>✖ 请先选择要操作的内容</b>");
        return;
    }

    int successCount = 0;
    for (auto* item : std::as_const(selectedItems)) {
        QString filePath = item->data(Qt::UserRole).toString();
        if (filePath.isEmpty()) continue;

        if (QFile::moveToTrash(filePath)) {
            successCount++;
            for (int i = 0; i < m_filesData.size(); ++i) {
                if (m_filesData[i].path == filePath) {
                    m_filesData.removeAt(i);
                    break;
                }
            }
            delete item;
        }
    }

    if (successCount > 0) {
        QString msg = selectedItems.size() > 1 ? QString("✔ %1 个文件已移至回收站").arg(successCount) : "✔ 文件已移至回收站";
        ToolTipOverlay::instance()->showText(QCursor::pos(), msg);
        m_infoLabel->setText(msg);
    } else if (!selectedItems.isEmpty()) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #e74c3c;'>✖ 无法删除文件，请检查是否被占用</b>");
    }
}

void FileSearchWidget::onMergeFiles(const QStringList& filePaths, const QString& rootPath, bool useCombineDir) {
    if (filePaths.isEmpty()) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#e74c3c;'>✖ 没有可合并的文件</b>");
        return;
    }

    QString targetDir = rootPath;
    if (useCombineDir) {
        targetDir = QCoreApplication::applicationDirPath() + "/Combine";
        QDir dir(targetDir);
        if (!dir.exists()) dir.mkpath(".");
    }

    QString ts = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString outName = QString("%1_code_export.md").arg(ts);
    QString outPath = QDir(targetDir).filePath(outName);

    QFile outFile(outPath);
    if (!outFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#e74c3c;'>✖ 无法创建输出文件</b>");
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
    ToolTipOverlay::instance()->showText(QCursor::pos(), msg);
}

void FileSearchWidget::onMergeSelectedFiles() {
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
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#e74c3c;'>✖ 选中项中没有支持的文件类型</b>");
        return;
    }

    QString rootPath = m_pathInput->text().trimmed();
    if (!QDir(rootPath).exists()) {
        rootPath = QFileInfo(paths.first()).absolutePath();
    }

    onMergeFiles(paths, rootPath);
}

void FileSearchWidget::onMergeFolderContent() {
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
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#e74c3c;'>✖ 文件夹中没有支持的文件类型</b>");
        return;
    }

    onMergeFiles(filePaths, folderPath);
}

void FileSearchWidget::updateShortcuts() {
    auto& sm = ShortcutManager::instance();
    if (m_actionSelectAll) m_actionSelectAll->setShortcut(sm.getShortcut("fs_select_all"));
    if (m_actionCopy) m_actionCopy->setShortcut(sm.getShortcut("fs_copy"));
    if (m_actionDelete) m_actionDelete->setShortcut(sm.getShortcut("fs_delete"));
    if (m_actionScan) m_actionScan->setShortcut(sm.getShortcut("fs_scan"));
}

void FileSearchWidget::addHistoryEntry(const QString& path) {
    if (path.isEmpty() || !QDir(path).exists()) return;
    QSettings settings("RapidNotes", "FileSearchHistory");
    QStringList history = settings.value("list").toStringList();
    history.removeAll(path);
    history.prepend(path);
    while (history.size() > 10) history.removeLast();
    settings.setValue("list", history);
}

QStringList FileSearchWidget::getHistory() const {
    QSettings settings("RapidNotes", "FileSearchHistory");
    return settings.value("list").toStringList();
}

void FileSearchWidget::clearHistory() {
    QSettings settings("RapidNotes", "FileSearchHistory");
    settings.setValue("list", QStringList());
}

void FileSearchWidget::removeHistoryEntry(const QString& path) {
    QSettings settings("RapidNotes", "FileSearchHistory");
    QStringList history = settings.value("list").toStringList();
    history.removeAll(path);
    settings.setValue("list", history);
}

void FileSearchWidget::addSearchHistoryEntry(const QString& text) {
    if (text.isEmpty()) return;
    QSettings settings("RapidNotes", "FileSearchFilenameHistory");
    QStringList history = settings.value("list").toStringList();
    history.removeAll(text);
    history.prepend(text);
    while (history.size() > 10) history.removeLast();
    settings.setValue("list", history);
}

QStringList FileSearchWidget::getSearchHistory() const {
    QSettings settings("RapidNotes", "FileSearchFilenameHistory");
    return settings.value("list").toStringList();
}

void FileSearchWidget::removeSearchHistoryEntry(const QString& text) {
    QSettings settings("RapidNotes", "FileSearchFilenameHistory");
    QStringList history = settings.value("list").toStringList();
    history.removeAll(text);
    settings.setValue("list", history);
}

void FileSearchWidget::clearSearchHistory() {
    QSettings settings("RapidNotes", "FileSearchFilenameHistory");
    settings.setValue("list", QStringList());
}

void FileSearchWidget::useHistoryPath(const QString& path) {
    m_pathInput->setText(path);
    startScan(path);
}

void FileSearchWidget::onSidebarItemClicked(QListWidgetItem* item) {
    if (!item) return;
    QString path = item->data(Qt::UserRole).toString();
    m_pathInput->setText(path);
    startScan(path);
}

void FileSearchWidget::showSidebarContextMenu(const QPoint& pos) {
    QListWidgetItem* item = m_sidebar->itemAt(pos);
    if (!item) return;

    QMenu menu(this);
    IconHelper::setupMenu(&menu);
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

void FileSearchWidget::addFavorite(const QString& path) {
    for (int i = 0; i < m_sidebar->count(); ++i) {
        if (m_sidebar->item(i)->data(Qt::UserRole).toString() == path) return;
    }
    QFileInfo fi(path);
    QString displayName = fi.fileName();
    if (displayName.isEmpty()) displayName = QDir::toNativeSeparators(fi.absoluteFilePath());
    auto* item = new QListWidgetItem(IconHelper::getIcon("folder", "#F1C40F"), displayName);
    item->setData(Qt::UserRole, path);
    item->setToolTip(path);
    m_sidebar->addItem(item);
    saveFavorites();
}

void FileSearchWidget::loadFavorites() {
    QSettings settings("RapidNotes", "FileSearchFavorites");
    QStringList favs = settings.value("list").toStringList();
    for (const QString& path : std::as_const(favs)) {
        if (QDir(path).exists()) {
            QFileInfo fi(path);
            QString displayName = fi.fileName();
            if (displayName.isEmpty()) displayName = QDir::toNativeSeparators(fi.absoluteFilePath());
            auto* item = new QListWidgetItem(IconHelper::getIcon("folder", "#F1C40F"), displayName);
            item->setData(Qt::UserRole, path);
            item->setToolTip(path);
            m_sidebar->addItem(item);
        }
    }
}

void FileSearchWidget::saveFavorites() {
    QStringList favs;
    for (int i = 0; i < m_sidebar->count(); ++i) {
        favs << m_sidebar->item(i)->data(Qt::UserRole).toString();
    }
    QSettings settings("RapidNotes", "FileSearchFavorites");
    settings.setValue("list", favs);
}

bool FileSearchWidget::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::MouseButtonDblClick) {
        if (watched == m_pathInput) {
            if (!m_pathPopup) m_pathPopup = new FileSearchHistoryPopup(this, m_pathInput, FileSearchHistoryPopup::Path);
            m_pathPopup->showAnimated();
            return true;
        } else if (watched == m_searchInput) {
            if (!m_searchPopup) m_searchPopup = new FileSearchHistoryPopup(this, m_searchInput, FileSearchHistoryPopup::Filename);
            m_searchPopup->showAnimated();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void FileSearchWidget::onCollectionItemClicked(QListWidgetItem* item) {
}

void FileSearchWidget::showCollectionContextMenu(const QPoint& pos) {
    auto selectedItems = m_collectionSidebar->selectedItems();
    if (selectedItems.isEmpty()) return;

    QMenu menu(this);
    IconHelper::setupMenu(&menu);
    menu.setStyleSheet("QMenu { background-color: #252526; border: 1px solid #444; color: #EEE; } QMenu::item:selected { background-color: #37373D; }");
    
    menu.addAction(IconHelper::getIcon("merge", "#3498DB"), "合并选中内容", [this](){
        QStringList paths;
        for (auto* item : m_collectionSidebar->selectedItems()) {
            paths << item->data(Qt::UserRole).toString();
        }
        onMergeFiles(paths, "", true);
    });

    menu.addAction(IconHelper::getIcon("close", "#E74C3C"), "取消收藏", [this](){
        for (auto* item : m_collectionSidebar->selectedItems()) {
            delete item;
        }
        saveCollection();
    });
    
    menu.exec(m_collectionSidebar->mapToGlobal(pos));
}

void FileSearchWidget::addCollectionItem(const QString& path) {
    for (int i = 0; i < m_collectionSidebar->count(); ++i) {
        if (m_collectionSidebar->item(i)->data(Qt::UserRole).toString() == path) return;
    }
    QFileInfo fi(path);
    auto* item = new QListWidgetItem(IconHelper::getIcon("file", "#2ECC71"), fi.fileName());
    item->setData(Qt::UserRole, path);
    item->setToolTip(path);
    m_collectionSidebar->addItem(item);
    saveCollection();
}

void FileSearchWidget::loadCollection() {
    QSettings settings("RapidNotes", "FileSearchCollection");
    QStringList coll = settings.value("list").toStringList();
    for (const QString& path : std::as_const(coll)) {
        if (QFile::exists(path)) {
            QFileInfo fi(path);
            auto* item = new QListWidgetItem(IconHelper::getIcon("file", "#2ECC71"), fi.fileName());
            item->setData(Qt::UserRole, path);
            item->setToolTip(path);
            m_collectionSidebar->addItem(item);
        }
    }
}

void FileSearchWidget::saveCollection() {
    QStringList coll;
    for (int i = 0; i < m_collectionSidebar->count(); ++i) {
        coll << m_collectionSidebar->item(i)->data(Qt::UserRole).toString();
    }
    QSettings settings("RapidNotes", "FileSearchCollection");
    settings.setValue("list", coll);
}

void FileSearchWidget::onMergeCollectionFiles() {
    QStringList paths;
    for (int i = 0; i < m_collectionSidebar->count(); ++i) {
        paths << m_collectionSidebar->item(i)->data(Qt::UserRole).toString();
    }
    if (paths.isEmpty()) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#e74c3c;'>✖ 收藏夹为空</b>");
        return;
    }
    onMergeFiles(paths, "", true);
}

#include "FileSearchWidget.moc"
