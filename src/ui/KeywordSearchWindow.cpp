#include "ToolTipOverlay.h"
#include "KeywordSearchWindow.h"
#include "UnifiedSearchWindow.h"
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
// 合并逻辑相关常量与辅助函数 (从 UnifiedSearchWindow 复用逻辑)
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
// KeywordSearchHistory 相关辅助类
// ----------------------------------------------------------------------------
class KeywordChip : public QFrame {
    Q_OBJECT
public:
    KeywordChip(const QString& text, QWidget* parent = nullptr) : QFrame(parent), m_text(text) {
        setAttribute(Qt::WA_StyledBackground); setCursor(Qt::PointingHandCursor); setObjectName("KeywordChip");
        auto* layout = new QHBoxLayout(this); layout->setContentsMargins(10, 6, 10, 6); layout->setSpacing(10);
        auto* lbl = new QLabel(text); lbl->setStyleSheet("border: none; background: transparent; color: #DDD; font-size: 13px;");
        layout->addWidget(lbl); layout->addStretch();
        auto* btnDel = new QPushButton(); btnDel->setIcon(IconHelper::getIcon("close", "#666", 16)); btnDel->setIconSize(QSize(10, 10)); btnDel->setFixedSize(16, 16);
        btnDel->setStyleSheet("QPushButton { background-color: transparent; border-radius: 4px; padding: 0px; } QPushButton:hover { background-color: #E74C3C; }");
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
        m_widget = widget; m_edit = edit; m_type = type; setAttribute(Qt::WA_TranslucentBackground);
        auto* rootLayout = new QVBoxLayout(this); rootLayout->setContentsMargins(12, 12, 12, 12);
        auto* container = new QFrame(); container->setObjectName("PopupContainer");
        container->setStyleSheet("#PopupContainer { background-color: #252526; border: 1px solid #444; border-radius: 10px; }");
        rootLayout->addWidget(container);
        auto* layout = new QVBoxLayout(container); layout->setContentsMargins(12, 12, 12, 12); layout->setSpacing(10);
        auto* top = new QHBoxLayout(); auto* icon = new QLabel(); icon->setPixmap(IconHelper::getIcon("clock", "#888").pixmap(14, 14)); top->addWidget(icon);
        QString titleStr = "最近记录"; if (m_type == Path) titleStr = "最近扫描路径"; else if (m_type == Keyword) titleStr = "最近查找内容"; else if (m_type == Replace) titleStr = "最近替换内容";
        auto* title = new QLabel(titleStr); title->setStyleSheet("color: #888; font-weight: bold; font-size: 11px;"); top->addWidget(title); top->addStretch();
        auto* clearBtn = new QPushButton("清空"); clearBtn->setStyleSheet("QPushButton { background: transparent; color: #666; border: none; font-size: 11px; }");
        connect(clearBtn, &QPushButton::clicked, [this](){ clearAllHistory(); refreshUI(); });
        top->addWidget(clearBtn); layout->addLayout(top);
        auto* scroll = new QScrollArea(); scroll->setWidgetResizable(true); scroll->setStyleSheet("QScrollArea { background-color: transparent; border: none; }");
        m_chipsWidget = new QWidget(); m_vLayout = new QVBoxLayout(m_chipsWidget); m_vLayout->setContentsMargins(0, 0, 0, 0); m_vLayout->setSpacing(2); m_vLayout->addStretch();
        scroll->setWidget(m_chipsWidget); layout->addWidget(scroll);
        m_opacityAnim = new QPropertyAnimation(this, "windowOpacity"); m_opacityAnim->setDuration(200);
    }
    void clearAllHistory() {
        QString key = (m_type == Path) ? "pathList" : (m_type == Replace ? "replaceList" : "keywordList");
        QSettings settings("RapidNotes", "KeywordSearchHistory"); settings.setValue(key, QStringList());
    }
    void removeEntry(const QString& text) {
        QString key = (m_type == Path) ? "pathList" : (m_type == Replace ? "replaceList" : "keywordList");
        QSettings settings("RapidNotes", "KeywordSearchHistory"); QStringList history = settings.value(key).toStringList(); history.removeAll(text); settings.setValue(key, history);
    }
    QStringList getHistory() const {
        QString key = (m_type == Path) ? "pathList" : (m_type == Replace ? "replaceList" : "keywordList");
        QSettings settings("RapidNotes", "KeywordSearchHistory"); return settings.value(key).toStringList();
    }
    void refreshUI() {
        QLayoutItem* item; while ((item = m_vLayout->takeAt(0))) { if(item->widget()) item->widget()->deleteLater(); delete item; } m_vLayout->addStretch();
        QStringList history = getHistory();
        if(history.isEmpty()) { auto* lbl = new QLabel("暂无历史记录"); lbl->setAlignment(Qt::AlignCenter); lbl->setStyleSheet("color: #555; font-style: italic; margin: 20px;"); m_vLayout->insertWidget(0, lbl); }
        else { for(const QString& val : history) { auto* chip = new KeywordChip(val); chip->setFixedHeight(32); connect(chip, &KeywordChip::clicked, this, [this](const QString& v){ m_edit->setText(v); close(); });
            connect(chip, &KeywordChip::deleted, this, [this](const QString& v){ removeEntry(v); refreshUI(); }); m_vLayout->insertWidget(m_vLayout->count() - 1, chip); } }
        resize(m_edit->width() + 24, qMin(410, (int)history.size() * 34 + 60));
    }
    void showAnimated() { refreshUI(); QPoint pos = m_edit->mapToGlobal(QPoint(0, m_edit->height())); move(pos.x() - 12, pos.y() - 7); setWindowOpacity(0); show(); m_opacityAnim->setStartValue(0); m_opacityAnim->setEndValue(1); m_opacityAnim->start(); }
private:
    KeywordSearchWidget* m_widget; QLineEdit* m_edit; Type m_type; QWidget* m_chipsWidget; QVBoxLayout* m_vLayout; QPropertyAnimation* m_opacityAnim;
};

// ----------------------------------------------------------------------------
// KeywordResultItem
// ----------------------------------------------------------------------------
class KeywordResultItem : public QWidget {
    Q_OBJECT
public:
    KeywordResultItem(const QString& name, const QString& badge, const QColor& badgeColor, QWidget* parent = nullptr) : QWidget(parent) {
        auto* layout = new QHBoxLayout(this); layout->setContentsMargins(10, 0, 10, 0); layout->setSpacing(10);
        auto* nameLabel = new QLabel(name); nameLabel->setStyleSheet("color: #CCC; font-size: 13px;"); layout->addWidget(nameLabel); layout->addStretch();
        auto* badgeLabel = new QLabel(badge); badgeLabel->setFixedWidth(120); badgeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        badgeLabel->setStyleSheet(QString("color: %1; font-size: 12px; font-weight: bold;").arg(badgeColor.name())); layout->addWidget(badgeLabel);
    }
protected:
    void mousePressEvent(QMouseEvent* event) override { event->ignore(); }
};

// ----------------------------------------------------------------------------
// KeywordSearchWidget 实现
// ----------------------------------------------------------------------------
KeywordSearchWidget::KeywordSearchWidget(QWidget* parent) : QWidget(parent) {
    m_ignoreDirs = {".git", ".svn", ".idea", ".vscode", "__pycache__", "node_modules", "dist", "build", "venv"};
    initUI();
}
KeywordSearchWidget::~KeywordSearchWidget() {}
void KeywordSearchWidget::setupStyles() {}
void KeywordSearchWidget::initUI() {
    auto* midLayout = new QVBoxLayout(this); midLayout->setContentsMargins(0, 0, 0, 0); midLayout->setSpacing(15);
    auto* configGroup = new QWidget(); auto* configLayout = new QGridLayout(configGroup); configLayout->setContentsMargins(0, 0, 0, 0);
    configLayout->setHorizontalSpacing(10); configLayout->setVerticalSpacing(10);
    auto createLabel = [](const QString& text) { auto* lbl = new QLabel(text); lbl->setStyleSheet("color: #AAA; font-weight: bold;"); return lbl; };
    auto setEditStyle = [](QLineEdit* edit) { edit->setClearButtonEnabled(true); edit->setStyleSheet("QLineEdit { background: #252526; border: 1px solid #333; border-radius: 4px; padding: 6px; color: #EEE; }"); };
    configLayout->addWidget(createLabel("搜索目录:"), 0, 0);
    m_pathEdit = new ClickableLineEdit(); m_pathEdit->setPlaceholderText("选择搜索根目录 (双击查看历史)..."); setEditStyle(m_pathEdit);
    connect(m_pathEdit, &QLineEdit::returnPressed, this, &KeywordSearchWidget::onSearch);
    connect(m_pathEdit, &ClickableLineEdit::doubleClicked, this, &KeywordSearchWidget::onShowHistory);
    configLayout->addWidget(m_pathEdit, 0, 1);
    auto* browseBtn = new QPushButton(); browseBtn->setFixedSize(38, 32); browseBtn->setIcon(IconHelper::getIcon("folder", "#EEE", 18));
    connect(browseBtn, &QPushButton::clicked, this, &KeywordSearchWidget::onBrowseFolder);
    configLayout->addWidget(browseBtn, 0, 2);
    configLayout->addWidget(createLabel("文件过滤:"), 1, 0);
    m_filterEdit = new QLineEdit(); m_filterEdit->setPlaceholderText("例如: *.py, *.txt"); setEditStyle(m_filterEdit);
    connect(m_filterEdit, &QLineEdit::returnPressed, this, &KeywordSearchWidget::onSearch);
    configLayout->addWidget(m_filterEdit, 1, 1, 1, 2);
    configLayout->addWidget(createLabel("查找内容:"), 2, 0);
    m_searchEdit = new ClickableLineEdit(); m_searchEdit->setPlaceholderText("输入要查找的内容..."); setEditStyle(m_searchEdit);
    connect(m_searchEdit, &QLineEdit::returnPressed, this, &KeywordSearchWidget::onSearch);
    connect(m_searchEdit, &ClickableLineEdit::doubleClicked, this, &KeywordSearchWidget::onShowHistory);
    configLayout->addWidget(m_searchEdit, 2, 1);
    configLayout->addWidget(createLabel("替换内容:"), 3, 0);
    m_replaceEdit = new ClickableLineEdit(); m_replaceEdit->setPlaceholderText("替换为..."); setEditStyle(m_replaceEdit);
    connect(m_replaceEdit, &QLineEdit::returnPressed, this, &KeywordSearchWidget::onSearch);
    connect(m_replaceEdit, &ClickableLineEdit::doubleClicked, this, &KeywordSearchWidget::onShowHistory);
    configLayout->addWidget(m_replaceEdit, 3, 1);
    auto* swapBtn = new QPushButton(); swapBtn->setFixedSize(32, 74); swapBtn->setIcon(IconHelper::getIcon("swap", "#AAA", 20));
    connect(swapBtn, &QPushButton::clicked, this, &KeywordSearchWidget::onSwapSearchReplace);
    configLayout->addWidget(swapBtn, 2, 2, 2, 1);
    m_caseCheck = new QCheckBox("区分大小写"); configLayout->addWidget(m_caseCheck, 4, 1, 1, 2);
    midLayout->addWidget(configGroup);
    auto* btnLayout = new QHBoxLayout();
    auto* searchBtn = new QPushButton(" 智能搜索"); searchBtn->setIcon(IconHelper::getIcon("find_keyword", "#FFF", 16));
    searchBtn->setStyleSheet("QPushButton { background: #007ACC; border-radius: 4px; padding: 8px 20px; color: #FFF; }");
    connect(searchBtn, &QPushButton::clicked, this, &KeywordSearchWidget::onSearch);
    auto* replaceBtn = new QPushButton(" 执行替换"); replaceBtn->setIcon(IconHelper::getIcon("edit", "#FFF", 16));
    replaceBtn->setStyleSheet("QPushButton { background: #D32F2F; border-radius: 4px; padding: 8px 20px; color: #FFF; }");
    connect(replaceBtn, &QPushButton::clicked, this, &KeywordSearchWidget::onReplace);
    auto* undoBtn = new QPushButton(" 撤销替换"); undoBtn->setIcon(IconHelper::getIcon("undo", "#EEE", 16));
    connect(undoBtn, &QPushButton::clicked, this, &KeywordSearchWidget::onUndo);
    auto* clearBtn = new QPushButton(" 清空日志"); clearBtn->setIcon(IconHelper::getIcon("trash", "#EEE", 16));
    connect(clearBtn, &QPushButton::clicked, this, &KeywordSearchWidget::onClearLog);
    btnLayout->addWidget(searchBtn); btnLayout->addWidget(replaceBtn); btnLayout->addWidget(undoBtn); btnLayout->addWidget(clearBtn); btnLayout->addStretch();
    midLayout->addLayout(btnLayout);
    m_resultList = new QListWidget(); m_resultList->setSelectionMode(QAbstractItemView::ExtendedSelection); m_resultList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_resultList->setDragEnabled(true); m_resultList->setDragDropMode(QAbstractItemView::DragOnly);
    connect(m_resultList, &QListWidget::customContextMenuRequested, this, &KeywordSearchWidget::showResultContextMenu);
    midLayout->addWidget(m_resultList, 1);
    auto* statusLayout = new QVBoxLayout(); m_progressBar = new QProgressBar(); m_progressBar->setFixedHeight(4); m_progressBar->setTextVisible(false); m_progressBar->hide();
    m_statusLabel = new QLabel("就绪"); m_statusLabel->setStyleSheet("color: #888; font-size: 11px;");
    statusLayout->addWidget(m_progressBar); statusLayout->addWidget(m_statusLabel);
    midLayout->addLayout(statusLayout);

    m_actionSearch = new QAction(this);
    connect(m_actionSearch, &QAction::triggered, this, &KeywordSearchWidget::onSearch);
    addAction(m_actionSearch);

    m_actionReplace = new QAction(this);
    connect(m_actionReplace, &QAction::triggered, this, &KeywordSearchWidget::onReplace);
    addAction(m_actionReplace);

    updateShortcuts();
}

void KeywordSearchWidget::onBrowseFolder() { QString f = QFileDialog::getExistingDirectory(this, "选择搜索目录"); if (!f.isEmpty()) m_pathEdit->setText(f); }
bool KeywordSearchWidget::isTextFile(const QString& filePath) { QFile file(filePath); if (!file.open(QIODevice::ReadOnly)) return false; QByteArray chunk = file.read(1024); file.close(); if (chunk.isEmpty()) return true; if (chunk.contains('\0')) return false; return true; }
void KeywordSearchWidget::onSearch() {
    QString rootDir = m_pathEdit->text().trimmed(); QString keyword = m_searchEdit->text().trimmed();
    if (rootDir.isEmpty() || keyword.isEmpty()) return;
    addHistoryEntry(Path, rootDir); addHistoryEntry(Keyword, keyword);
    m_resultList->clear(); m_resultsData.clear(); m_progressBar->show(); m_progressBar->setRange(0, 0); m_statusLabel->setText("正在搜索...");
    QString filter = m_filterEdit->text(); bool caseSensitive = m_caseCheck->isChecked();
    (void)QtConcurrent::run([this, rootDir, keyword, filter, caseSensitive]() {
        int scannedFiles = 0; struct TmpMatch { QString path; int count; }; QList<TmpMatch> matches;
        QStringList filters; if (!filter.isEmpty()) filters = filter.split(QRegularExpression("[,\\s;]+"), Qt::SkipEmptyParts);
        QDirIterator it(rootDir, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            QString filePath = it.next(); bool skip = false; for (const QString& ignore : m_ignoreDirs) if (filePath.contains("/" + ignore + "/") || filePath.contains("\\" + ignore + "\\")) { skip = true; break; }
            if (skip) continue;
            if (!filters.isEmpty()) { bool matchFilter = false; QString fileName = QFileInfo(filePath).fileName(); for (const QString& f : filters) { QRegularExpression re(QRegularExpression::wildcardToRegularExpression(f)); if (re.match(fileName).hasMatch()) { matchFilter = true; break; } } if (!matchFilter) continue; }
            if (!isTextFile(filePath)) continue; scannedFiles++;
            QFile file(filePath); if (file.open(QIODevice::ReadOnly | QIODevice::Text)) { QString content = QTextStream(&file).readAll(); file.close(); Qt::CaseSensitivity cs = caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive; if (content.contains(keyword, cs)) matches.append({filePath, static_cast<int>(content.count(keyword, cs))}); }
        }
        QMetaObject::invokeMethod(this, [this, scannedFiles, matches]() {
            for(const auto& m : matches) {
                m_resultsData.append({m.path, m.count}); auto* item = new QListWidgetItem(""); item->setData(Qt::UserRole, m.path); item->setToolTip(m.path); m_resultList->addItem(item);
                auto* widget = new KeywordResultItem(QFileInfo(m.path).fileName(), QString::number(m.count), QColor("#007ACC")); m_resultList->setItemWidget(item, widget);
            }
            m_statusLabel->setText(QString("扫描 %1 个文件，找到 %2 个匹配").arg(scannedFiles).arg(matches.size())); m_progressBar->hide();
        });
    });
}
void KeywordSearchWidget::onReplace() {
    QString rootDir = m_pathEdit->text().trimmed(); QString keyword = m_searchEdit->text().trimmed(); QString replaceText = m_replaceEdit->text().trimmed();
    if (rootDir.isEmpty() || keyword.isEmpty()) return;
    addHistoryEntry(Path, rootDir); addHistoryEntry(Keyword, keyword); if (!replaceText.isEmpty()) addHistoryEntry(Replace, replaceText);
    m_resultList->clear(); m_resultsData.clear(); m_progressBar->show(); m_progressBar->setRange(0, 0); m_statusLabel->setText("正在替换...");
    QString filter = m_filterEdit->text(); bool caseSensitive = m_caseCheck->isChecked();
    (void)QtConcurrent::run([this, rootDir, keyword, replaceText, filter, caseSensitive]() {
        int modifiedFiles = 0; QString backupDirName = "_backup_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"); QDir root(rootDir); root.mkdir(backupDirName); m_lastBackupPath = root.absoluteFilePath(backupDirName);
        QStringList filters; if (!filter.isEmpty()) filters = filter.split(QRegularExpression("[,\\s;]+"), Qt::SkipEmptyParts);
        QDirIterator it(rootDir, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            QString filePath = it.next(); if (filePath.contains(backupDirName)) continue;
            bool skip = false; for (const QString& ignore : m_ignoreDirs) if (filePath.contains("/" + ignore + "/") || filePath.contains("\\" + ignore + "\\")) { skip = true; break; }
            if (skip) continue;
            if (!filters.isEmpty()) { bool matchFilter = false; QString fileName = QFileInfo(filePath).fileName(); for (const QString& f : filters) if (QRegularExpression(QRegularExpression::wildcardToRegularExpression(f)).match(fileName).hasMatch()) { matchFilter = true; break; } if (!matchFilter) continue; }
            if (!isTextFile(filePath)) continue;
            QFile file(filePath); if (file.open(QIODevice::ReadWrite | QIODevice::Text)) {
                QString content = QTextStream(&file).readAll(); Qt::CaseSensitivity cs = caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
                if (content.contains(keyword, cs)) {
                    QFile::copy(filePath, m_lastBackupPath + "/" + QFileInfo(filePath).fileName() + ".bak");
                    QString newContent = caseSensitive ? content.replace(keyword, replaceText) : content.replace(QRegularExpression(QRegularExpression::escape(keyword), QRegularExpression::CaseInsensitiveOption), replaceText);
                    file.resize(0); QTextStream(&file) << newContent; modifiedFiles++;
                    QMetaObject::invokeMethod(this, [this, filePath]() {
                        auto* item = new QListWidgetItem(""); item->setData(Qt::UserRole, filePath); item->setToolTip(filePath); m_resultList->addItem(item);
                        auto* widget = new KeywordResultItem(QFileInfo(filePath).fileName(), "已修改", QColor("#6A9955")); m_resultList->setItemWidget(item, widget);
                    });
                }
                file.close();
            }
        }
        QMetaObject::invokeMethod(this, [this, modifiedFiles]() { m_statusLabel->setText(QString("替换完成: 修改了 %1 个文件").arg(modifiedFiles)); m_progressBar->hide(); ToolTipOverlay::instance()->showText(QCursor::pos(), QString("✔ 已修改 %1 个文件").arg(modifiedFiles)); });
    });
}
void KeywordSearchWidget::onUndo() {
    if (m_lastBackupPath.isEmpty() || !QDir(m_lastBackupPath).exists()) return;
    int restored = 0; QDir backupDir(m_lastBackupPath); QStringList baks = backupDir.entryList({"*.bak"}); QString rootDir = m_pathEdit->text(); m_resultList->clear();
    for (const QString& bak : baks) {
        QString origName = bak.left(bak.length() - 4); QDirIterator it(rootDir, {origName}, QDir::Files, QDirIterator::Subdirectories);
        if (it.hasNext()) {
            QString targetPath = it.next(); if (QFile::remove(targetPath)) if (QFile::copy(backupDir.absoluteFilePath(bak), targetPath)) {
                restored++; auto* item = new QListWidgetItem(""); item->setData(Qt::UserRole, targetPath); item->setToolTip(targetPath); m_resultList->addItem(item);
                auto* widget = new KeywordResultItem(origName, "已恢复", QColor("#007ACC")); m_resultList->setItemWidget(item, widget);
            }
        }
    }
    m_statusLabel->setText(QString("撤销完成，已恢复 %1 个文件").arg(restored));
}
void KeywordSearchWidget::onClearLog() { m_resultList->clear(); m_resultsData.clear(); m_statusLabel->setText("就绪"); }
void KeywordSearchWidget::showResultContextMenu(const QPoint& pos) {
    auto selectedItems = m_resultList->selectedItems(); if (selectedItems.isEmpty()) return;
    QStringList paths; for (auto* item : std::as_const(selectedItems)) { QString p = item->data(Qt::UserRole).toString(); if (!p.isEmpty()) paths << p; }
    QMenu menu(this); IconHelper::setupMenu(&menu);
    if (selectedItems.size() == 1) {
        QString filePath = paths.first();
        menu.addAction(IconHelper::getIcon("folder", "#F1C40F"), "定位文件夹", [filePath](){ QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(filePath).absolutePath())); });
        menu.addAction(IconHelper::getIcon("edit", "#3498DB"), "编辑", [this](){ onEditFile(); });
    }
    menu.addAction(IconHelper::getIcon("copy", "#2ECC71"), "复制选中路径", [this](){ copySelectedPaths(); });
    menu.addAction(IconHelper::getIcon("merge", "#3498DB"), "合并选中内容", [this](){ onMergeSelectedFiles(); });
    menu.exec(m_resultList->mapToGlobal(pos));
}
void KeywordSearchWidget::onEditFile() {
    auto selectedItems = m_resultList->selectedItems(); if (selectedItems.isEmpty()) return;
    QStringList paths; for (auto* item : std::as_const(selectedItems)) { QString p = item->data(Qt::UserRole).toString(); if (!p.isEmpty()) paths << p; }
    QSettings s("RapidNotes", "ExternalEditor"); QString editorPath = s.value("EditorPath").toString();
    if (editorPath.isEmpty() || !QFile::exists(editorPath)) {
        for (const QString& p : {"C:/Program Files/Notepad++/notepad++.exe", "C:/Program Files (x86)/Notepad++/notepad++.exe"}) if (QFile::exists(p)) { editorPath = p; break; }
    }
    if (editorPath.isEmpty() || !QFile::exists(editorPath)) { editorPath = QFileDialog::getOpenFileName(this, "选择编辑器", "C:/Program Files", "Executable (*.exe)"); if (editorPath.isEmpty()) return; s.setValue("EditorPath", editorPath); }
    for (const QString& fp : paths) QProcess::startDetached(editorPath, { QDir::toNativeSeparators(fp) });
}
void KeywordSearchWidget::copySelectedPaths() {
    auto selectedItems = m_resultList->selectedItems(); QStringList paths; if (selectedItems.isEmpty()) for (int i = 0; i < m_resultList->count(); ++i) paths << m_resultList->item(i)->data(Qt::UserRole).toString();
    else for (auto* item : selectedItems) paths << item->data(Qt::UserRole).toString();
    if (!paths.isEmpty()) QApplication::clipboard()->setText(paths.join("\n"));
}
void KeywordSearchWidget::copySelectedFiles() {
    auto selectedItems = m_resultList->selectedItems(); if (selectedItems.isEmpty()) return;
    QList<QUrl> urls; QStringList paths; for (auto* item : std::as_const(selectedItems)) { QString p = item->data(Qt::UserRole).toString(); if (!p.isEmpty()) { urls << QUrl::fromLocalFile(p); paths << p; } }
    QMimeData* mimeData = new QMimeData(); mimeData->setUrls(urls); mimeData->setText(paths.join("\n")); QApplication::clipboard()->setMimeData(mimeData);
}
void KeywordSearchWidget::onMergeSelectedFiles() {
    auto* uswin = qobject_cast<UnifiedSearchWindow*>(window()); if(!uswin) return;
    QStringList paths; for (auto* item : m_resultList->selectedItems()) { QString p = item->data(Qt::UserRole).toString(); if (!p.isEmpty() && isSupportedFile(p)) paths << p; }
    if (!paths.isEmpty()) QMetaObject::invokeMethod(uswin, "onMergeFiles", Q_ARG(QStringList, paths), Q_ARG(QString, m_pathEdit->text().trimmed()));
}
void KeywordSearchWidget::onSwapSearchReplace() { QString s = m_searchEdit->text(); m_searchEdit->setText(m_replaceEdit->text()); m_replaceEdit->setText(s); }
void KeywordSearchWidget::addHistoryEntry(HistoryType type, const QString& text) {
    if (text.isEmpty()) return; QString key = (type == Path) ? "pathList" : (type == Replace ? "replaceList" : "keywordList");
    QSettings s("RapidNotes", "KeywordSearchHistory"); QStringList h = s.value(key).toStringList(); h.removeAll(text); h.prepend(text); if(h.size()>10) h.removeLast(); s.setValue(key, h);
}
void KeywordSearchWidget::onShowHistory() {
    auto* edit = qobject_cast<ClickableLineEdit*>(sender()); if (!edit) return;
    KeywordSearchHistoryPopup::Type type = (edit == m_pathEdit) ? KeywordSearchHistoryPopup::Path : (edit == m_replaceEdit ? KeywordSearchHistoryPopup::Replace : KeywordSearchHistoryPopup::Keyword);
    auto* popup = new KeywordSearchHistoryPopup(this, edit, type); popup->setAttribute(Qt::WA_DeleteOnClose); popup->showAnimated();
}
void KeywordSearchWidget::updateShortcuts() {
    auto& sm = ShortcutManager::instance();
    if (m_actionSearch) m_actionSearch->setShortcut(sm.getShortcut("ks_search"));
    if (m_actionReplace) m_actionReplace->setShortcut(sm.getShortcut("ks_replace"));
}

KeywordSearchWindow::KeywordSearchWindow(QWidget* parent) : FramelessDialog("查找关键字", parent) {
    auto* layout = new QVBoxLayout(m_contentArea); layout->setContentsMargins(0,0,0,0);
    m_searchWidget = new KeywordSearchWidget(m_contentArea); layout->addWidget(m_searchWidget);
    m_resizeHandle = new ResizeHandle(this, this);
}
KeywordSearchWindow::~KeywordSearchWindow() {}
void KeywordSearchWindow::hideEvent(QHideEvent* event) { FramelessDialog::hideEvent(event); }
void KeywordSearchWindow::resizeEvent(QResizeEvent* event) { FramelessDialog::resizeEvent(event); if(m_resizeHandle) m_resizeHandle->move(width()-20, height()-20); }

#include "KeywordSearchWindow.moc"
