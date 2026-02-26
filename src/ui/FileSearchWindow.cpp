#include "ToolTipOverlay.h"
#include "FileSearchWindow.h"
#include "StringUtils.h"
#include "../core/ShortcutManager.h"
#include "IconHelper.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QDesktopServices>
#include <QProcess>
#include <QClipboard>
#include <QApplication>
#include <QSettings>
#include <QMenu>
#include <QToolButton>
#include <QMimeData>
#include <QDateTime>
#include <QPropertyAnimation>
#include <QScrollArea>

// 保持 ScannerThread 不变
ScannerThread::ScannerThread(const QString& folderPath, QObject* parent) : QThread(parent), m_folderPath(folderPath) {}
void ScannerThread::stop() { m_isRunning = false; wait(); }
void ScannerThread::run() {
    int count = 0; if (m_folderPath.isEmpty() || !QDir(m_folderPath).exists()) { emit finished(0); return; }
    std::function<void(const QString&)> scanDir = [&](const QString& currentPath) {
        if (!m_isRunning) return;
        QDir dir(currentPath); QFileInfoList files = dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden);
        for (const auto& fi : files) {
            if (!m_isRunning) return;
            bool hidden = fi.isHidden() || fi.fileName().startsWith('.');
            emit fileFound(fi.fileName(), fi.absoluteFilePath(), hidden); count++;
        }
        QFileInfoList subDirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
        for (const auto& di : subDirs) {
            if (!m_isRunning) return;
            if (!QStringList({".git", "node_modules"}).contains(di.fileName())) scanDir(di.absoluteFilePath());
        }
    };
    scanDir(m_folderPath); emit finished(count);
}

// FileSearchWidget 实现
FileSearchWidget::FileSearchWidget(QWidget* parent) : QWidget(parent) {
    setupStyles();
    initUI();
}
FileSearchWidget::~FileSearchWidget() { if (m_scanThread) { m_scanThread->stop(); m_scanThread->deleteLater(); } }

void FileSearchWidget::setupStyles() {
    setStyleSheet(R"(
        QLineEdit { background-color: #333333; border: 1px solid #444444; color: #FFFFFF; border-radius: 6px; padding: 8px; }
        QLineEdit:focus { border: 1px solid #007ACC; }
        QListWidget { background-color: #1E1E1E; border: 1px solid #333; border-radius: 6px; color: #CCC; }
        #ActionBtn { background-color: #007ACC; color: white; border-radius: 6px; }
    )");
}

void FileSearchWidget::initUI() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(10);

    auto* pathLayout = new QHBoxLayout();
    m_pathInput = new QLineEdit(); m_pathInput->setPlaceholderText("在此粘贴路径...");
    connect(m_pathInput, &QLineEdit::returnPressed, this, &FileSearchWidget::onPathReturnPressed);
    auto* btnScan = new QToolButton(); btnScan->setIcon(IconHelper::getIcon("scan", "#1abc9c", 18)); btnScan->setFixedSize(38, 38);
    connect(btnScan, &QToolButton::clicked, this, &FileSearchWidget::onPathReturnPressed);
    auto* btnBrowse = new QToolButton(); btnBrowse->setIcon(IconHelper::getIcon("folder", "#ffffff", 18)); btnBrowse->setFixedSize(38, 38);
    btnBrowse->setObjectName("ActionBtn"); connect(btnBrowse, &QToolButton::clicked, this, &FileSearchWidget::selectFolder);
    pathLayout->addWidget(m_pathInput); pathLayout->addWidget(btnScan); pathLayout->addWidget(btnBrowse);
    layout->addLayout(pathLayout);

    auto* searchLayout = new QHBoxLayout();
    m_searchInput = new QLineEdit(); m_searchInput->setPlaceholderText("输入文件名过滤...");
    connect(m_searchInput, &QLineEdit::textChanged, this, &FileSearchWidget::refreshList);
    m_extInput = new QLineEdit(); m_extInput->setPlaceholderText("后缀"); m_extInput->setFixedWidth(100);
    connect(m_extInput, &QLineEdit::textChanged, this, &FileSearchWidget::refreshList);
    searchLayout->addWidget(m_searchInput); searchLayout->addWidget(m_extInput);
    layout->addLayout(searchLayout);

    auto* infoLayout = new QHBoxLayout();
    m_infoLabel = new QLabel("等待操作..."); m_infoLabel->setStyleSheet("color: #888;");
    m_showHiddenCheck = new QCheckBox("显示隐性文件"); m_showHiddenCheck->setStyleSheet("color: #888;");
    connect(m_showHiddenCheck, &QCheckBox::toggled, this, &FileSearchWidget::refreshList);
    infoLayout->addWidget(m_infoLabel); infoLayout->addStretch(); infoLayout->addWidget(m_showHiddenCheck);
    layout->addLayout(infoLayout);

    auto* listHeader = new QHBoxLayout();
    auto* resultLbl = new QLabel("搜索结果"); resultLbl->setStyleSheet("color: #888; font-size: 12px;");
    listHeader->addWidget(resultLbl); listHeader->addStretch();
    auto* btnCopyAll = new QToolButton(); btnCopyAll->setIcon(IconHelper::getIcon("copy", "#1abc9c", 14));
    btnCopyAll->setStyleSheet("background: transparent; border: none;");
    connect(btnCopyAll, &QToolButton::clicked, this, [this](){ /* Copy Logic */ });
    listHeader->addWidget(btnCopyAll);
    layout->addLayout(listHeader);

    m_fileList = new QListWidget(); m_fileList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_fileList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_fileList, &QListWidget::customContextMenuRequested, this, &FileSearchWidget::showFileContextMenu);
    layout->addWidget(m_fileList);
}

void FileSearchWidget::setPath(const QString& p) { m_pathInput->setText(p); startScan(p); }
QString FileSearchWidget::currentPath() const { return m_pathInput->text().trimmed(); }
void FileSearchWidget::selectFolder() { QString d = QFileDialog::getExistingDirectory(this, "选择文件夹"); if(!d.isEmpty()) setPath(d); }
void FileSearchWidget::onPathReturnPressed() { startScan(m_pathInput->text().trimmed()); }
void FileSearchWidget::startScan(const QString& path) {
    if(path.isEmpty() || !QDir(path).exists()) return;
    if(m_scanThread) { m_scanThread->stop(); m_scanThread->deleteLater(); }
    m_fileList->clear(); m_filesData.clear(); m_infoLabel->setText("正在扫描: " + path);
    m_scanThread = new ScannerThread(path, this);
    connect(m_scanThread, &ScannerThread::fileFound, this, &FileSearchWidget::onFileFound);
    connect(m_scanThread, &ScannerThread::finished, this, &FileSearchWidget::onScanFinished);
    m_scanThread->start();
}
void FileSearchWidget::onFileFound(const QString& n, const QString& p, bool h) { m_filesData.append({n, p, h}); }
void FileSearchWidget::onScanFinished(int c) { m_infoLabel->setText(QString("扫描结束，共 %1 个文件").arg(c)); refreshList(); }
void FileSearchWidget::refreshList() {
    m_fileList->clear(); QString t = m_searchInput->text().toLower(); QString e = m_extInput->text().toLower();
    for(const auto& d : m_filesData) {
        if(!m_showHiddenCheck->isChecked() && d.isHidden) continue;
        if(!e.isEmpty() && !d.name.toLower().endsWith("." + e)) continue;
        if(!t.isEmpty() && !d.name.toLower().contains(t)) continue;
        auto* item = new QListWidgetItem(d.name); item->setData(Qt::UserRole, d.path); m_fileList->addItem(item);
    }
}
void FileSearchWidget::showFileContextMenu(const QPoint& pos) { /* Logic */ }
void FileSearchWidget::copySelectedFiles() { /* Logic */ }
void FileSearchWidget::onEditFile() { /* Logic */ }
void FileSearchWidget::onCutFile() { /* Logic */ }
void FileSearchWidget::onDeleteFile() { /* Logic */ }
void FileSearchWidget::onMergeSelectedFiles() { /* Logic */ }
void FileSearchWidget::onMergeFiles(const QStringList&, const QString&, bool) { /* Logic */ }
void FileSearchWidget::updateShortcuts() { /* Logic */ }
void FileSearchWidget::addHistoryEntry(const QString&) {}
QStringList FileSearchWidget::getHistory() const { return {}; }
void FileSearchWidget::clearHistory() {}
void FileSearchWidget::removeHistoryEntry(const QString&) {}
void FileSearchWidget::useHistoryPath(const QString& p) { setPath(p); }
void FileSearchWidget::addSearchHistoryEntry(const QString&) {}
QStringList FileSearchWidget::getSearchHistory() const { return {}; }
void FileSearchWidget::removeSearchHistoryEntry(const QString&) {}
void FileSearchWidget::clearSearchHistory() {}
bool FileSearchWidget::eventFilter(QObject* w, QEvent* e) { return QWidget::eventFilter(w, e); }

// FileSearchWindow
FileSearchWindow::FileSearchWindow(QWidget* p) : FramelessDialog("查找文件", p) {
    resize(1000, 600); m_searchWidget = new FileSearchWidget(m_contentArea);
    auto* l = new QVBoxLayout(m_contentArea); l->addWidget(m_searchWidget); m_resizeHandle = new ResizeHandle(this, this);
}
void FileSearchWindow::resizeEvent(QResizeEvent* e) { FramelessDialog::resizeEvent(e); if(m_resizeHandle) m_resizeHandle->move(width()-20, height()-20); }

#include "FileSearchWindow.moc"
