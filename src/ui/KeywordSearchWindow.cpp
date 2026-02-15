#include "ToolTipOverlay.h"
#include "KeywordSearchWindow.h"
#include "IconHelper.h"
#include "StringUtils.h"
#include "../core/ShortcutManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFileDialog>
#include <QDirIterator>
#include <utility>
#include <QTextStream>
#include <QRegularExpression>
#include <QDateTime>
#include <QProcess>
#include <QDesktopServices>
#include <QUrl>
#include <QtConcurrent>
#include <QScrollBar>
#include <QToolTip>
#include <QSettings>
#include <QMenu>
#include <QSet>
#include <QMap>
#include <QFileInfo>
#include <QGraphicsDropShadowEffect>
#include <QPropertyAnimation>
#include <QScrollArea>
#include <QCheckBox>
#include <QProgressBar>
#include <QTextBrowser>
#include <QToolButton>
#include <QApplication>
#include <QClipboard>

#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>

// ----------------------------------------------------------------------------
// 合并逻辑相关常量与辅助函数 (复刻自 FileSearchWindow)
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
// Sidebar ListWidget subclass for Drag & Drop
// ----------------------------------------------------------------------------
class KeywordSidebarListWidget : public QListWidget {
    Q_OBJECT
public:
    explicit KeywordSidebarListWidget(QWidget* parent = nullptr) : QListWidget(parent) {
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

// ----------------------------------------------------------------------------
// KeywordSearchHistory 相关辅助类 (复刻 FileSearchHistoryPopup 逻辑)
// ----------------------------------------------------------------------------
class KeywordChip : public QFrame {
    Q_OBJECT
public:
    KeywordChip(const QString& text, QWidget* parent = nullptr) : QFrame(parent), m_text(text) {
        setAttribute(Qt::WA_StyledBackground);
        setCursor(Qt::PointingHandCursor);
        setObjectName("KeywordChip");
        
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
            "#KeywordChip { background-color: transparent; border: none; border-radius: 4px; }"
            "#KeywordChip:hover { background-color: #3E3E42; }"
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

class KeywordSearchHistoryPopup : public QWidget {
    Q_OBJECT
public:
    enum Type { Path, Keyword, Replace };

    explicit KeywordSearchHistoryPopup(KeywordSearchWidget* widget, QLineEdit* edit, Type type) 
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

        auto* shadow = new QGraphicsDropShadowEffect(container);
        shadow->setBlurRadius(20); shadow->setXOffset(0); shadow->setYOffset(5);
        shadow->setColor(QColor(0, 0, 0, 120));
        container->setGraphicsEffect(shadow);

        auto* layout = new QVBoxLayout(container);
        layout->setContentsMargins(12, 12, 12, 12);
        layout->setSpacing(10);

        auto* top = new QHBoxLayout();
        auto* icon = new QLabel();
        icon->setPixmap(IconHelper::getIcon("clock", "#888").pixmap(14, 14));
        icon->setStyleSheet("border: none; background: transparent;");
        top->addWidget(icon);

        QString titleStr = "最近记录";
        if (m_type == Path) titleStr = "最近扫描路径";
        else if (m_type == Keyword) titleStr = "最近查找内容";
        else if (m_type == Replace) titleStr = "最近替换内容";

        auto* title = new QLabel(titleStr);
        title->setStyleSheet("color: #888; font-weight: bold; font-size: 11px; background: transparent; border: none;");
        top->addWidget(title);
        top->addStretch();
        auto* clearBtn = new QPushButton("清空");
        clearBtn->setCursor(Qt::PointingHandCursor);
        clearBtn->setStyleSheet("QPushButton { background: transparent; color: #666; border: none; font-size: 11px; } QPushButton:hover { color: #E74C3C; }");
        connect(clearBtn, &QPushButton::clicked, [this](){
            clearAllHistory();
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

    void clearAllHistory() {
        QString key = "keywordList";
        if (m_type == Path) key = "pathList";
        else if (m_type == Replace) key = "replaceList";

        QSettings settings("RapidNotes", "KeywordSearchHistory");
        settings.setValue(key, QStringList());
    }

    void removeEntry(const QString& text) {
        QString key = "keywordList";
        if (m_type == Path) key = "pathList";
        else if (m_type == Replace) key = "replaceList";

        QSettings settings("RapidNotes", "KeywordSearchHistory");
        QStringList history = settings.value(key).toStringList();
        history.removeAll(text);
        settings.setValue(key, history);
    }

    QStringList getHistory() const {
        QString key = "keywordList";
        if (m_type == Path) key = "pathList";
        else if (m_type == Replace) key = "replaceList";

        QSettings settings("RapidNotes", "KeywordSearchHistory");
        return settings.value(key).toStringList();
    }

    void refreshUI() {
        QLayoutItem* item;
        while ((item = m_vLayout->takeAt(0))) {
            if(item->widget()) item->widget()->deleteLater();
            delete item;
        }
        m_vLayout->addStretch();
        
        QStringList history = getHistory();
        if(history.isEmpty()) {
            auto* lbl = new QLabel("暂无历史记录");
            lbl->setAlignment(Qt::AlignCenter);
            lbl->setStyleSheet("color: #555; font-style: italic; margin: 20px; border: none;");
            m_vLayout->insertWidget(0, lbl);
        } else {
            for(const QString& val : history) {
                auto* chip = new KeywordChip(val);
                chip->setFixedHeight(32);
                connect(chip, &KeywordChip::clicked, this, [this](const QString& v){ 
                    m_edit->setText(v);
                    close(); 
                });
                connect(chip, &KeywordChip::deleted, this, [this](const QString& v){ 
                    removeEntry(v);
                    refreshUI(); 
                });
                m_vLayout->insertWidget(m_vLayout->count() - 1, chip);
            }
        }
        
        int targetWidth = m_edit->width();
        int contentHeight = qMin(410, (int)history.size() * 34 + 60);
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
    KeywordSearchWidget* m_widget;
    QLineEdit* m_edit;
    Type m_type;
    QWidget* m_chipsWidget;
    QVBoxLayout* m_vLayout;
    QPropertyAnimation* m_opacityAnim;
};

// ----------------------------------------------------------------------------
// KeywordCollectionListWidget 实现 (复刻自 FileSearchWindow)
// ----------------------------------------------------------------------------
KeywordCollectionListWidget::KeywordCollectionListWidget(QWidget* parent) : QListWidget(parent) {
    setAcceptDrops(true);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
}

void KeywordCollectionListWidget::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls() || event->mimeData()->hasText()) {
        event->acceptProposedAction();
    }
}

void KeywordCollectionListWidget::dragMoveEvent(QDragMoveEvent* event) {
    event->acceptProposedAction();
}

void KeywordCollectionListWidget::dropEvent(QDropEvent* event) {
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
// KeywordResultItem: 用于在列表中左右对齐显示结果
// ----------------------------------------------------------------------------
class KeywordResultItem : public QWidget {
    Q_OBJECT
public:
    KeywordResultItem(const QString& name, const QString& badge, const QColor& badgeColor, QWidget* parent = nullptr)
        : QWidget(parent)
    {
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(10, 0, 10, 0);
        layout->setSpacing(10);

        auto* nameLabel = new QLabel(name);
        nameLabel->setStyleSheet("color: #CCCCCC; font-size: 13px; border: none; background: transparent;");
        layout->addWidget(nameLabel);

        layout->addStretch();

        auto* badgeLabel = new QLabel(badge);
        badgeLabel->setStyleSheet(QString("color: %1; font-size: 12px; font-weight: bold; border: none; background: transparent;").arg(badgeColor.name()));

        // 为右侧数字或状态添加悬浮提示
        if (badge == "已修改") {
            badgeLabel->setToolTip("文件已修改");
        } else if (badge == "已恢复") {
            badgeLabel->setToolTip("文件已恢复");
        } else {
            badgeLabel->setToolTip("匹配次数");
        }

        layout->addWidget(badgeLabel);
    }
protected:
    // 忽略鼠标事件以便让底层的 QListWidget 处理选中和点击
    void mousePressEvent(QMouseEvent* event) override { event->ignore(); }
    void mouseReleaseEvent(QMouseEvent* event) override { event->ignore(); }
    void mouseDoubleClickEvent(QMouseEvent* event) override { event->ignore(); }
};

// ----------------------------------------------------------------------------
// KeywordSearchWidget 实现
// ----------------------------------------------------------------------------
KeywordSearchWidget::KeywordSearchWidget(QWidget* parent) : QWidget(parent) {
    m_ignoreDirs = {".git", ".svn", ".idea", ".vscode", "__pycache__", "node_modules", "dist", "build", "venv"};
    setupStyles();
    initUI();
    loadFavorites();
    loadCollection();
}

KeywordSearchWidget::~KeywordSearchWidget() {
}

void KeywordSearchWidget::setupStyles() {
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
            min-height: 20px;
            max-height: 20px;
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

void KeywordSearchWidget::initUI() {
    auto* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(0);

    auto* splitter = new QSplitter(Qt::Horizontal);
    mainLayout->addWidget(splitter);

    // --- 左侧边栏 ---
    auto* sidebarWidget = new QWidget();
    auto* sidebarLayout = new QVBoxLayout(sidebarWidget);
    sidebarLayout->setContentsMargins(0, 0, 10, 0);
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

    m_sidebar = new KeywordSidebarListWidget();
    m_sidebar->setObjectName("SidebarList");
    m_sidebar->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_sidebar->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_sidebar->setMinimumWidth(200);
    m_sidebar->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(static_cast<KeywordSidebarListWidget*>(m_sidebar), &KeywordSidebarListWidget::folderDropped, this, &KeywordSearchWidget::addFavorite);
    connect(m_sidebar, &QListWidget::itemClicked, this, &KeywordSearchWidget::onSidebarItemClicked);
    connect(m_sidebar, &QListWidget::customContextMenuRequested, this, &KeywordSearchWidget::showSidebarContextMenu);
    sidebarLayout->addWidget(m_sidebar);

    auto* btnAddFav = new QPushButton("收藏当前路径");
    btnAddFav->setFixedHeight(32);
    btnAddFav->setCursor(Qt::PointingHandCursor);
    btnAddFav->setStyleSheet(
        "QPushButton { background-color: #2D2D30; border: 1px solid #444; color: #AAA; border-radius: 4px; font-size: 12px; }"
        "QPushButton:hover { background-color: #3E3E42; color: #FFF; border-color: #666; }"
    );
    connect(btnAddFav, &QPushButton::clicked, this, [this](){
        QString p = m_pathEdit->text().trimmed();
        if (QDir(p).exists()) addFavorite(p);
    });
    sidebarLayout->addWidget(btnAddFav);

    splitter->addWidget(sidebarWidget);

    // --- 中间内容区域 ---
    auto* midWidget = new QWidget();
    auto* midLayout = new QVBoxLayout(midWidget);
    midLayout->setContentsMargins(10, 0, 10, 0);
    midLayout->setSpacing(15);

    // --- 配置区域 ---
    auto* configGroup = new QWidget();
    auto* configLayout = new QGridLayout(configGroup);
    configLayout->setContentsMargins(0, 0, 0, 0);
    configLayout->setHorizontalSpacing(10); 
    configLayout->setVerticalSpacing(10);
    configLayout->setColumnStretch(1, 1);
    configLayout->setColumnStretch(0, 0);
    configLayout->setColumnStretch(2, 0);

    auto createLabel = [](const QString& text) {
        auto* lbl = new QLabel(text);
        lbl->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        lbl->setStyleSheet("color: #AAA; font-weight: bold; border: none; background: transparent;");
        return lbl;
    };

    auto setEditStyle = [](QLineEdit* edit) {
        edit->setClearButtonEnabled(true);
        edit->setStyleSheet(
            "QLineEdit { background: #252526; border: 1px solid #333; border-radius: 4px; padding: 6px; color: #EEE; }"
            "QLineEdit:focus { border-color: #007ACC; }"
        );
    };

    // 1. 搜索目录
    configLayout->addWidget(createLabel("搜索目录:"), 0, 0);
    m_pathEdit = new ClickableLineEdit();
    m_pathEdit->setPlaceholderText("选择搜索根目录 (双击查看历史)...");
    setEditStyle(m_pathEdit);
    connect(m_pathEdit, &QLineEdit::returnPressed, this, &KeywordSearchWidget::onSearch);
    connect(m_pathEdit, &ClickableLineEdit::doubleClicked, this, &KeywordSearchWidget::onShowHistory);
    configLayout->addWidget(m_pathEdit, 0, 1);

    auto* browseBtn = new QPushButton();
    browseBtn->setFixedSize(38, 32);
    browseBtn->setIcon(IconHelper::getIcon("folder", "#EEE", 18));
    browseBtn->setToolTip(StringUtils::wrapToolTip("浏览文件夹"));
    browseBtn->setAutoDefault(false);
    browseBtn->setCursor(Qt::PointingHandCursor);
    browseBtn->setStyleSheet("QPushButton { background: #3E3E42; border: none; border-radius: 4px; } QPushButton:hover { background: #4E4E52; }");
    connect(browseBtn, &QPushButton::clicked, this, &KeywordSearchWidget::onBrowseFolder);
    configLayout->addWidget(browseBtn, 0, 2);

    // 2. 文件过滤
    configLayout->addWidget(createLabel("文件过滤:"), 1, 0);
    m_filterEdit = new QLineEdit();
    m_filterEdit->setPlaceholderText("例如: *.py, *.txt (留空则扫描所有文本文件)");
    setEditStyle(m_filterEdit);
    connect(m_filterEdit, &QLineEdit::returnPressed, this, &KeywordSearchWidget::onSearch);
    configLayout->addWidget(m_filterEdit, 1, 1, 1, 2);

    // 3. 查找内容
    configLayout->addWidget(createLabel("查找内容:"), 2, 0);
    m_searchEdit = new ClickableLineEdit();
    m_searchEdit->setPlaceholderText("输入要查找的内容 (双击查看历史)...");
    setEditStyle(m_searchEdit);
    connect(m_searchEdit, &QLineEdit::returnPressed, this, &KeywordSearchWidget::onSearch);
    connect(m_searchEdit, &ClickableLineEdit::doubleClicked, this, &KeywordSearchWidget::onShowHistory);
    configLayout->addWidget(m_searchEdit, 2, 1);

    // 4. 替换内容
    configLayout->addWidget(createLabel("替换内容:"), 3, 0);
    m_replaceEdit = new ClickableLineEdit();
    m_replaceEdit->setPlaceholderText("替换为 (双击查看历史)...");
    setEditStyle(m_replaceEdit);
    connect(m_replaceEdit, &QLineEdit::returnPressed, this, &KeywordSearchWidget::onSearch);
    connect(m_replaceEdit, &ClickableLineEdit::doubleClicked, this, &KeywordSearchWidget::onShowHistory);
    configLayout->addWidget(m_replaceEdit, 3, 1);

    // 交换按钮 (跨越查找和替换行)
    auto* swapBtn = new QPushButton();
    swapBtn->setFixedSize(32, 74); 
    swapBtn->setCursor(Qt::PointingHandCursor);
    swapBtn->setToolTip(StringUtils::wrapToolTip("交换查找与替换内容"));
    swapBtn->setIcon(IconHelper::getIcon("swap", "#AAA", 20));
    swapBtn->setAutoDefault(false);
    swapBtn->setStyleSheet("QPushButton { background: #3E3E42; border: none; border-radius: 4px; } QPushButton:hover { background: #4E4E52; }");
    connect(swapBtn, &QPushButton::clicked, this, &KeywordSearchWidget::onSwapSearchReplace);
    configLayout->addWidget(swapBtn, 2, 2, 2, 1);

    // 选项
    m_caseCheck = new QCheckBox("区分大小写");
    m_caseCheck->setStyleSheet("QCheckBox { color: #AAA; }");
    configLayout->addWidget(m_caseCheck, 4, 1, 1, 2);

    midLayout->addWidget(configGroup);

    // --- 按钮区域 ---
    auto* btnLayout = new QHBoxLayout();
    auto* searchBtn = new QPushButton(" 智能搜索");
    searchBtn->setAutoDefault(false);
    searchBtn->setIcon(IconHelper::getIcon("find_keyword", "#FFF", 16));
    searchBtn->setStyleSheet("QPushButton { background: #007ACC; border: none; border-radius: 4px; padding: 8px 20px; color: #FFF; font-weight: bold; } QPushButton:hover { background: #0098FF; }");
    connect(searchBtn, &QPushButton::clicked, this, &KeywordSearchWidget::onSearch);

    auto* replaceBtn = new QPushButton(" 执行替换");
    replaceBtn->setAutoDefault(false);
    replaceBtn->setIcon(IconHelper::getIcon("edit", "#FFF", 16));
    replaceBtn->setStyleSheet("QPushButton { background: #D32F2F; border: none; border-radius: 4px; padding: 8px 20px; color: #FFF; font-weight: bold; } QPushButton:hover { background: #F44336; }");
    connect(replaceBtn, &QPushButton::clicked, this, &KeywordSearchWidget::onReplace);

    auto* undoBtn = new QPushButton(" 撤销替换");
    undoBtn->setAutoDefault(false);
    undoBtn->setIcon(IconHelper::getIcon("undo", "#EEE", 16));
    undoBtn->setStyleSheet("QPushButton { background: #3E3E42; border: none; border-radius: 4px; padding: 8px 20px; color: #EEE; } QPushButton:hover { background: #4E4E52; }");
    connect(undoBtn, &QPushButton::clicked, this, &KeywordSearchWidget::onUndo);

    auto* clearBtn = new QPushButton(" 清空日志");
    clearBtn->setAutoDefault(false);
    clearBtn->setIcon(IconHelper::getIcon("trash", "#EEE", 16));
    clearBtn->setStyleSheet("QPushButton { background: #3E3E42; border: none; border-radius: 4px; padding: 8px 20px; color: #EEE; } QPushButton:hover { background: #4E4E52; }");
    connect(clearBtn, &QPushButton::clicked, this, &KeywordSearchWidget::onClearLog);

    btnLayout->addWidget(searchBtn);
    btnLayout->addWidget(replaceBtn);
    btnLayout->addWidget(undoBtn);
    btnLayout->addWidget(clearBtn);
    btnLayout->addStretch();
    midLayout->addLayout(btnLayout);

    // --- 结果列表展示区域 ---
    auto* listHeaderLayout = new QHBoxLayout();
    auto* listTitle = new QLabel("搜索结果");
    listTitle->setStyleSheet("color: #888; font-size: 11px; font-weight: bold; border: none; background: transparent;");
    listHeaderLayout->addWidget(listTitle);
    listHeaderLayout->addStretch();

    auto* btnCopyAll = new QToolButton();
    btnCopyAll->setIcon(IconHelper::getIcon("copy", "#1abc9c", 14));
    btnCopyAll->setToolTip(StringUtils::wrapToolTip("复制所有结果的路径"));
    btnCopyAll->setFixedSize(20, 20);
    btnCopyAll->setCursor(Qt::PointingHandCursor);
    btnCopyAll->setStyleSheet("QToolButton { border: none; background: transparent; padding: 2px; }"
                               "QToolButton:hover { background-color: #3E3E42; border-radius: 4px; }");
    connect(btnCopyAll, &QToolButton::clicked, this, &KeywordSearchWidget::copySelectedPaths);
    listHeaderLayout->addWidget(btnCopyAll);
    midLayout->addLayout(listHeaderLayout);

    m_resultList = new QListWidget();
    m_resultList->setObjectName("FileList");
    m_resultList->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_resultList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_resultList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_resultList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_resultList->setDragEnabled(true);
    m_resultList->setDragDropMode(QAbstractItemView::DragOnly);
    connect(m_resultList, &QListWidget::customContextMenuRequested, this, &KeywordSearchWidget::showResultContextMenu);
    connect(m_resultList, &QListWidget::itemDoubleClicked, [this](QListWidgetItem* item){
        QString path = item->data(Qt::UserRole).toString();
        if(!path.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });
    midLayout->addWidget(m_resultList, 1);

    // --- 状态栏 ---
    auto* statusLayout = new QVBoxLayout();
    m_progressBar = new QProgressBar();
    m_progressBar->setFixedHeight(4);
    m_progressBar->setTextVisible(false);
    m_progressBar->setStyleSheet("QProgressBar { background: #252526; border: none; } QProgressBar::chunk { background: #007ACC; }");
    m_progressBar->hide();
    
    m_statusLabel = new QLabel("就绪");
    m_statusLabel->setStyleSheet("color: #888; font-size: 11px;");
    
    statusLayout->addWidget(m_progressBar);
    statusLayout->addWidget(m_statusLabel);
    midLayout->addLayout(statusLayout);

    splitter->addWidget(midWidget);

    // --- 右侧边栏 (文件收藏) ---
    auto* collectionWidget = new QWidget();
    auto* collectionLayout = new QVBoxLayout(collectionWidget);
    collectionLayout->setContentsMargins(10, 0, 0, 0);
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

    m_collectionSidebar = new KeywordCollectionListWidget();
    m_collectionSidebar->setObjectName("SidebarList");
    m_collectionSidebar->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_collectionSidebar->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_collectionSidebar->setMinimumWidth(200);
    m_collectionSidebar->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_collectionSidebar, &KeywordCollectionListWidget::filesDropped, this, [this](const QStringList& paths){
        for(const QString& p : paths) addCollectionItem(p);
    });
    connect(m_collectionSidebar, &QListWidget::itemClicked, this, &KeywordSearchWidget::onCollectionItemClicked);
    connect(m_collectionSidebar, &QListWidget::customContextMenuRequested, this, &KeywordSearchWidget::showCollectionContextMenu);
    collectionLayout->addWidget(m_collectionSidebar);

    auto* btnMergeColl = new QPushButton("合并收藏内容");
    btnMergeColl->setFixedHeight(32);
    btnMergeColl->setCursor(Qt::PointingHandCursor);
    btnMergeColl->setStyleSheet(
        "QPushButton { background-color: #2D2D30; border: 1px solid #444; color: #AAA; border-radius: 4px; font-size: 12px; }"
        "QPushButton:hover { background-color: #3E3E42; color: #FFF; border-color: #666; }"
    );
    connect(btnMergeColl, &QPushButton::clicked, this, &KeywordSearchWidget::onMergeCollectionFiles);
    collectionLayout->addWidget(btnMergeColl);

    splitter->addWidget(collectionWidget);

    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 0);

    // 快捷键支持
    m_actionSearch = new QAction(this);
    connect(m_actionSearch, &QAction::triggered, this, &KeywordSearchWidget::onSearch);
    addAction(m_actionSearch);

    m_actionReplace = new QAction(this);
    connect(m_actionReplace, &QAction::triggered, this, &KeywordSearchWidget::onReplace);
    addAction(m_actionReplace);

    m_actionUndo = new QAction(this);
    connect(m_actionUndo, &QAction::triggered, this, &KeywordSearchWidget::onUndo);
    addAction(m_actionUndo);

    m_actionSwap = new QAction(this);
    connect(m_actionSwap, &QAction::triggered, this, &KeywordSearchWidget::onSwapSearchReplace);
    addAction(m_actionSwap);

    m_actionSelectAll = new QAction(this);
    connect(m_actionSelectAll, &QAction::triggered, [this](){ m_resultList->selectAll(); });
    m_resultList->addAction(m_actionSelectAll);

    m_actionCopyPaths = new QAction(this);
    connect(m_actionCopyPaths, &QAction::triggered, this, &KeywordSearchWidget::copySelectedPaths);
    m_resultList->addAction(m_actionCopyPaths);

    m_actionCopyFiles = new QAction(this);
    connect(m_actionCopyFiles, &QAction::triggered, this, &KeywordSearchWidget::copySelectedFiles);
    m_resultList->addAction(m_actionCopyFiles);

    updateShortcuts();
    connect(&ShortcutManager::instance(), &ShortcutManager::shortcutsChanged, this, &KeywordSearchWidget::updateShortcuts);
}

void KeywordSearchWidget::updateShortcuts() {
    auto& sm = ShortcutManager::instance();
    if (m_actionSearch) m_actionSearch->setShortcut(sm.getShortcut("ks_search"));
    if (m_actionReplace) m_actionReplace->setShortcut(sm.getShortcut("ks_replace"));
    if (m_actionUndo) m_actionUndo->setShortcut(sm.getShortcut("ks_undo"));
    if (m_actionSwap) m_actionSwap->setShortcut(sm.getShortcut("ks_swap"));
    if (m_actionSelectAll) m_actionSelectAll->setShortcut(sm.getShortcut("fs_select_all"));
    if (m_actionCopyPaths) m_actionCopyPaths->setShortcut(sm.getShortcut("fs_copy"));
}

void KeywordSearchWidget::onSidebarItemClicked(QListWidgetItem* item) {
    if (!item) return;
    QString path = item->data(Qt::UserRole).toString();
    m_pathEdit->setText(path);
}

void KeywordSearchWidget::showSidebarContextMenu(const QPoint& pos) {
    QListWidgetItem* item = m_sidebar->itemAt(pos);
    if (!item) return;

    QMenu menu(this);
    IconHelper::setupMenu(&menu);
    menu.setStyleSheet("QMenu { background-color: #252526; border: 1px solid #444; color: #EEE; } QMenu::item:selected { background-color: #37373D; }");
    
    QAction* pinAct = menu.addAction(IconHelper::getIcon("pin", "#F1C40F"), "置顶文件夹");
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

void KeywordSearchWidget::addFavorite(const QString& path) {
    // 检查是否已存在
    for (int i = 0; i < m_sidebar->count(); ++i) {
        if (m_sidebar->item(i)->data(Qt::UserRole).toString() == path) return;
    }

    QFileInfo fi(path);
    auto* item = new QListWidgetItem(IconHelper::getIcon("folder", "#F1C40F"), fi.fileName());
    item->setData(Qt::UserRole, path);
    item->setToolTip(StringUtils::wrapToolTip(path));
    m_sidebar->addItem(item);
    saveFavorites();
}

void KeywordSearchWidget::loadFavorites() {
    QSettings settings("RapidNotes", "KeywordSearchFavorites");
    QStringList favs = settings.value("list").toStringList();
    for (const QString& path : std::as_const(favs)) {
        if (QDir(path).exists()) {
            QFileInfo fi(path);
            auto* item = new QListWidgetItem(IconHelper::getIcon("folder", "#F1C40F"), fi.fileName());
            item->setData(Qt::UserRole, path);
            item->setToolTip(StringUtils::wrapToolTip(path));
            m_sidebar->addItem(item);
        }
    }
}

void KeywordSearchWidget::saveFavorites() {
    QStringList favs;
    for (int i = 0; i < m_sidebar->count(); ++i) {
        favs << m_sidebar->item(i)->data(Qt::UserRole).toString();
    }
    QSettings settings("RapidNotes", "KeywordSearchFavorites");
    settings.setValue("list", favs);
}

void KeywordSearchWidget::onBrowseFolder() {
    QString folder = QFileDialog::getExistingDirectory(this, "选择搜索目录");
    if (!folder.isEmpty()) {
        m_pathEdit->setText(folder);
    }
}

bool KeywordSearchWidget::isTextFile(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return false;
    
    QByteArray chunk = file.read(1024);
    file.close();

    if (chunk.isEmpty()) return true;
    if (chunk.contains('\0')) return false;

    return true;
}

void KeywordSearchWidget::onSearch() {
    QString rootDir = m_pathEdit->text().trimmed();
    QString keyword = m_searchEdit->text().trimmed();
    QString replaceText = m_replaceEdit->text().trimmed();
    if (rootDir.isEmpty() || keyword.isEmpty()) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #e74c3c;'>✖ 目录和查找内容不能为空!</b>");
        return;
    }

    // 保存历史记录
    addHistoryEntry(Path, rootDir);
    addHistoryEntry(Keyword, keyword);
    if (!replaceText.isEmpty()) {
        addHistoryEntry(Replace, replaceText);
    }

    m_resultList->clear();
    m_resultsData.clear();
    m_progressBar->show();
    m_progressBar->setRange(0, 0);
    m_statusLabel->setText("正在搜索...");

    QString filter = m_filterEdit->text();
    bool caseSensitive = m_caseCheck->isChecked();

    (void)QtConcurrent::run([this, rootDir, keyword, filter, caseSensitive]() {
        int scannedFiles = 0;
        struct TmpMatch { QString path; int count; };
        QList<TmpMatch> matches;

        QStringList filters;
        if (!filter.isEmpty()) {
            filters = filter.split(QRegularExpression("[,\\s;]+"), Qt::SkipEmptyParts);
        }

        QDirIterator it(rootDir, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            QString filePath = it.next();
            
            // 过滤目录
            bool skip = false;
            for (const QString& ignore : m_ignoreDirs) {
                if (filePath.contains("/" + ignore + "/") || filePath.contains("\\" + ignore + "\\")) {
                    skip = true;
                    break;
                }
            }
            if (skip) continue;

            // 过滤文件名
            if (!filters.isEmpty()) {
                bool matchFilter = false;
                QString fileName = QFileInfo(filePath).fileName();
                for (const QString& f : filters) {
                    QRegularExpression re(QRegularExpression::wildcardToRegularExpression(f));
                    if (re.match(fileName).hasMatch()) {
                        matchFilter = true;
                        break;
                    }
                }
                if (!matchFilter) continue;
            }

            if (!isTextFile(filePath)) continue;

            scannedFiles++;
            QFile file(filePath);
            if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QTextStream in(&file);
                QString content = in.readAll();
                file.close();

                Qt::CaseSensitivity cs = caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
                if (content.contains(keyword, cs)) {
                    int count = content.count(keyword, cs);
                    matches.append({filePath, count});
                }
            }
        }

        QMetaObject::invokeMethod(this, [this, scannedFiles, matches, keyword]() {
            for(const auto& m : matches) {
                m_resultsData.append({m.path, m.count});
                QString fileName = QFileInfo(m.path).fileName();
                auto* item = new QListWidgetItem("");
                item->setData(Qt::UserRole, m.path);
                item->setToolTip(StringUtils::wrapToolTip(m.path));
                m_resultList->addItem(item);

                auto* widget = new KeywordResultItem(fileName, QString::number(m.count), QColor("#007ACC"));
                m_resultList->setItemWidget(item, widget);
            }

            m_statusLabel->setText(QString("扫描 %1 个文件，找到 %2 个匹配").arg(scannedFiles).arg(matches.size()));
            m_progressBar->hide();
        });
    });
}

void KeywordSearchWidget::onReplace() {
    QString rootDir = m_pathEdit->text().trimmed();
    QString keyword = m_searchEdit->text().trimmed();
    QString replaceText = m_replaceEdit->text().trimmed();
    if (rootDir.isEmpty() || keyword.isEmpty()) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #e74c3c;'>✖ 目录和查找内容不能为空!</b>");
        return;
    }

    // 保存历史记录
    addHistoryEntry(Path, rootDir);
    addHistoryEntry(Keyword, keyword);
    if (!replaceText.isEmpty()) {
        addHistoryEntry(Replace, replaceText);
    }

    m_resultList->clear();
    m_resultsData.clear();
    ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #007acc;'>ℹ 正在开始批量替换...</b>");

    m_progressBar->show();
    m_progressBar->setRange(0, 0);
    m_statusLabel->setText("正在替换...");

    QString filter = m_filterEdit->text();
    bool caseSensitive = m_caseCheck->isChecked();

    (void)QtConcurrent::run([this, rootDir, keyword, replaceText, filter, caseSensitive]() {
        int modifiedFiles = 0;
        QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
        QString backupDirName = "_backup_" + timestamp;
        QDir root(rootDir);
        root.mkdir(backupDirName);
        m_lastBackupPath = root.absoluteFilePath(backupDirName);

        QStringList filters;
        if (!filter.isEmpty()) {
            filters = filter.split(QRegularExpression("[,\\s;]+"), Qt::SkipEmptyParts);
        }

        QDirIterator it(rootDir, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            QString filePath = it.next();
            if (filePath.contains(backupDirName)) continue;

            // 过滤目录和文件名（逻辑同搜索）
            bool skip = false;
            for (const QString& ignore : m_ignoreDirs) {
                if (filePath.contains("/" + ignore + "/") || filePath.contains("\\" + ignore + "\\")) {
                    skip = true;
                    break;
                }
            }
            if (skip) continue;

            if (!filters.isEmpty()) {
                bool matchFilter = false;
                QString fileName = QFileInfo(filePath).fileName();
                for (const QString& f : filters) {
                    QRegularExpression re(QRegularExpression::wildcardToRegularExpression(f));
                    if (re.match(fileName).hasMatch()) {
                        matchFilter = true;
                        break;
                    }
                }
                if (!matchFilter) continue;
            }

            if (!isTextFile(filePath)) continue;

            QFile file(filePath);
            if (file.open(QIODevice::ReadWrite | QIODevice::Text)) {
                QTextStream in(&file);
                QString content = in.readAll();
                
                Qt::CaseSensitivity cs = caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
                if (content.contains(keyword, cs)) {
                    // 备份
                    QString fileName = QFileInfo(filePath).fileName();
                    QFile::copy(filePath, m_lastBackupPath + "/" + fileName + ".bak");

                    // 替换
                    QString newContent;
                    if (caseSensitive) {
                        newContent = content.replace(keyword, replaceText);
                    } else {
                        newContent = content.replace(QRegularExpression(QRegularExpression::escape(keyword), QRegularExpression::CaseInsensitiveOption), replaceText);
                    }

                    file.resize(0);
                    in << newContent;
                    modifiedFiles++;

                    QMetaObject::invokeMethod(this, [this, filePath]() {
                        QString fileName = QFileInfo(filePath).fileName();
                        auto* item = new QListWidgetItem("");
                        item->setData(Qt::UserRole, filePath);
                        item->setToolTip(StringUtils::wrapToolTip(filePath));
                        m_resultList->addItem(item);

                        auto* widget = new KeywordResultItem(fileName, "已修改", QColor("#6A9955"));
                        m_resultList->setItemWidget(item, widget);
                    });
                }
                file.close();
            }
        }

        QMetaObject::invokeMethod(this, [this, modifiedFiles]() {
            m_statusLabel->setText(QString("替换完成: 修改了 %1 个文件").arg(modifiedFiles));
            m_progressBar->hide();
            ToolTipOverlay::instance()->showText(QCursor::pos(), QString("✔ 已修改 %1 个文件 (备份于 %2)").arg(modifiedFiles).arg(m_lastBackupPath));
        });
    });
}

void KeywordSearchWidget::onUndo() {
    if (m_lastBackupPath.isEmpty() || !QDir(m_lastBackupPath).exists()) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #e74c3c;'>✖ 未找到有效的备份目录！</b>");
        return;
    }

    int restored = 0;
    QDir backupDir(m_lastBackupPath);
    QStringList baks = backupDir.entryList({"*.bak"});
    
    QString rootDir = m_pathEdit->text();

    m_resultList->clear();
    for (const QString& bak : baks) {
        QString origName = bak.left(bak.length() - 4);
        
        // 在根目录下寻找原始文件（简化策略：找同名文件）
        QDirIterator it(rootDir, {origName}, QDir::Files, QDirIterator::Subdirectories);
        if (it.hasNext()) {
            QString targetPath = it.next();
            if (QFile::remove(targetPath)) {
                if (QFile::copy(backupDir.absoluteFilePath(bak), targetPath)) {
                    restored++;
                    auto* item = new QListWidgetItem("");
                    item->setData(Qt::UserRole, targetPath);
                    item->setToolTip(StringUtils::wrapToolTip(targetPath));
                    m_resultList->addItem(item);

                    auto* widget = new KeywordResultItem(origName, "已恢复", QColor("#007ACC"));
                    m_resultList->setItemWidget(item, widget);
                }
            }
        }
    }

    m_statusLabel->setText(QString("撤销完成，已恢复 %1 个文件").arg(restored));
    ToolTipOverlay::instance()->showText(QCursor::pos(), QString("✔ 已恢复 %1 个文件").arg(restored));
}

void KeywordSearchWidget::onClearLog() {
    m_resultList->clear();
    m_resultsData.clear();
    m_statusLabel->setText("就绪");
}

void KeywordSearchWidget::showResultContextMenu(const QPoint& pos) {
    auto selectedItems = m_resultList->selectedItems();
    if (selectedItems.isEmpty()) {
        auto* item = m_resultList->itemAt(pos);
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
    menu.addAction(IconHelper::getIcon("copy", "#2ECC71"), copyPathText, [this](){ copySelectedPaths(); });

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
        auto selectedItems = m_resultList->selectedItems();
        for (auto* item : selectedItems) {
            QString p = item->data(Qt::UserRole).toString();
            if (!p.isEmpty()) addCollectionItem(p);
        }
    });

    menu.exec(m_resultList->mapToGlobal(pos));
}

void KeywordSearchWidget::onEditFile() {
    auto selectedItems = m_resultList->selectedItems();
    if (selectedItems.isEmpty()) return;

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

void KeywordSearchWidget::copySelectedPaths() {
    auto selectedItems = m_resultList->selectedItems();
    QStringList paths;
    if (selectedItems.isEmpty()) {
        for (int i = 0; i < m_resultList->count(); ++i) {
            paths << m_resultList->item(i)->data(Qt::UserRole).toString();
        }
    } else {
        for (auto* item : selectedItems) {
            paths << item->data(Qt::UserRole).toString();
        }
    }

    if (paths.isEmpty()) return;
    QApplication::clipboard()->setText(paths.join("\n"));
    ToolTipOverlay::instance()->showText(QCursor::pos(), "✔ 已复制路径到剪贴板");
}

void KeywordSearchWidget::copySelectedFiles() {
    auto selectedItems = m_resultList->selectedItems();
    if (selectedItems.isEmpty()) return;

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

    ToolTipOverlay::instance()->showText(QCursor::pos(), QString("✔ 已复制 %1 个文件").arg(urls.size()));
}

void KeywordSearchWidget::onMergeFiles(const QStringList& filePaths, const QString& rootPath, bool useCombineDir) {
    if (filePaths.isEmpty()) return;

    QString targetDir = rootPath;
    if (useCombineDir) {
        targetDir = QCoreApplication::applicationDirPath() + "/Combine";
        QDir dir(targetDir);
        if (!dir.exists()) dir.mkpath(".");
    }

    QString ts = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString outName = QString("%1_keyword_export.md").arg(ts);
    QString outPath = QDir(targetDir).filePath(outName);

    QFile outFile(outPath);
    if (!outFile.open(QIODevice::WriteOnly | QIODevice::Text)) return;

    QTextStream out(&outFile);
    out.setEncoding(QStringConverter::Utf8);

    out << "# 关键字搜索导出结果 - " << ts << "\n\n";
    out << "**项目路径**: `" << rootPath << "`\n\n";
    out << "**文件总数**: " << filePaths.size() << "\n\n";

    for (const QString& fp : filePaths) {
        QString relPath = rootPath.isEmpty() ? fp : QDir(rootPath).relativeFilePath(fp);
        QString lang = getFileLanguage(fp);

        out << "## 文件: `" << relPath << "`\n\n";
        out << "```" << lang << "\n";

        QFile inFile(fp);
        if (inFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            out << inFile.readAll();
        }
        out << "\n```\n\n";
    }

    outFile.close();
    ToolTipOverlay::instance()->showText(QCursor::pos(), QString("✔ 已保存: %1").arg(outName));
}

void KeywordSearchWidget::onMergeSelectedFiles() {
    auto selectedItems = m_resultList->selectedItems();
    if (selectedItems.isEmpty()) return;

    QStringList paths;
    for (auto* item : std::as_const(selectedItems)) {
        QString p = item->data(Qt::UserRole).toString();
        if (!p.isEmpty() && isSupportedFile(p)) paths << p;
    }

    if (paths.isEmpty()) return;
    onMergeFiles(paths, m_pathEdit->text().trimmed());
}

void KeywordSearchWidget::onMergeCollectionFiles() {
    QStringList paths;
    for (int i = 0; i < m_collectionSidebar->count(); ++i) {
        paths << m_collectionSidebar->item(i)->data(Qt::UserRole).toString();
    }
    if (paths.isEmpty()) return;
    onMergeFiles(paths, "", true);
}

void KeywordSearchWidget::onCollectionItemClicked(QListWidgetItem* item) {
}

void KeywordSearchWidget::showCollectionContextMenu(const QPoint& pos) {
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

void KeywordSearchWidget::addCollectionItem(const QString& path) {
    for (int i = 0; i < m_collectionSidebar->count(); ++i) {
        if (m_collectionSidebar->item(i)->data(Qt::UserRole).toString() == path) return;
    }

    QFileInfo fi(path);
    auto* item = new QListWidgetItem(IconHelper::getIcon("file", "#2ECC71"), fi.fileName());
    item->setData(Qt::UserRole, path);
    item->setToolTip(StringUtils::wrapToolTip(path));
    m_collectionSidebar->addItem(item);
    saveCollection();
}

void KeywordSearchWidget::loadCollection() {
    QSettings settings("RapidNotes", "KeywordSearchCollection");
    QStringList coll = settings.value("list").toStringList();
    for (const QString& path : std::as_const(coll)) {
        if (QFile::exists(path)) {
            QFileInfo fi(path);
            auto* item = new QListWidgetItem(IconHelper::getIcon("file", "#2ECC71"), fi.fileName());
            item->setData(Qt::UserRole, path);
            item->setToolTip(StringUtils::wrapToolTip(path));
            m_collectionSidebar->addItem(item);
        }
    }
}

void KeywordSearchWidget::saveCollection() {
    QStringList coll;
    for (int i = 0; i < m_collectionSidebar->count(); ++i) {
        coll << m_collectionSidebar->item(i)->data(Qt::UserRole).toString();
    }
    QSettings settings("RapidNotes", "KeywordSearchCollection");
    settings.setValue("list", coll);
}

void KeywordSearchWidget::onSwapSearchReplace() {
    QString searchTxt = m_searchEdit->text();
    QString replaceTxt = m_replaceEdit->text();
    m_searchEdit->setText(replaceTxt);
    m_replaceEdit->setText(searchTxt);
}

void KeywordSearchWidget::addHistoryEntry(HistoryType type, const QString& text) {
    if (text.isEmpty()) return;
    QString key = "keywordList";
    if (type == Path) key = "pathList";
    else if (type == Replace) key = "replaceList";

    QSettings settings("RapidNotes", "KeywordSearchHistory");
    QStringList history = settings.value(key).toStringList();
    history.removeAll(text);
    history.prepend(text);
    while (history.size() > 10) history.removeLast();
    settings.setValue(key, history);
}

void KeywordSearchWidget::onShowHistory() {
    auto* edit = qobject_cast<ClickableLineEdit*>(sender());
    if (!edit) return;

    KeywordSearchHistoryPopup::Type type = KeywordSearchHistoryPopup::Keyword;
    if (edit == m_pathEdit) type = KeywordSearchHistoryPopup::Path;
    else if (edit == m_replaceEdit) type = KeywordSearchHistoryPopup::Replace;
    
    auto* popup = new KeywordSearchHistoryPopup(this, edit, type);
    popup->setAttribute(Qt::WA_DeleteOnClose);
    popup->showAnimated();
}

// ----------------------------------------------------------------------------
// KeywordSearchWindow 实现
// ----------------------------------------------------------------------------
KeywordSearchWindow::KeywordSearchWindow(QWidget* parent) : FramelessDialog("查找关键字", parent) {
    setObjectName("KeywordSearchWindow");
    loadWindowSettings();
    resize(1200, 700);
    m_searchWidget = new KeywordSearchWidget(m_contentArea);
    auto* layout = new QVBoxLayout(m_contentArea);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_searchWidget);
    m_resizeHandle = new ResizeHandle(this, this);
    m_resizeHandle->raise();
}

KeywordSearchWindow::~KeywordSearchWindow() {
}

void KeywordSearchWindow::hideEvent(QHideEvent* event) {
    FramelessDialog::hideEvent(event);
}

void KeywordSearchWindow::resizeEvent(QResizeEvent* event) {
    FramelessDialog::resizeEvent(event);
    if (m_resizeHandle) {
        m_resizeHandle->move(width() - 20, height() - 20);
    }
}

#include "KeywordSearchWindow.moc"
