#include "FileSearchWindow.h"
#include "ToolTipOverlay.h"
#include "StringUtils.h"
#include "IconHelper.h"
#include "ResizeHandle.h"
#include "../core/ShortcutManager.h"
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
#include <QDirIterator>
#include <QMouseEvent>

// ----------------------------------------------------------------------------
// ScannerThread
// ----------------------------------------------------------------------------
ScannerThread::ScannerThread(const QString& folderPath, QObject* parent) : QThread(parent), m_folderPath(folderPath) {}
void ScannerThread::stop() { m_isRunning = false; wait(); }
void ScannerThread::run() {
    int count = 0; if (m_folderPath.isEmpty() || !QDir(m_folderPath).exists()) { emit finished(0); return; }
    std::function<void(const QString&)> scanDir = [&](const QString& currentPath) {
        if (!m_isRunning) return;
        QDir dir(currentPath);
        QFileInfoList files = dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden);
        for (const auto& fi : files) {
            if (!m_isRunning) return;
            bool hidden = fi.isHidden() || fi.fileName().startsWith('.');
            emit fileFound(fi.fileName(), fi.absoluteFilePath(), hidden);
            count++;
        }
        QFileInfoList subDirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
        for (const auto& di : subDirs) {
            if (!m_isRunning) return;
            if (!QStringList({".git", "node_modules", ".idea"}).contains(di.fileName())) scanDir(di.absoluteFilePath());
        }
    };
    scanDir(m_folderPath); emit finished(count);
}

// ----------------------------------------------------------------------------
// FileSearchHistoryPopup
// ----------------------------------------------------------------------------
FileSearchHistoryPopup::FileSearchHistoryPopup(FileSearchWidget* widget, QLineEdit* edit, Type type)
    : QWidget(widget->window(), Qt::Popup | Qt::FramelessWindowHint), m_widget(widget), m_edit(edit), m_type(type)
{
    auto* layout = new QVBoxLayout(this);
    m_chipsWidget = new QWidget();
    m_vLayout = new QVBoxLayout(m_chipsWidget);
    layout->addWidget(m_chipsWidget);
    setStyleSheet("background-color: #252526; border: 1px solid #444; border-radius: 8px;");
}
void FileSearchHistoryPopup::refreshUI() {
    QLayoutItem* item;
    while ((item = m_vLayout->takeAt(0))) { if(item->widget()) item->widget()->deleteLater(); delete item; }
    QStringList history = (m_type == Path) ? m_widget->getHistory() : m_widget->getSearchHistory();
    for(const QString& val : history) {
        auto* btn = new QPushButton(val);
        btn->setStyleSheet("text-align: left; padding: 5px; color: #EEE; background: transparent; border: none;");
        connect(btn, &QPushButton::clicked, this, [this, val](){
            if (m_type == Path) m_widget->useHistoryPath(val); else m_edit->setText(val); close();
        });
        m_vLayout->addWidget(btn);
    }
}
void FileSearchHistoryPopup::showAnimated() {
    refreshUI();
    move(m_edit->mapToGlobal(QPoint(0, m_edit->height())));
    show();
}

// ----------------------------------------------------------------------------
// FileSearchWidget
// ----------------------------------------------------------------------------
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
        #ActionBtn { background-color: #007ACC; color: white; border-radius: 6px; padding: 8px; font-weight: bold; }
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

    m_fileList = new QListWidget(); m_fileList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_fileList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_fileList, &QListWidget::customContextMenuRequested, this, &FileSearchWidget::showFileContextMenu);
    layout->addWidget(m_fileList);

    updateShortcuts();
}

void FileSearchWidget::setPath(const QString& path) { m_pathInput->setText(path); startScan(path); }
QString FileSearchWidget::currentPath() const { return m_pathInput->text().trimmed(); }
void FileSearchWidget::selectFolder() { QString d = QFileDialog::getExistingDirectory(this, "选择文件夹"); if (!d.isEmpty()) setPath(d); }
void FileSearchWidget::onPathReturnPressed() { startScan(m_pathInput->text().trimmed()); }
void FileSearchWidget::startScan(const QString& path) {
    if (path.isEmpty() || !QDir(path).exists()) return;
    if (m_scanThread) { m_scanThread->stop(); m_scanThread->deleteLater(); }
    m_fileList->clear(); m_filesData.clear(); m_infoLabel->setText("正在扫描: " + path);
    m_scanThread = new ScannerThread(path, this);
    connect(m_scanThread, &ScannerThread::fileFound, this, &FileSearchWidget::onFileFound);
    connect(m_scanThread, &ScannerThread::finished, this, &FileSearchWidget::onScanFinished);
    m_scanThread->start();
}
void FileSearchWidget::onFileFound(const QString& name, const QString& path, bool isHidden) { m_filesData.append({name, path, isHidden}); }
void FileSearchWidget::onScanFinished(int count) { m_infoLabel->setText(QString("扫描结束，共 %1 个文件").arg(count)); refreshList(); }
void FileSearchWidget::refreshList() {
    m_fileList->clear(); QString t = m_searchInput->text().toLower(); QString e = m_extInput->text().toLower();
    for(const auto& data : m_filesData) {
        if (!m_showHiddenCheck->isChecked() && data.isHidden) continue;
        if (!e.isEmpty() && !data.name.toLower().endsWith("." + e)) continue;
        if (!t.isEmpty() && !data.name.toLower().contains(t)) continue;
        auto* item = new QListWidgetItem(data.name); item->setData(Qt::UserRole, data.path); m_fileList->addItem(item);
    }
}
void FileSearchWidget::showFileContextMenu(const QPoint& pos) {
    QListWidgetItem* item = m_fileList->itemAt(pos); if(!item) return;
    QMenu menu(this); menu.addAction("定位文件", [item](){ QProcess::startDetached("explorer", {"/select,", QDir::toNativeSeparators(item->data(Qt::UserRole).toString())}); });
    menu.addAction("复制路径", [item](){ QApplication::clipboard()->setText(item->data(Qt::UserRole).toString()); });
    menu.exec(m_fileList->mapToGlobal(pos));
}
void FileSearchWidget::copySelectedFiles() {}
void FileSearchWidget::onEditFile() {}
void FileSearchWidget::onCutFile() {}
void FileSearchWidget::onDeleteFile() {}
void FileSearchWidget::onMergeSelectedFiles() {}
void FileSearchWidget::onMergeFiles(const QStringList&, const QString&, bool) {}
void FileSearchWidget::updateShortcuts() {}
void FileSearchWidget::addHistoryEntry(const QString&) {}
QStringList FileSearchWidget::getHistory() const { return {}; }
void FileSearchWidget::clearHistory() {}
void FileSearchWidget::removeHistoryEntry(const QString&) {}
void FileSearchWidget::useHistoryPath(const QString& p) { setPath(p); }
void FileSearchWidget::addSearchHistoryEntry(const QString&) {}
QStringList FileSearchWidget::getSearchHistory() const { return {}; }
void FileSearchWidget::removeSearchHistoryEntry(const QString&) {}
void FileSearchWidget::clearSearchHistory() {}
bool FileSearchWidget::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::MouseButtonDblClick && watched == m_pathInput) {
        if (!m_pathPopup) m_pathPopup = new FileSearchHistoryPopup(this, m_pathInput, FileSearchHistoryPopup::Path);
        m_pathPopup->showAnimated(); return true;
    }
    return QWidget::eventFilter(watched, event);
}

// ----------------------------------------------------------------------------
// FileSearchWindow
// ----------------------------------------------------------------------------
FileSearchWindow::FileSearchWindow(QWidget* parent) : FramelessDialog("查找文件", parent) {
    resize(1000, 600);
    m_searchWidget = new FileSearchWidget(m_contentArea);
    auto* layout = new QVBoxLayout(m_contentArea);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_searchWidget);
    m_resizeHandle = new ResizeHandle(this, this);
}
void FileSearchWindow::resizeEvent(QResizeEvent* event) {
    FramelessDialog::resizeEvent(event);
    if (m_resizeHandle) m_resizeHandle->move(width() - 20, height() - 20);
}
