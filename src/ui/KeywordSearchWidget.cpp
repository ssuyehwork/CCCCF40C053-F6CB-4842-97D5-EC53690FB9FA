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

static bool isSupportedFile(const QString& filePath) {
    QFileInfo fi(filePath);
    return SUPPORTED_EXTENSIONS.contains("." + fi.suffix().toLower());
}

class KeywordChip : public QFrame {
    Q_OBJECT
public:
    KeywordChip(const QString& text, QWidget* parent = nullptr) : QFrame(parent), m_text(text) {
        setAttribute(Qt::WA_StyledBackground);
        setCursor(Qt::PointingHandCursor);
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(10, 6, 10, 6); layout->setSpacing(10);
        auto* lbl = new QLabel(text); lbl->setStyleSheet("color: #DDD; font-size: 13px; border:none;");
        layout->addWidget(lbl); layout->addStretch();
        auto* btnDel = new QPushButton(); btnDel->setIcon(IconHelper::getIcon("close", "#666", 16)); btnDel->setFixedSize(16, 16);
        btnDel->setStyleSheet("QPushButton { background: transparent; border-radius: 4px; } QPushButton:hover { background: #E74C3C; }");
        connect(btnDel, &QPushButton::clicked, this, [this](){ emit deleted(m_text); });
        layout->addWidget(btnDel);
        setStyleSheet("#KeywordChip { border-radius: 4px; } #KeywordChip:hover { background-color: #3E3E42; }");
    }
    void mousePressEvent(QMouseEvent* e) override { if(e->button() == Qt::LeftButton) emit clicked(m_text); }
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
        auto* rootLayout = new QVBoxLayout(this); rootLayout->setContentsMargins(12, 12, 12, 12);
        auto* container = new QFrame(); container->setStyleSheet("background-color: #252526; border: 1px solid #444; border-radius: 10px;");
        rootLayout->addWidget(container);
        auto* layout = new QVBoxLayout(container); layout->setContentsMargins(12, 12, 12, 12); layout->setSpacing(10);
        auto* top = new QHBoxLayout();
        auto* icon = new QLabel(); icon->setPixmap(IconHelper::getIcon("clock", "#888").pixmap(14, 14));
        top->addWidget(icon);
        auto* title = new QLabel((m_type == Path) ? "最近路径" : "最近查找/替换"); title->setStyleSheet("color: #888; font-size: 11px;");
        top->addWidget(title); top->addStretch();
        auto* clearBtn = new QPushButton("清空"); clearBtn->setStyleSheet("QPushButton { background: transparent; color: #666; font-size: 11px; }");
        connect(clearBtn, &QPushButton::clicked, [this](){ clear(); refreshUI(); });
        top->addWidget(clearBtn); layout->addLayout(top);
        auto* scroll = new QScrollArea(); scroll->setWidgetResizable(true); scroll->setStyleSheet("QScrollArea { background: transparent; border: none; }");
        m_chipsWidget = new QWidget(); m_vLayout = new QVBoxLayout(m_chipsWidget); m_vLayout->addStretch();
        scroll->setWidget(m_chipsWidget); layout->addWidget(scroll);
        m_opacityAnim = new QPropertyAnimation(this, "windowOpacity"); m_opacityAnim->setDuration(200);
    }
    void clear() { QSettings("RapidNotes", "KeywordSearchHistory").setValue((m_type == Path) ? "pathList" : "keywordList", QStringList()); }
    void refreshUI() {
        QLayoutItem* item; while ((item = m_vLayout->takeAt(0))) { if(item->widget()) item->widget()->deleteLater(); delete item; }
        m_vLayout->addStretch();
        QStringList history = QSettings("RapidNotes", "KeywordSearchHistory").value((m_type == Path) ? "pathList" : "keywordList").toStringList();
        for(const QString& val : history) {
            auto* chip = new KeywordChip(val); chip->setFixedHeight(32);
            connect(chip, &KeywordChip::clicked, this, [this](const QString& v){ m_edit->setText(v); close(); });
            m_vLayout->insertWidget(m_vLayout->count()-1, chip);
        }
        resize(m_edit->width() + 24, 400);
    }
    void showAnimated() {
        refreshUI(); QPoint pos = m_edit->mapToGlobal(QPoint(0, m_edit->height())); move(pos.x() - 12, pos.y() - 7);
        show(); m_opacityAnim->setStartValue(0); m_opacityAnim->setEndValue(1); m_opacityAnim->start();
    }
private:
    KeywordSearchWidget* m_widget; QLineEdit* m_edit; Type m_type; QWidget* m_chipsWidget; QVBoxLayout* m_vLayout; QPropertyAnimation* m_opacityAnim;
};

class KeywordResultItem : public QWidget {
    Q_OBJECT
public:
    KeywordResultItem(const QString& name, const QString& badge, const QColor& badgeColor, QWidget* parent = nullptr) : QWidget(parent) {
        auto* layout = new QHBoxLayout(this); layout->setContentsMargins(10, 0, 10, 0);
        auto* nameLabel = new QLabel(name); nameLabel->setStyleSheet("color: #CCC; font-size: 13px; border:none;");
        layout->addWidget(nameLabel); layout->addStretch();
        auto* badgeLabel = new QLabel(badge); badgeLabel->setStyleSheet(QString("color: %1; font-weight: bold; border:none;").arg(badgeColor.name()));
        layout->addWidget(badgeLabel);
    }
};

KeywordSearchWidget::KeywordSearchWidget(QWidget* parent) : QWidget(parent) {
    m_ignoreDirs = {".git", ".svn", ".idea", ".vscode", "__pycache__", "node_modules", "dist", "build", "venv"};
    setupStyles(); initUI();
}
KeywordSearchWidget::~KeywordSearchWidget() {}

void KeywordSearchWidget::setupStyles() {
    setStyleSheet(R"(
        QWidget { font-family: "Microsoft YaHei", sans-serif; font-size: 13px; color: #E0E0E0; outline: none; }
        QListWidget { background-color: #252526; border: 1px solid #333; border-radius: 4px; padding: 2px; }
        QListWidget::item { height: 28px; padding-left: 8px; border-radius: 4px; }
        QListWidget::item:selected { background-color: #37373D; border-left: 3px solid #007ACC; }
        QLineEdit { background-color: #252526; border: 1px solid #333; color: #FFF; border-radius: 4px; padding: 6px; }
        QLineEdit:focus { border: 1px solid #007ACC; }
    )");
}

void KeywordSearchWidget::initUI() {
    auto* layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(6);

    auto* configGroup = new QWidget();
    auto* configLayout = new QGridLayout(configGroup);
    configLayout->setContentsMargins(0, 0, 0, 0); configLayout->setSpacing(8); configLayout->setColumnStretch(1, 1);

    auto createLabel = [](const QString& text) { auto* lbl = new QLabel(text); lbl->setStyleSheet("color: #AAA; font-weight: bold;"); return lbl; };

    configLayout->addWidget(createLabel("搜索目录:"), 0, 0);
    m_pathEdit = new ClickableLineEdit(); connect(m_pathEdit, &ClickableLineEdit::doubleClicked, this, &KeywordSearchWidget::onShowHistory);
    configLayout->addWidget(m_pathEdit, 0, 1);

    auto* browseBtn = new QPushButton(); browseBtn->setFixedSize(34, 32); browseBtn->setIcon(IconHelper::getIcon("folder", "#EEE", 18));
    browseBtn->setStyleSheet("QPushButton { background: #3E3E42; border-radius: 4px; }");
    connect(browseBtn, &QPushButton::clicked, this, &KeywordSearchWidget::onBrowseFolder);
    configLayout->addWidget(browseBtn, 0, 2);

    configLayout->addWidget(createLabel("文件过滤:"), 1, 0);
    m_filterEdit = new QLineEdit(); m_filterEdit->setPlaceholderText("*.py, *.txt");
    configLayout->addWidget(m_filterEdit, 1, 1, 1, 2);

    configLayout->addWidget(createLabel("查找内容:"), 2, 0);
    m_searchEdit = new ClickableLineEdit(); connect(m_searchEdit, &ClickableLineEdit::doubleClicked, this, &KeywordSearchWidget::onShowHistory);
    configLayout->addWidget(m_searchEdit, 2, 1);

    configLayout->addWidget(createLabel("替换内容:"), 3, 0);
    m_replaceEdit = new ClickableLineEdit(); connect(m_replaceEdit, &ClickableLineEdit::doubleClicked, this, &KeywordSearchWidget::onShowHistory);
    configLayout->addWidget(m_replaceEdit, 3, 1);

    auto* swapBtn = new QPushButton(); swapBtn->setFixedSize(32, 68); swapBtn->setIcon(IconHelper::getIcon("swap", "#AAA", 20));
    swapBtn->setStyleSheet("QPushButton { background: #3E3E42; border-radius: 4px; }");
    connect(swapBtn, &QPushButton::clicked, this, &KeywordSearchWidget::onSwapSearchReplace);
    configLayout->addWidget(swapBtn, 2, 2, 2, 1);

    m_caseCheck = new QCheckBox("区分大小写"); configLayout->addWidget(m_caseCheck, 4, 1);
    layout->addWidget(configGroup);

    auto* btnLayout = new QHBoxLayout();
    auto* searchBtn = new QPushButton(" 智能搜索"); searchBtn->setIcon(IconHelper::getIcon("find_keyword", "#FFF", 16));
    searchBtn->setStyleSheet("QPushButton { background: #007ACC; border-radius: 4px; padding: 6px 15px; color: #FFF; font-weight: bold; }");
    connect(searchBtn, &QPushButton::clicked, this, &KeywordSearchWidget::onSearch);

    auto* replaceBtn = new QPushButton(" 执行替换"); replaceBtn->setIcon(IconHelper::getIcon("edit", "#FFF", 16));
    replaceBtn->setStyleSheet("QPushButton { background: #D32F2F; border-radius: 4px; padding: 6px 15px; color: #FFF; font-weight: bold; }");
    connect(replaceBtn, &QPushButton::clicked, this, &KeywordSearchWidget::onReplace);

    auto* undoBtn = new QPushButton(" 撤销"); undoBtn->setStyleSheet("QPushButton { background: #3E3E42; border-radius: 4px; padding: 6px 15px; }");
    connect(undoBtn, &QPushButton::clicked, this, &KeywordSearchWidget::onUndo);

    btnLayout->addWidget(searchBtn); btnLayout->addWidget(replaceBtn); btnLayout->addWidget(undoBtn);
    btnLayout->addStretch(); layout->addLayout(btnLayout);

    m_resultList = new QListWidget(); m_resultList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_resultList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_resultList, &QListWidget::customContextMenuRequested, this, &KeywordSearchWidget::showResultContextMenu);
    layout->addWidget(m_resultList, 1);

    m_progressBar = new QProgressBar(); m_progressBar->setFixedHeight(4); m_progressBar->setTextVisible(false); m_progressBar->hide();
    m_statusLabel = new QLabel("就绪"); m_statusLabel->setStyleSheet("color: #888; font-size: 11px;");
    layout->addWidget(m_progressBar); layout->addWidget(m_statusLabel);

    m_actionSearch = new QAction(this); connect(m_actionSearch, &QAction::triggered, this, &KeywordSearchWidget::onSearch); addAction(m_actionSearch);
    updateShortcuts();
}

void KeywordSearchWidget::updateShortcuts() {
    auto& sm = ShortcutManager::instance();
    if (m_actionSearch) m_actionSearch->setShortcut(sm.getShortcut("ks_search"));
}
void KeywordSearchWidget::onBrowseFolder() { QString f = QFileDialog::getExistingDirectory(this, "选择目录"); if (!f.isEmpty()) m_pathEdit->setText(f); }
bool KeywordSearchWidget::isTextFile(const QString& fp) { QFile f(fp); if (!f.open(QIODevice::ReadOnly)) return false; QByteArray c = f.read(1024); return !c.contains('\0'); }
void KeywordSearchWidget::onSearch() {
    QString r = m_pathEdit->text().trimmed(); QString k = m_searchEdit->text().trimmed(); if (r.isEmpty() || k.isEmpty()) return;
    addHistoryEntry(Path, r); addHistoryEntry(Keyword, k);
    m_resultList->clear(); m_progressBar->show(); m_progressBar->setRange(0, 0); m_statusLabel->setText("正在搜索...");
    (void)QtConcurrent::run([this, r, k]() {
        struct Tmp { QString p; int c; }; QList<Tmp> ms;
        QDirIterator it(r, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            QString fp = it.next(); if (!isTextFile(fp)) continue;
            QFile f(fp); if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QString content = QString::fromUtf8(f.readAll());
                int c = content.count(k, m_caseCheck->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive);
                if (c > 0) ms.append({fp, c});
            }
        }
        QMetaObject::invokeMethod(this, [this, ms]() {
            for(const auto& m : ms) {
                auto* item = new QListWidgetItem(""); item->setData(Qt::UserRole, m.p);
                m_resultList->addItem(item); m_resultList->setItemWidget(item, new KeywordResultItem(QFileInfo(m.p).fileName(), QString::number(m.c), QColor("#007ACC")));
            }
            m_statusLabel->setText(QString("找到 %1 个匹配").arg(ms.size())); m_progressBar->hide();
        });
    });
}
void KeywordSearchWidget::onReplace() {
    QString r = m_pathEdit->text().trimmed(); QString k = m_searchEdit->text().trimmed(); QString rt = m_replaceEdit->text(); if (r.isEmpty() || k.isEmpty()) return;
    m_progressBar->show(); m_statusLabel->setText("正在替换...");
    (void)QtConcurrent::run([this, r, k, rt]() {
        int mod = 0; QString bd = r + "/_backup_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"); QDir().mkdir(bd); m_lastBackupPath = bd;
        QDirIterator it(r, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            QString fp = it.next(); if (fp.contains("_backup_") || !isTextFile(fp)) continue;
            QFile f(fp); if (f.open(QIODevice::ReadWrite | QIODevice::Text)) {
                QString c = QString::fromUtf8(f.readAll()); if (c.contains(k)) {
                    QFile::copy(fp, bd + "/" + QFileInfo(fp).fileName() + ".bak");
                    f.resize(0); f.write(c.replace(k, rt).toUtf8()); mod++;
                }
            }
        }
        QMetaObject::invokeMethod(this, [this, mod]() { m_statusLabel->setText(QString("修改了 %1 个文件").arg(mod)); m_progressBar->hide(); ToolTipOverlay::instance()->showText(QCursor::pos(), "✔ 替换完成"); });
    });
}
void KeywordSearchWidget::onUndo() {
    if (m_lastBackupPath.isEmpty()) return;
    QDir d(m_lastBackupPath);
    for (const QString& b : d.entryList({"*.bak"})) {
        QString o = b.left(b.length()-4); QDirIterator it(m_pathEdit->text(), {o}, QDir::Files, QDirIterator::Subdirectories);
        if (it.hasNext()) { QString t = it.next(); QFile::remove(t); QFile::copy(d.absoluteFilePath(b), t); }
    }
    ToolTipOverlay::instance()->showText(QCursor::pos(), "✔ 撤销完成");
}
void KeywordSearchWidget::showResultContextMenu(const QPoint& pos) {
    auto items = m_resultList->selectedItems(); if (items.isEmpty()) return;
    QStringList paths; for (auto* it : items) paths << it->data(Qt::UserRole).toString();
    QMenu menu(this); IconHelper::setupMenu(&menu);
    menu.addAction(IconHelper::getIcon("folder", "#F1C40F"), "定位文件夹", [paths](){ QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(paths[0]).absolutePath())); });
    menu.addAction(IconHelper::getIcon("star", "#F1C40F"), "加入收藏", [this, paths](){ auto* win = qobject_cast<SearchAppWindow*>(window()); if (win) win->addCollectionItems(paths); });
    menu.addAction(IconHelper::getIcon("merge", "#3498DB"), "合并选中", [this](){ onMergeSelectedFiles(); });
    menu.exec(m_resultList->mapToGlobal(pos));
}
void KeywordSearchWidget::onMergeFiles(const QStringList& filePaths, const QString& rootPath, bool useCombineDir) {
    if (filePaths.isEmpty()) return;
    QString target = (useCombineDir || rootPath.isEmpty()) ? QCoreApplication::applicationDirPath() + "/Combine" : rootPath;
    QDir().mkpath(target);
    QString outPath = QDir(target).filePath(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + "_export.md");
    QFile out(outPath); if (!out.open(QIODevice::WriteOnly)) return;
    QTextStream ts(&out); ts << "# 导出结果\n\n";
    for (const QString& fp : filePaths) { ts << "## " << fp << "\n```\n"; QFile f(fp); if (f.open(QIODevice::ReadOnly)) ts << f.readAll(); ts << "\n```\n\n"; }
    ToolTipOverlay::instance()->showText(QCursor::pos(), "✔ 已导出");
}
void KeywordSearchWidget::onMergeSelectedFiles() {
    QStringList paths; for (auto* it : m_resultList->selectedItems()) { QString p = it->data(Qt::UserRole).toString(); if (isSupportedFile(p)) paths << p; }
    onMergeFiles(paths, m_pathEdit->text().trimmed());
}
void KeywordSearchWidget::setPath(const QString& path) { m_pathEdit->setText(path); }
QString KeywordSearchWidget::getCurrentPath() const { return m_pathEdit->text().trimmed(); }
void KeywordSearchWidget::addHistoryEntry(HistoryType t, const QString& txt) {
    QString k = (t == Path) ? "pathList" : "keywordList"; QSettings s("RapidNotes", "KeywordSearchHistory");
    QStringList h = s.value(k).toStringList(); h.removeAll(txt); h.prepend(txt); while(h.size()>10) h.removeLast(); s.setValue(k, h);
}
void KeywordSearchWidget::onShowHistory() {
    auto* edit = qobject_cast<ClickableLineEdit*>(sender()); if (!edit) return;
    auto* popup = new KeywordSearchHistoryPopup(this, edit, (edit == m_pathEdit) ? KeywordSearchHistoryPopup::Path : KeywordSearchHistoryPopup::Keyword);
    popup->setAttribute(Qt::WA_DeleteOnClose); popup->showAnimated();
}
void KeywordSearchWidget::onSwapSearchReplace() { QString s = m_searchEdit->text(); m_searchEdit->setText(m_replaceEdit->text()); m_replaceEdit->setText(s); }
void KeywordSearchWidget::onClearLog() { m_resultList->clear(); m_statusLabel->setText("就绪"); }

#include "KeywordSearchWidget.moc"
