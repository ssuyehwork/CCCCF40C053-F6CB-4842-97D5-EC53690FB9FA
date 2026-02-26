#include "ToolTipOverlay.h"
#include "KeywordSearchWidget.h"
#include "SearchAppWindow.h"
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
#include <QCoreApplication>
#include <QMimeData>

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
        btnDel->setStyleSheet("QPushButton { background: transparent; border-radius: 4px; } QPushButton:hover { background: #E74C3C; }");

        connect(btnDel, &QPushButton::clicked, this, [this](){ emit deleted(m_text); });
        layout->addWidget(btnDel);

        setStyleSheet("#KeywordChip { background-color: transparent; border: none; border-radius: 4px; } #KeywordChip:hover { background-color: #3E3E42; }");
    }
    void mousePressEvent(QMouseEvent* e) override { if(e->button() == Qt::LeftButton) emit clicked(m_text); QFrame::mousePressEvent(e); }
signals:
    void clicked(const QString& text); void deleted(const QString& text);
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
        m_widget = widget; m_edit = edit; m_type = type;
        setAttribute(Qt::WA_TranslucentBackground);
        auto* rootLayout = new QVBoxLayout(this);
        rootLayout->setContentsMargins(12, 12, 12, 12);
        auto* container = new QFrame(); container->setObjectName("PopupContainer");
        container->setStyleSheet("#PopupContainer { background-color: #252526; border: 1px solid #444; border-radius: 10px; }");
        rootLayout->addWidget(container);
        auto* layout = new QVBoxLayout(container);
        layout->setContentsMargins(12, 12, 12, 12); layout->setSpacing(10);
        auto* top = new QHBoxLayout();
        auto* icon = new QLabel(); icon->setPixmap(IconHelper::getIcon("clock", "#888").pixmap(14, 14));
        top->addWidget(icon);
        QString titleStr = (m_type == Path) ? "最近路径" : ((m_type == Keyword) ? "最近查找" : "最近替换");
        auto* title = new QLabel(titleStr); title->setStyleSheet("color: #888; font-weight: bold; font-size: 11px;");
        top->addWidget(title); top->addStretch();
        auto* clearBtn = new QPushButton("清空"); clearBtn->setCursor(Qt::PointingHandCursor);
        clearBtn->setStyleSheet("QPushButton { background: transparent; color: #666; font-size: 11px; } QPushButton:hover { color: #E74C3C; }");
        connect(clearBtn, &QPushButton::clicked, [this](){ clearAllHistory(); refreshUI(); });
        top->addWidget(clearBtn); layout->addLayout(top);
        auto* scroll = new QScrollArea(); scroll->setWidgetResizable(true);
        scroll->setStyleSheet("QScrollArea { background: transparent; border: none; }");
        m_chipsWidget = new QWidget(); m_vLayout = new QVBoxLayout(m_chipsWidget);
        m_vLayout->setContentsMargins(0,0,0,0); m_vLayout->setSpacing(2); m_vLayout->addStretch();
        scroll->setWidget(m_chipsWidget); layout->addWidget(scroll);
        m_opacityAnim = new QPropertyAnimation(this, "windowOpacity"); m_opacityAnim->setDuration(200);
    }
    void clearAllHistory() {
        QString key = (m_type == Path) ? "pathList" : ((m_type == Replace) ? "replaceList" : "keywordList");
        QSettings("RapidNotes", "KeywordSearchHistory").setValue(key, QStringList());
    }
    void removeEntry(const QString& text) {
        QString key = (m_type == Path) ? "pathList" : ((m_type == Replace) ? "replaceList" : "keywordList");
        QSettings settings("RapidNotes", "KeywordSearchHistory");
        QStringList history = settings.value(key).toStringList(); history.removeAll(text);
        settings.setValue(key, history);
    }
    QStringList getHistory() const {
        QString key = (m_type == Path) ? "pathList" : ((m_type == Replace) ? "replaceList" : "keywordList");
        return QSettings("RapidNotes", "KeywordSearchHistory").value(key).toStringList();
    }
    void refreshUI() {
        QLayoutItem* item; while ((item = m_vLayout->takeAt(0))) { if(item->widget()) item->widget()->deleteLater(); delete item; }
        m_vLayout->addStretch();
        QStringList history = getHistory();
        if(history.isEmpty()) {
            auto* lbl = new QLabel("暂无历史"); lbl->setAlignment(Qt::AlignCenter);
            lbl->setStyleSheet("color: #555; font-style: italic; margin: 20px;");
            m_vLayout->insertWidget(0, lbl);
        } else {
            for(const QString& val : history) {
                auto* chip = new KeywordChip(val); chip->setFixedHeight(32);
                connect(chip, &KeywordChip::clicked, this, [this](const QString& v){ m_edit->setText(v); close(); });
                connect(chip, &KeywordChip::deleted, this, [this](const QString& v){ removeEntry(v); refreshUI(); });
                m_vLayout->insertWidget(m_vLayout->count() - 1, chip);
            }
        }
        resize(m_edit->width() + 24, qMin(410, (int)history.size() * 34 + 60));
    }
    void showAnimated() {
        refreshUI(); QPoint pos = m_edit->mapToGlobal(QPoint(0, m_edit->height())); move(pos.x() - 12, pos.y() - 7);
        setWindowOpacity(0); show(); m_opacityAnim->setStartValue(0); m_opacityAnim->setEndValue(1); m_opacityAnim->start();
    }
private:
    KeywordSearchWidget* m_widget; QLineEdit* m_edit; Type m_type;
    QWidget* m_chipsWidget; QVBoxLayout* m_vLayout; QPropertyAnimation* m_opacityAnim;
};

class KeywordResultItem : public QWidget {
    Q_OBJECT
public:
    KeywordResultItem(const QString& name, const QString& badge, const QColor& badgeColor, QWidget* parent = nullptr)
        : QWidget(parent)
    {
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(10, 0, 10, 0); layout->setSpacing(10);
        auto* nameLabel = new QLabel(name);
        nameLabel->setStyleSheet("color: #CCCCCC; font-size: 13px; border: none; background: transparent;");
        layout->addWidget(nameLabel); layout->addStretch();
        auto* badgeLabel = new QLabel(badge);
        badgeLabel->setFixedWidth(120); badgeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        badgeLabel->setStyleSheet(QString("color: %1; font-size: 12px; font-weight: bold; border: none; background: transparent;").arg(badgeColor.name()));
        layout->addWidget(badgeLabel);
    }
};

// ----------------------------------------------------------------------------
// KeywordSearchWidget 实现
// ----------------------------------------------------------------------------
KeywordSearchWidget::KeywordSearchWidget(QWidget* parent) : QWidget(parent) {
    m_ignoreDirs = {".git", ".svn", ".idea", ".vscode", "__pycache__", "node_modules", "dist", "build", "venv"};
    setupStyles();
    initUI();
}

KeywordSearchWidget::~KeywordSearchWidget() {}

void KeywordSearchWidget::setupStyles() {
    setStyleSheet(R"(
        QWidget { font-family: "Microsoft YaHei", "Segoe UI", sans-serif; font-size: 14px; color: #E0E0E0; outline: none; }
        QListWidget { background-color: #252526; border: 1px solid #333; border-radius: 6px; padding: 4px; }
        QListWidget::item { height: 32px; padding-left: 8px; border-radius: 4px; color: #CCCCCC; }
        QListWidget::item:selected { background-color: #37373D; border-left: 3px solid #007ACC; color: #FFFFFF; }
        QListWidget::item:hover { background-color: #2A2D2E; }
        QLineEdit { background-color: #333; border: 1px solid #444; color: #FFFFFF; border-radius: 6px; padding: 8px; }
        QLineEdit:focus { border: 1px solid #007ACC; background-color: #2D2D2D; }
    )");
}

void KeywordSearchWidget::initUI() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(15);

    auto* configGroup = new QWidget();
    auto* configLayout = new QGridLayout(configGroup);
    configLayout->setContentsMargins(0, 0, 0, 0); configLayout->setColumnStretch(1, 1);

    auto createLabel = [](const QString& text) {
        auto* lbl = new QLabel(text); lbl->setStyleSheet("color: #AAA; font-weight: bold;"); return lbl;
    };

    configLayout->addWidget(createLabel("搜索目录:"), 0, 0);
    m_pathEdit = new ClickableLineEdit();
    m_pathEdit->setPlaceholderText("选择搜索根目录 (双击查看历史)...");
    connect(m_pathEdit, &ClickableLineEdit::doubleClicked, this, &KeywordSearchWidget::onShowHistory);
    configLayout->addWidget(m_pathEdit, 0, 1);

    auto* browseBtn = new QPushButton();
    browseBtn->setFixedSize(38, 32); browseBtn->setIcon(IconHelper::getIcon("folder", "#EEE", 18));
    browseBtn->setCursor(Qt::PointingHandCursor);
    browseBtn->setStyleSheet("QPushButton { background: #3E3E42; border-radius: 4px; } QPushButton:hover { background: #4E4E52; }");
    connect(browseBtn, &QPushButton::clicked, this, &KeywordSearchWidget::onBrowseFolder);
    configLayout->addWidget(browseBtn, 0, 2);

    configLayout->addWidget(createLabel("文件过滤:"), 1, 0);
    m_filterEdit = new QLineEdit();
    m_filterEdit->setPlaceholderText("例如: *.py, *.txt");
    configLayout->addWidget(m_filterEdit, 1, 1, 1, 2);

    configLayout->addWidget(createLabel("查找内容:"), 2, 0);
    m_searchEdit = new ClickableLineEdit();
    m_searchEdit->setPlaceholderText("要查找的内容...");
    connect(m_searchEdit, &ClickableLineEdit::doubleClicked, this, &KeywordSearchWidget::onShowHistory);
    configLayout->addWidget(m_searchEdit, 2, 1);

    configLayout->addWidget(createLabel("替换内容:"), 3, 0);
    m_replaceEdit = new ClickableLineEdit();
    m_replaceEdit->setPlaceholderText("替换为...");
    connect(m_replaceEdit, &ClickableLineEdit::doubleClicked, this, &KeywordSearchWidget::onShowHistory);
    configLayout->addWidget(m_replaceEdit, 3, 1);

    auto* swapBtn = new QPushButton();
    swapBtn->setFixedSize(32, 74); swapBtn->setIcon(IconHelper::getIcon("swap", "#AAA", 20));
    swapBtn->setStyleSheet("QPushButton { background: #3E3E42; border-radius: 4px; }");
    connect(swapBtn, &QPushButton::clicked, this, &KeywordSearchWidget::onSwapSearchReplace);
    configLayout->addWidget(swapBtn, 2, 2, 2, 1);

    m_caseCheck = new QCheckBox("区分大小写");
    configLayout->addWidget(m_caseCheck, 4, 1);
    layout->addWidget(configGroup);

    auto* btnLayout = new QHBoxLayout();
    auto* searchBtn = new QPushButton(" 智能搜索");
    searchBtn->setIcon(IconHelper::getIcon("find_keyword", "#FFF", 16));
    searchBtn->setStyleSheet("QPushButton { background: #007ACC; border-radius: 4px; padding: 8px 20px; color: #FFF; font-weight: bold; }");
    connect(searchBtn, &QPushButton::clicked, this, &KeywordSearchWidget::onSearch);

    auto* replaceBtn = new QPushButton(" 执行替换");
    replaceBtn->setIcon(IconHelper::getIcon("edit", "#FFF", 16));
    replaceBtn->setStyleSheet("QPushButton { background: #D32F2F; border-radius: 4px; padding: 8px 20px; color: #FFF; font-weight: bold; }");
    connect(replaceBtn, &QPushButton::clicked, this, &KeywordSearchWidget::onReplace);

    auto* undoBtn = new QPushButton(" 撤销");
    undoBtn->setStyleSheet("QPushButton { background: #3E3E42; border-radius: 4px; padding: 8px 15px; }");
    connect(undoBtn, &QPushButton::clicked, this, &KeywordSearchWidget::onUndo);

    btnLayout->addWidget(searchBtn); btnLayout->addWidget(replaceBtn); btnLayout->addWidget(undoBtn);
    btnLayout->addStretch();
    layout->addLayout(btnLayout);

    m_resultList = new QListWidget();
    m_resultList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_resultList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_resultList, &QListWidget::customContextMenuRequested, this, &KeywordSearchWidget::showResultContextMenu);
    layout->addWidget(m_resultList, 1);

    m_progressBar = new QProgressBar(); m_progressBar->setFixedHeight(4); m_progressBar->setTextVisible(false); m_progressBar->hide();
    m_statusLabel = new QLabel("就绪"); m_statusLabel->setStyleSheet("color: #888; font-size: 11px;");
    layout->addWidget(m_progressBar); layout->addWidget(m_statusLabel);

    m_actionSearch = new QAction(this); connect(m_actionSearch, &QAction::triggered, this, &KeywordSearchWidget::onSearch); addAction(m_actionSearch);
    m_actionReplace = new QAction(this); connect(m_actionReplace, &QAction::triggered, this, &KeywordSearchWidget::onReplace); addAction(m_actionReplace);
    m_actionUndo = new QAction(this); connect(m_actionUndo, &QAction::triggered, this, &KeywordSearchWidget::onUndo); addAction(m_actionUndo);

    updateShortcuts();
}

void KeywordSearchWidget::updateShortcuts() {
    auto& sm = ShortcutManager::instance();
    if (m_actionSearch) m_actionSearch->setShortcut(sm.getShortcut("ks_search"));
    if (m_actionReplace) m_actionReplace->setShortcut(sm.getShortcut("ks_replace"));
    if (m_actionUndo) m_actionUndo->setShortcut(sm.getShortcut("ks_undo"));
}

void KeywordSearchWidget::onBrowseFolder() {
    QString folder = QFileDialog::getExistingDirectory(this, "选择搜索目录");
    if (!folder.isEmpty()) m_pathEdit->setText(folder);
}

bool KeywordSearchWidget::isTextFile(const QString& filePath) {
    QFile file(filePath); if (!file.open(QIODevice::ReadOnly)) return false;
    QByteArray chunk = file.read(1024); file.close();
    return !chunk.contains('\0');
}

void KeywordSearchWidget::onSearch() {
    QString rootDir = m_pathEdit->text().trimmed();
    QString keyword = m_searchEdit->text().trimmed();
    if (rootDir.isEmpty() || keyword.isEmpty()) return;

    addHistoryEntry(Path, rootDir); addHistoryEntry(Keyword, keyword);
    m_resultList->clear(); m_resultsData.clear();
    m_progressBar->show(); m_progressBar->setRange(0, 0); m_statusLabel->setText("正在搜索...");

    QString filter = m_filterEdit->text(); bool caseSensitive = m_caseCheck->isChecked();

    (void)QtConcurrent::run([this, rootDir, keyword, filter, caseSensitive]() {
        struct TmpMatch { QString path; int count; }; QList<TmpMatch> matches;
        QStringList filters = filter.split(QRegularExpression("[,\\s;]+"), Qt::SkipEmptyParts);
        QDirIterator it(rootDir, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            QString filePath = it.next();
            bool skip = false; for (const QString& ignore : m_ignoreDirs) if (filePath.contains("/" + ignore + "/")) { skip = true; break; }
            if (skip) continue;
            if (!filters.isEmpty()) {
                bool matchFilter = false; QString fileName = QFileInfo(filePath).fileName();
                for (const QString& f : filters) { if (QRegularExpression(QRegularExpression::wildcardToRegularExpression(f)).match(fileName).hasMatch()) { matchFilter = true; break; } }
                if (!matchFilter) continue;
            }
            if (!isTextFile(filePath)) continue;
            QFile file(filePath); if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QString content = QTextStream(&file).readAll();
                int count = content.count(keyword, caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive);
                if (count > 0) matches.append({filePath, count});
            }
        }
        QMetaObject::invokeMethod(this, [this, matches]() {
            for(const auto& m : matches) {
                auto* item = new QListWidgetItem(""); item->setData(Qt::UserRole, m.path);
                m_resultList->addItem(item);
                m_resultList->setItemWidget(item, new KeywordResultItem(QFileInfo(m.path).fileName(), QString::number(m.count), QColor("#007ACC")));
            }
            m_statusLabel->setText(QString("找到 %1 个匹配").arg(matches.size())); m_progressBar->hide();
        });
    });
}

void KeywordSearchWidget::onReplace() {
    QString rootDir = m_pathEdit->text().trimmed();
    QString keyword = m_searchEdit->text().trimmed();
    QString replaceText = m_replaceEdit->text().trimmed();
    if (rootDir.isEmpty() || keyword.isEmpty()) return;

    m_progressBar->show(); m_progressBar->setRange(0, 0); m_statusLabel->setText("正在替换...");
    bool caseSensitive = m_caseCheck->isChecked();

    (void)QtConcurrent::run([this, rootDir, keyword, replaceText, caseSensitive]() {
        int modified = 0;
        QString backupDir = rootDir + "/_backup_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
        QDir().mkdir(backupDir); m_lastBackupPath = backupDir;
        QDirIterator it(rootDir, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            QString filePath = it.next(); if (filePath.contains("_backup_")) continue;
            if (!isTextFile(filePath)) continue;
            QFile file(filePath); if (file.open(QIODevice::ReadWrite | QIODevice::Text)) {
                QString content = file.readAll();
                if (content.contains(keyword, caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive)) {
                    QFile::copy(filePath, backupDir + "/" + QFileInfo(filePath).fileName() + ".bak");
                    file.resize(0); file.write(content.replace(keyword, replaceText).toUtf8());
                    modified++;
                }
            }
        }
        QMetaObject::invokeMethod(this, [this, modified]() {
            m_statusLabel->setText(QString("已修改 %1 个文件").arg(modified)); m_progressBar->hide();
            ToolTipOverlay::instance()->showText(QCursor::pos(), QString("✔ 已完成替换 (%1 个文件)").arg(modified));
        });
    });
}

void KeywordSearchWidget::onUndo() {
    if (m_lastBackupPath.isEmpty()) return;
    QDir backupDir(m_lastBackupPath);
    for (const QString& bak : backupDir.entryList({"*.bak"})) {
        QString orig = bak.left(bak.length() - 4);
        QDirIterator it(m_pathEdit->text(), {orig}, QDir::Files, QDirIterator::Subdirectories);
        if (it.hasNext()) { QString target = it.next(); QFile::remove(target); QFile::copy(backupDir.absoluteFilePath(bak), target); }
    }
    ToolTipOverlay::instance()->showText(QCursor::pos(), "✔ 撤销成功");
}

void KeywordSearchWidget::onClearLog() { m_resultList->clear(); m_statusLabel->setText("就绪"); }

void KeywordSearchWidget::showResultContextMenu(const QPoint& pos) {
    auto selectedItems = m_resultList->selectedItems();
    if (selectedItems.isEmpty()) return;
    QStringList paths; for (auto* it : selectedItems) paths << it->data(Qt::UserRole).toString();

    QMenu menu(this); IconHelper::setupMenu(&menu);
    if (paths.size() == 1) {
        menu.addAction(IconHelper::getIcon("folder", "#F1C40F"), "定位文件夹", [paths](){ QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(paths[0]).absolutePath())); });
        menu.addAction(IconHelper::getIcon("edit", "#3498DB"), "编辑", [this](){ onEditFile(); });
    }
    menu.addAction(IconHelper::getIcon("copy", "#2ECC71"), "复制路径", [paths](){ QApplication::clipboard()->setText(paths.join("\n")); });
    menu.addAction(IconHelper::getIcon("merge", "#3498DB"), "合并选中内容", [this](){ onMergeSelectedFiles(); });
    menu.addAction(IconHelper::getIcon("star", "#F1C40F"), "加入收藏", [this, paths](){
        auto* win = qobject_cast<SearchAppWindow*>(window()); if (win) win->addCollectionItems(paths);
    });
    menu.exec(m_resultList->mapToGlobal(pos));
}

void KeywordSearchWidget::onEditFile() {
    auto selectedItems = m_resultList->selectedItems(); if (selectedItems.isEmpty()) return;
    QSettings settings("RapidNotes", "ExternalEditor");
    QString editorPath = settings.value("EditorPath").toString();
    if (editorPath.isEmpty()) {
        editorPath = QFileDialog::getOpenFileName(this, "选择编辑器"); if (editorPath.isEmpty()) return;
        settings.setValue("EditorPath", editorPath);
    }
    for (auto* it : selectedItems) QProcess::startDetached(editorPath, { QDir::toNativeSeparators(it->data(Qt::UserRole).toString()) });
}

void KeywordSearchWidget::copySelectedPaths() {
    QStringList paths; for (int i = 0; i < m_resultList->count(); ++i) paths << m_resultList->item(i)->data(Qt::UserRole).toString();
    QApplication::clipboard()->setText(paths.join("\n"));
}

void KeywordSearchWidget::copySelectedFiles() {
    QList<QUrl> urls; for (auto* it : m_resultList->selectedItems()) urls << QUrl::fromLocalFile(it->data(Qt::UserRole).toString());
    QMimeData* md = new QMimeData(); md->setUrls(urls); QApplication::clipboard()->setMimeData(md);
}

void KeywordSearchWidget::onMergeFiles(const QStringList& filePaths, const QString& rootPath, bool useCombineDir) {
    if (filePaths.isEmpty()) return;
    QString outPath = QDir(useCombineDir ? QCoreApplication::applicationDirPath() + "/Combine" : rootPath).filePath(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + "_export.md");
    QFile out(outPath); if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream ts(&out); ts << "# 导出结果\n\n";
    for (const QString& fp : filePaths) {
        ts << "## " << fp << "\n```\n"; QFile f(fp); if (f.open(QIODevice::ReadOnly)) ts << f.readAll(); ts << "\n```\n\n";
    }
    ToolTipOverlay::instance()->showText(QCursor::pos(), "✔ 已导出");
}

void KeywordSearchWidget::onMergeSelectedFiles() {
    QStringList paths; for (auto* it : m_resultList->selectedItems()) { QString p = it->data(Qt::UserRole).toString(); if (isSupportedFile(p)) paths << p; }
    onMergeFiles(paths, m_pathEdit->text().trimmed());
}

void KeywordSearchWidget::setPath(const QString& path) { m_pathEdit->setText(path); }
QString KeywordSearchWidget::getCurrentPath() const { return m_pathEdit->text().trimmed(); }

void KeywordSearchWidget::onSwapSearchReplace() {
    QString s = m_searchEdit->text(); m_searchEdit->setText(m_replaceEdit->text()); m_replaceEdit->setText(s);
}

void KeywordSearchWidget::addHistoryEntry(HistoryType type, const QString& text) {
    QString key = (type == Path) ? "pathList" : ((type == Replace) ? "replaceList" : "keywordList");
    QSettings settings("RapidNotes", "KeywordSearchHistory");
    QStringList h = settings.value(key).toStringList(); h.removeAll(text); h.prepend(text);
    while (h.size() > 10) h.removeLast(); settings.setValue(key, h);
}

void KeywordSearchWidget::onShowHistory() {
    auto* edit = qobject_cast<ClickableLineEdit*>(sender()); if (!edit) return;
    auto* popup = new KeywordSearchHistoryPopup(this, edit, (edit == m_pathEdit) ? KeywordSearchHistoryPopup::Path : ((edit == m_replaceEdit) ? KeywordSearchHistoryPopup::Replace : KeywordSearchHistoryPopup::Keyword));
    popup->setAttribute(Qt::WA_DeleteOnClose); popup->showAnimated();
}

#include "KeywordSearchWidget.moc"
