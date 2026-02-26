#include "ToolTipOverlay.h"
#include "FileSearchWidget.h"
#include "StringUtils.h"
#include "SearchAppWindow.h"
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
        btnDel->setStyleSheet("QPushButton { background-color: transparent; border-radius: 4px; padding: 0px; } QPushButton:hover { background-color: #E74C3C; }");

        connect(btnDel, &QPushButton::clicked, this, [this](){ emit deleted(m_text); });
        layout->addWidget(btnDel);

        setStyleSheet("#PathChip { background-color: transparent; border: none; border-radius: 4px; } #PathChip:hover { background-color: #3E3E42; }");
    }
    void mousePressEvent(QMouseEvent* e) override { if(e->button() == Qt::LeftButton) emit clicked(m_text); QFrame::mousePressEvent(e); }
signals:
    void clicked(const QString& text); void deleted(const QString& text);
private:
    QString m_text;
};

class FileSearchHistoryPopup : public QWidget {
    Q_OBJECT
public:
    enum Type { Path, Filename };
    explicit FileSearchHistoryPopup(FileSearchWidget* widget, QLineEdit* edit, Type type)
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
        auto* title = new QLabel(m_type == Path ? "最近扫描路径" : "最近搜索文件名");
        title->setStyleSheet("color: #888; font-weight: bold; font-size: 11px;");
        top->addWidget(title); top->addStretch();
        auto* clearBtn = new QPushButton("清空"); clearBtn->setCursor(Qt::PointingHandCursor);
        clearBtn->setStyleSheet("QPushButton { background: transparent; color: #666; font-size: 11px; } QPushButton:hover { color: #E74C3C; }");
        connect(clearBtn, &QPushButton::clicked, [this](){ if (m_type == Path) m_widget->clearHistory(); else m_widget->clearSearchHistory(); refreshUI(); });
        top->addWidget(clearBtn); layout->addLayout(top);
        auto* scroll = new QScrollArea(); scroll->setWidgetResizable(true);
        scroll->setStyleSheet("QScrollArea { background-color: transparent; border: none; }");
        m_chipsWidget = new QWidget(); m_vLayout = new QVBoxLayout(m_chipsWidget);
        m_vLayout->setContentsMargins(0, 0, 0, 0); m_vLayout->setSpacing(2); m_vLayout->addStretch();
        scroll->setWidget(m_chipsWidget); layout->addWidget(scroll);
        m_opacityAnim = new QPropertyAnimation(this, "windowOpacity"); m_opacityAnim->setDuration(200);
    }
    void refreshUI() {
        QLayoutItem* item; while ((item = m_vLayout->takeAt(0))) { if(item->widget()) item->widget()->deleteLater(); delete item; }
        m_vLayout->addStretch();
        QStringList history = (m_type == Path) ? m_widget->getHistory() : m_widget->getSearchHistory();
        if(history.isEmpty()) {
            auto* lbl = new QLabel("暂无历史记录"); lbl->setAlignment(Qt::AlignCenter);
            lbl->setStyleSheet("color: #555; font-style: italic; margin: 20px; border: none;");
            m_vLayout->insertWidget(0, lbl);
        } else {
            for(const QString& val : history) {
                auto* chip = new PathChip(val); chip->setFixedHeight(32);
                connect(chip, &PathChip::clicked, this, [this](const QString& v){ if (m_type == Path) m_widget->useHistoryPath(v); else m_edit->setText(v); close(); });
                connect(chip, &PathChip::deleted, this, [this](const QString& v){ if (m_type == Path) m_widget->removeHistoryEntry(v); else m_widget->removeSearchHistoryEntry(v); refreshUI(); });
                m_vLayout->insertWidget(m_vLayout->count() - 1, chip);
            }
        }
        resize(m_edit->width() + 24, 410);
    }
    void showAnimated() {
        refreshUI(); QPoint pos = m_edit->mapToGlobal(QPoint(0, m_edit->height())); move(pos.x() - 12, pos.y() - 7);
        setWindowOpacity(0); show(); m_opacityAnim->setStartValue(0); m_opacityAnim->setEndValue(1); m_opacityAnim->start();
    }
private:
    FileSearchWidget* m_widget; QLineEdit* m_edit; Type m_type; QWidget* m_chipsWidget; QVBoxLayout* m_vLayout; QPropertyAnimation* m_opacityAnim;
};

// ----------------------------------------------------------------------------
// ScannerThread 实现
// ----------------------------------------------------------------------------
ScannerThread::ScannerThread(const QString& folderPath, QObject* parent) : QThread(parent), m_folderPath(folderPath) {}
void ScannerThread::stop() { m_isRunning = false; wait(); }
void ScannerThread::run() {
    int count = 0; if (m_folderPath.isEmpty() || !QDir(m_folderPath).exists()) { emit finished(0); return; }
    QStringList ignored = {".git", ".idea", "__pycache__", "node_modules", "$RECYCLE.BIN", "System Volume Information"};
    std::function<void(const QString&)> scanDir = [&](const QString& currentPath) {
        if (!m_isRunning) return;
        QDir dir(currentPath); QFileInfoList files = dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden);
        for (const auto& fi : files) {
            if (!m_isRunning) return;
            bool hidden = fi.isHidden() || fi.fileName().startsWith('.');
            emit fileFound(fi.fileName(), fi.absoluteFilePath(), hidden); count++;
        }
        QFileInfoList subDirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
        for (const auto& di : subDirs) { if (!m_isRunning) return; if (!ignored.contains(di.fileName())) scanDir(di.absoluteFilePath()); }
    };
    scanDir(m_folderPath); emit finished(count);
}

// ----------------------------------------------------------------------------
// FileSearchWidget 实现
// ----------------------------------------------------------------------------
FileSearchWidget::FileSearchWidget(QWidget* parent) : QWidget(parent) {
    setupStyles();
    initUI();
}
FileSearchWidget::~FileSearchWidget() { if (m_scanThread) { m_scanThread->stop(); m_scanThread->deleteLater(); } }

void FileSearchWidget::setupStyles() {
    setStyleSheet(R"(
        QWidget { font-family: "Microsoft YaHei", sans-serif; font-size: 13px; color: #E0E0E0; outline: none; }
        QListWidget { background-color: #252526; border: 1px solid #333; border-radius: 4px; padding: 2px; }
        QListWidget::item { height: 28px; padding-left: 8px; border-radius: 4px; color: #CCC; }
        QListWidget::item:selected { background-color: #37373D; border-left: 3px solid #007ACC; color: #FFF; }
        QListWidget::item:hover { background-color: #2A2D2E; }
        QLineEdit { background-color: #252526; border: 1px solid #333; color: #FFF; border-radius: 4px; padding: 6px; }
        QLineEdit:focus { border: 1px solid #007ACC; }
    )");
}

void FileSearchWidget::initUI() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6); // 1:1复刻 紧凑布局

    auto* pathLayout = new QHBoxLayout();
    m_pathInput = new QLineEdit();
    m_pathInput->setPlaceholderText("双击查看历史，或在此粘贴路径...");
    m_pathInput->installEventFilter(this);
    connect(m_pathInput, &QLineEdit::returnPressed, this, &FileSearchWidget::onPathReturnPressed);

    auto* btnScan = new QToolButton();
    btnScan->setIcon(IconHelper::getIcon("scan", "#1abc9c", 18));
    btnScan->setFixedSize(34, 34);
    btnScan->setCursor(Qt::PointingHandCursor);
    btnScan->setStyleSheet("QToolButton { border: 1px solid #333; background: #2D2D30; border-radius: 4px; } QToolButton:hover { border-color: #007ACC; }");
    connect(btnScan, &QToolButton::clicked, this, &FileSearchWidget::onPathReturnPressed);

    auto* btnBrowse = new QToolButton();
    btnBrowse->setIcon(IconHelper::getIcon("folder", "#FFFFFF", 18));
    btnBrowse->setFixedSize(34, 34);
    btnBrowse->setCursor(Qt::PointingHandCursor);
    btnBrowse->setStyleSheet("QToolButton { background: #007ACC; border: none; border-radius: 4px; } QToolButton:hover { background: #0098FF; }");
    connect(btnBrowse, &QToolButton::clicked, this, &FileSearchWidget::selectFolder);

    pathLayout->addWidget(m_pathInput);
    pathLayout->addWidget(btnScan);
    pathLayout->addWidget(btnBrowse);
    layout->addLayout(pathLayout);

    auto* searchLayout = new QHBoxLayout();
    m_searchInput = new QLineEdit();
    m_searchInput->setPlaceholderText("输入文件名过滤...");
    m_searchInput->installEventFilter(this);
    connect(m_searchInput, &QLineEdit::textChanged, this, &FileSearchWidget::refreshList);

    m_extInput = new QLineEdit();
    m_extInput->setPlaceholderText("后缀 (如 py)");
    m_extInput->setFixedWidth(100);
    connect(m_extInput, &QLineEdit::textChanged, this, &FileSearchWidget::refreshList);

    searchLayout->addWidget(m_searchInput);
    searchLayout->addWidget(m_extInput);
    layout->addLayout(searchLayout);

    auto* infoLayout = new QHBoxLayout();
    m_infoLabel = new QLabel("等待操作...");
    m_infoLabel->setStyleSheet("color: #888; font-size: 11px;");
    m_showHiddenCheck = new QCheckBox("显示隐性文件");
    m_showHiddenCheck->setStyleSheet("QCheckBox { color: #888; font-size: 11px; }");
    connect(m_showHiddenCheck, &QCheckBox::toggled, this, &FileSearchWidget::refreshList);
    infoLayout->addWidget(m_infoLabel);
    infoLayout->addWidget(m_showHiddenCheck);
    infoLayout->addStretch();
    layout->addLayout(infoLayout);

    auto* listHeaderLayout = new QHBoxLayout();
    auto* listTitle = new QLabel("搜索结果");
    listTitle->setStyleSheet("color: #888; font-size: 11px; font-weight: bold;");
    auto* btnCopyAll = new QToolButton();
    btnCopyAll->setIcon(IconHelper::getIcon("copy", "#1abc9c", 14));
    btnCopyAll->setFixedSize(20, 20);
    btnCopyAll->setCursor(Qt::PointingHandCursor);
    btnCopyAll->setStyleSheet("QToolButton { border: none; background: transparent; } QToolButton:hover { background-color: #3E3E42; border-radius: 4px; }");
    connect(btnCopyAll, &QToolButton::clicked, this, [this](){
        QStringList paths; for (int i = 0; i < m_fileList->count(); ++i) { QString p = m_fileList->item(i)->data(Qt::UserRole).toString(); if (!p.isEmpty()) paths << p; }
        if (!paths.isEmpty()) QApplication::clipboard()->setText(paths.join("\n"));
    });
    listHeaderLayout->addWidget(listTitle);
    listHeaderLayout->addStretch();
    listHeaderLayout->addWidget(btnCopyAll);
    layout->addLayout(listHeaderLayout);

    m_fileList = new QListWidget();
    m_fileList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_fileList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_fileList->setDragEnabled(true);
    connect(m_fileList, &QListWidget::customContextMenuRequested, this, &FileSearchWidget::showFileContextMenu);
    layout->addWidget(m_fileList);

    m_actionSelectAll = new QAction(this); connect(m_actionSelectAll, &QAction::triggered, [this](){ m_fileList->selectAll(); }); m_fileList->addAction(m_actionSelectAll);
    m_actionCopy = new QAction(this); connect(m_actionCopy, &QAction::triggered, this, &FileSearchWidget::copySelectedFiles); m_fileList->addAction(m_actionCopy);
    m_actionDelete = new QAction(this); connect(m_actionDelete, &QAction::triggered, this, &FileSearchWidget::onDeleteFile); m_fileList->addAction(m_actionDelete);
    m_actionScan = new QAction(this); connect(m_actionScan, &QAction::triggered, this, &FileSearchWidget::onPathReturnPressed); addAction(m_actionScan);
    updateShortcuts();
}

void FileSearchWidget::selectFolder() { QString d = QFileDialog::getExistingDirectory(this, "选择文件夹"); if (!d.isEmpty()) setPath(d); }
void FileSearchWidget::onPathReturnPressed() { QString p = m_pathInput->text().trimmed(); if (QDir(p).exists()) startScan(p); else m_infoLabel->setText("路径不存在"); }
void FileSearchWidget::startScan(const QString& path) {
    if (m_scanThread) { m_scanThread->stop(); m_scanThread->deleteLater(); }
    m_fileList->clear(); m_filesData.clear(); m_visibleCount = 0; m_hiddenCount = 0;
    m_infoLabel->setText("正在扫描: " + path);
    m_scanThread = new ScannerThread(path, this);
    connect(m_scanThread, &ScannerThread::fileFound, this, &FileSearchWidget::onFileFound);
    connect(m_scanThread, &ScannerThread::finished, this, &FileSearchWidget::onScanFinished);
    m_scanThread->start();
}
void FileSearchWidget::onFileFound(const QString& name, const QString& path, bool isHidden) {
    m_filesData.append({name, path, isHidden});
    if (isHidden) m_hiddenCount++; else m_visibleCount++;
}
void FileSearchWidget::onScanFinished(int count) {
    m_infoLabel->setText(QString("扫描结束，共 %1 个文件").arg(count));
    addHistoryEntry(m_pathInput->text().trimmed());
    std::sort(m_filesData.begin(), m_filesData.end(), [](const FileData& a, const FileData& b){ return a.name.localeAwareCompare(b.name) < 0; });
    refreshList();
}
void FileSearchWidget::refreshList() {
    m_fileList->clear(); QString txt = m_searchInput->text().toLower(); QString ext = m_extInput->text().toLower().trimmed();
    if (ext.startsWith(".")) ext = ext.mid(1);
    bool showHidden = m_showHiddenCheck->isChecked();
    int shown = 0;
    for (const auto& data : m_filesData) {
        if (!showHidden && data.isHidden) continue;
        if (!ext.isEmpty() && !data.name.toLower().endsWith("." + ext)) continue;
        if (!txt.isEmpty() && !data.name.toLower().contains(txt)) continue;
        auto* item = new QListWidgetItem(data.name); item->setData(Qt::UserRole, data.path); item->setToolTip(data.path);
        m_fileList->addItem(item); if (++shown >= 1000) break;
    }
}
void FileSearchWidget::showFileContextMenu(const QPoint& pos) {
    auto selectedItems = m_fileList->selectedItems(); if (selectedItems.isEmpty()) return;
    QStringList paths; for (auto* it : selectedItems) paths << it->data(Qt::UserRole).toString();
    QMenu menu(this); IconHelper::setupMenu(&menu);
    if (paths.size() == 1) {
        QString fp = paths[0];
        menu.addAction(IconHelper::getIcon("folder", "#F1C40F"), "定位文件夹", [fp](){ QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(fp).absolutePath())); });
        menu.addAction(IconHelper::getIcon("edit", "#3498DB"), "编辑", [this](){ onEditFile(); });
        menu.addSeparator();
    }
    menu.addAction(IconHelper::getIcon("copy", "#2ECC71"), "复制路径", [paths](){ QApplication::clipboard()->setText(paths.join("\n")); });
    menu.addAction(IconHelper::getIcon("star", "#F1C40F"), "加入收藏", [this, paths](){ auto* win = qobject_cast<SearchAppWindow*>(window()); if (win) win->addCollectionItems(paths); });
    menu.addAction(IconHelper::getIcon("merge", "#3498DB"), "合并选中", [this](){ onMergeSelectedFiles(); });
    menu.exec(m_fileList->mapToGlobal(pos));
}
void FileSearchWidget::onEditFile() {
    auto items = m_fileList->selectedItems(); if (items.isEmpty()) return;
    QSettings settings("RapidNotes", "ExternalEditor"); QString editor = settings.value("EditorPath").toString();
    if (editor.isEmpty() || !QFile::exists(editor)) { editor = QFileDialog::getOpenFileName(this, "选择编辑器"); if (editor.isEmpty()) return; settings.setValue("EditorPath", editor); }
    for (auto* it : items) QProcess::startDetached(editor, { QDir::toNativeSeparators(it->data(Qt::UserRole).toString()) });
}
void FileSearchWidget::copySelectedFiles() {
    QList<QUrl> urls; for (auto* it : m_fileList->selectedItems()) urls << QUrl::fromLocalFile(it->data(Qt::UserRole).toString());
    QMimeData* md = new QMimeData(); md->setUrls(urls); QApplication::clipboard()->setMimeData(md);
}
void FileSearchWidget::onDeleteFile() {
    for (auto* it : m_fileList->selectedItems()) { QString fp = it->data(Qt::UserRole).toString(); if (QFile::moveToTrash(fp)) delete it; }
}
void FileSearchWidget::onMergeFiles(const QStringList& filePaths, const QString& rootPath, bool useCombineDir) {
    if (filePaths.isEmpty()) return;
    QString target = (useCombineDir || rootPath.isEmpty()) ? QCoreApplication::applicationDirPath() + "/Combine" : rootPath;
    QDir().mkpath(target);
    QString outPath = QDir(target).filePath(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + "_export.md");
    QFile out(outPath); if (!out.open(QIODevice::WriteOnly)) return;
    QTextStream ts(&out); ts << "# 导出结果\n\n";
    for (const QString& fp : filePaths) { ts << "## " << fp << "\n```\n"; QFile f(fp); if (f.open(QIODevice::ReadOnly)) ts << f.readAll(); ts << "\n```\n\n"; }
    ToolTipOverlay::instance()->showText(QCursor::pos(), "✔ 已导出");
}
void FileSearchWidget::onMergeSelectedFiles() {
    QStringList paths; for (auto* it : m_fileList->selectedItems()) { QString p = it->data(Qt::UserRole).toString(); if (isSupportedFile(p)) paths << p; }
    onMergeFiles(paths, m_pathInput->text().trimmed());
}
void FileSearchWidget::updateShortcuts() {
    auto& sm = ShortcutManager::instance();
    if (m_actionSelectAll) m_actionSelectAll->setShortcut(sm.getShortcut("fs_select_all"));
    if (m_actionCopy) m_actionCopy->setShortcut(sm.getShortcut("fs_copy"));
    if (m_actionDelete) m_actionDelete->setShortcut(sm.getShortcut("fs_delete"));
    if (m_actionScan) m_actionScan->setShortcut(sm.getShortcut("fs_scan"));
}
void FileSearchWidget::setPath(const QString& path) { m_pathInput->setText(path); startScan(path); }
QString FileSearchWidget::getCurrentPath() const { return m_pathInput->text().trimmed(); }
void FileSearchWidget::addHistoryEntry(const QString& p) { if (p.isEmpty()) return; QSettings s("RapidNotes", "FileSearchHistory"); QStringList h = s.value("list").toStringList(); h.removeAll(p); h.prepend(p); while(h.size()>20) h.removeLast(); s.setValue("list", h); }
QStringList FileSearchWidget::getHistory() const { return QSettings("RapidNotes", "FileSearchHistory").value("list").toStringList(); }
void FileSearchWidget::clearHistory() { QSettings("RapidNotes", "FileSearchHistory").setValue("list", QStringList()); }
void FileSearchWidget::removeHistoryEntry(const QString& p) { QSettings s("RapidNotes", "FileSearchHistory"); QStringList h = s.value("list").toStringList(); h.removeAll(p); s.setValue("list", h); }
void FileSearchWidget::useHistoryPath(const QString& p) { setPath(p); }
void FileSearchWidget::addSearchHistoryEntry(const QString& t) { if (t.isEmpty()) return; QSettings s("RapidNotes", "FileSearchFilenameHistory"); QStringList h = s.value("list").toStringList(); h.removeAll(t); h.prepend(t); while(h.size()>20) h.removeLast(); s.setValue("list", h); }
QStringList FileSearchWidget::getSearchHistory() const { return QSettings("RapidNotes", "FileSearchFilenameHistory").value("list").toStringList(); }
void FileSearchWidget::removeSearchHistoryEntry(const QString& t) { QSettings s("RapidNotes", "FileSearchFilenameHistory"); QStringList h = s.value("list").toStringList(); h.removeAll(t); s.setValue("list", h); }
void FileSearchWidget::clearSearchHistory() { QSettings("RapidNotes", "FileSearchFilenameHistory").setValue("list", QStringList()); }

bool FileSearchWidget::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::MouseButtonDblClick) {
        if (watched == m_pathInput) { if (!m_pathPopup) m_pathPopup = new FileSearchHistoryPopup(this, m_pathInput, FileSearchHistoryPopup::Path); m_pathPopup->showAnimated(); return true; }
        else if (watched == m_searchInput) { if (!m_searchPopup) m_searchPopup = new FileSearchHistoryPopup(this, m_searchInput, FileSearchHistoryPopup::Filename); m_searchPopup->showAnimated(); return true; }
    }
    return QWidget::eventFilter(watched, event);
}

#include "FileSearchWidget.moc"
