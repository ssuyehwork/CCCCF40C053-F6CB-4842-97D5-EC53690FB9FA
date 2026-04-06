#include "PathAcquisitionWindow.h"
#include "StringUtils.h"

#include "IconHelper.h"
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QApplication>
#include <QClipboard>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QToolTip>
#include <QDirIterator>
#include <QDir>
#include <QFileInfo>
#include <QCheckBox>
#include <QMenu>
#include <QAction>
#include <QProcess>
#include <QDesktopServices>
#include <QFileDialog>
#include <QKeyEvent>
#include <utility>

PathAcquisitionWindow::PathAcquisitionWindow(QWidget* parent) : FramelessDialog("路径提取", parent) {
    setAcceptDrops(true);
    resize(700, 500); // 增加尺寸以适应左右布局

    initUI();
}

PathAcquisitionWindow::~PathAcquisitionWindow() {
}

void PathAcquisitionWindow::initUI() {
    auto* mainLayout = new QHBoxLayout(m_contentArea);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(20);

    // --- 左侧面板 (操作区) ---
    auto* leftPanel = new QWidget();
    auto* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(15);

    // 拖拽提示区 (使用内部布局和自定义 Label 确保在任意高度下都能完美居中)
    m_dropHint = new QToolButton();
    m_dropHint->setObjectName("DropZone");
    m_dropHint->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    
    auto* dropLayout = new QVBoxLayout(m_dropHint);
    dropLayout->setContentsMargins(20, 20, 20, 20);
    dropLayout->setSpacing(15);

    m_dropIconLabel = new QLabel();
    m_dropIconLabel->setAlignment(Qt::AlignCenter);
    m_dropIconLabel->setFixedSize(48, 48);
    m_dropIconLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    m_dropTextLabel = new QLabel("拖入文件/文件夹\n(或点击进行浏览)");
    m_dropTextLabel->setAlignment(Qt::AlignCenter);
    m_dropTextLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    dropLayout->addStretch();
    dropLayout->addWidget(m_dropIconLabel, 0, Qt::AlignCenter);
    dropLayout->addWidget(m_dropTextLabel, 0, Qt::AlignCenter);
    dropLayout->addStretch();

    updateDropHintStyle(false); // 初始化样式
    
    connect(m_dropHint, &QToolButton::clicked, this, &PathAcquisitionWindow::onBrowse);
    leftLayout->addWidget(m_dropHint, 1);

    // 选项
    m_recursiveCheck = new QCheckBox("递归遍历文件夹(包含所有子目录)");
    m_recursiveCheck->setStyleSheet("QCheckBox { color: #ccc; font-size: 12px; spacing: 5px; } QCheckBox::indicator { width: 16px; height: 16px; }");
    // 连接信号实现自动刷新
    connect(m_recursiveCheck, &QCheckBox::toggled, this, [this](bool){
        if (!m_currentUrls.isEmpty()) {
            processStoredUrls();
        }
    });
    leftLayout->addWidget(m_recursiveCheck);

    auto* tipLabel = new QLabel("提示：切换选项会自动刷新无需重新拖入");
    tipLabel->setStyleSheet("color: #666; font-size: 11px;");
    tipLabel->setAlignment(Qt::AlignLeft);
    tipLabel->setWordWrap(true);
    leftLayout->addWidget(tipLabel);

    leftLayout->addStretch(); // 底部弹簧

    leftPanel->setFixedWidth(200);
    mainLayout->addWidget(leftPanel);

    // --- 右侧面板 (列表区) ---
    auto* rightPanel = new QWidget();
    auto* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(10);

    auto* listHeaderLayout = new QHBoxLayout();
    listHeaderLayout->setContentsMargins(0, 0, 0, 0);

    auto* listLabel = new QLabel("提取结果");
    listLabel->setStyleSheet("color: #888; font-size: 12px; font-weight: bold;");
    
    m_copyAllBtn = new QToolButton();
    m_copyAllBtn->setIcon(IconHelper::getIcon("copy", "#1abc9c", 16));
    m_copyAllBtn->setToolTip(StringUtils::wrapToolTip("复制全部路径"));
    m_copyAllBtn->setCursor(Qt::PointingHandCursor);
    m_copyAllBtn->setStyleSheet("QToolButton { border: none; background: transparent; padding: 2px; }"
                                "QToolButton:hover { background-color: #3E3E42; border-radius: 4px; }");
    connect(m_copyAllBtn, &QToolButton::clicked, this, &PathAcquisitionWindow::onCopyAll);

    listHeaderLayout->addWidget(listLabel);
    listHeaderLayout->addStretch();
    listHeaderLayout->addWidget(m_copyAllBtn);
    rightLayout->addLayout(listHeaderLayout);

    m_pathList = new QListWidget(this);
    m_pathList->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_pathList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_pathList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_pathList->setStyleSheet("QListWidget { background-color: #252526; border: 1px solid #333; border-radius: 6px; color: #AAA; padding: 5px; font-size: 12px; outline: none; } "
                              "QListWidget::item { padding: 4px; border-bottom: 1px solid #2d2d2d; } "
                              "QListWidget::item:selected { background-color: #3E3E42; color: #FFF; }");
    m_pathList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_pathList->setCursor(Qt::PointingHandCursor);
    connect(m_pathList, &QListWidget::customContextMenuRequested, this, &PathAcquisitionWindow::onShowContextMenu);
    connect(m_pathList, &QListWidget::itemDoubleClicked, this, [](QListWidgetItem* item) {
        QString path = item->text();
        QProcess::startDetached("explorer.exe", { "/select,", QDir::toNativeSeparators(path) });
    });

    // 快捷键支持
    auto* actionSelectAll = new QAction(this);
    actionSelectAll->setShortcut(QKeySequence("Ctrl+A"));
    connect(actionSelectAll, &QAction::triggered, [this](){ m_pathList->selectAll(); });
    m_pathList->addAction(actionSelectAll);

    auto* actionDelete = new QAction(this);
    actionDelete->setShortcut(QKeySequence(Qt::Key_Delete));
    connect(actionDelete, &QAction::triggered, [this](){
        auto selected = m_pathList->selectedItems();
        if (selected.isEmpty()) {
            QToolTip::showText(QCursor::pos(), StringUtils::wrapToolTip("<b style='color:#e74c3c;'>✖ 请先选择要操作的内容</b>"), this, {}, 2000);
            return;
        }
        for (auto* item : std::as_const(selected)) delete item;
    });
    m_pathList->addAction(actionDelete);

    rightLayout->addWidget(m_pathList);

    mainLayout->addWidget(rightPanel);
}

void PathAcquisitionWindow::updateDropHintStyle(bool dragging) {
    if (dragging) {
        m_dropHint->setStyleSheet(
            "QToolButton#DropZone { border: 2px dashed #3a90ff; border-radius: 8px; background-color: rgba(58, 144, 255, 0.05); }"
        );
        m_dropIconLabel->setPixmap(IconHelper::getIcon("folder", "#3a90ff", 48).pixmap(48, 48));
        m_dropTextLabel->setStyleSheet("color: #3a90ff; font-size: 13px; background: transparent; border: none; font-weight: bold;");
    } else {
        m_dropHint->setStyleSheet(
            "QToolButton#DropZone { border: 2px dashed #444; border-radius: 8px; background: #181818; }"
            "QToolButton#DropZone:hover { border-color: #555; background: #202020; }"
        );
        m_dropIconLabel->setPixmap(IconHelper::getIcon("folder", "#888888", 48).pixmap(48, 48));
        m_dropTextLabel->setStyleSheet("color: #888; font-size: 13px; background: transparent; border: none;");
    }
}

void PathAcquisitionWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
        updateDropHintStyle(true);
    }
}

void PathAcquisitionWindow::dragLeaveEvent(QDragLeaveEvent* event) {
    Q_UNUSED(event);
    updateDropHintStyle(false);
}

void PathAcquisitionWindow::dropEvent(QDropEvent* event) {
    const QMimeData* mimeData = event->mimeData();
    if (mimeData->hasUrls()) {
        m_currentUrls = mimeData->urls();
        processStoredUrls();
    }
    updateDropHintStyle(false);
}

void PathAcquisitionWindow::hideEvent(QHideEvent* event) {
    m_currentUrls.clear();
    m_pathList->clear();
    FramelessDialog::hideEvent(event);
}

void PathAcquisitionWindow::keyPressEvent(QKeyEvent* event) {
    if (event->modifiers() == Qt::ControlModifier && event->key() == Qt::Key_V) {
        const QMimeData* mimeData = QApplication::clipboard()->mimeData();
        if (mimeData && mimeData->hasUrls()) {
            m_currentUrls = mimeData->urls();
            processStoredUrls();
            QToolTip::showText(QCursor::pos(), StringUtils::wrapToolTip("<b style='color:#2ecc71;'>✔ 已粘贴并提取路径</b>"), this, {}, 2000);
        } else {
            QToolTip::showText(QCursor::pos(), StringUtils::wrapToolTip("<b style='color:#e74c3c;'>✖ 剪贴板中无文件/文件夹</b>"), this, {}, 2000);
        }
    } else {
        FramelessDialog::keyPressEvent(event);
    }
}

void PathAcquisitionWindow::processStoredUrls() {
    m_pathList->clear();
    
    QStringList paths;
    for (const QUrl& url : std::as_const(m_currentUrls)) {
        QString path = url.toLocalFile();
        if (!path.isEmpty()) {
            QFileInfo fileInfo(path);
            
            // 如果选中递归且是目录，则遍历
            if (m_recursiveCheck->isChecked() && fileInfo.isDir()) {
                QDirIterator it(path, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
                while (it.hasNext()) {
                    QString subPath = it.next();
                    // 统一路径分隔符
                    subPath.replace("\\", "/");
                    m_pathList->addItem(subPath);
                    paths << subPath;
                }
            } else {
                // 默认行为：只添加该路径本身
                // 统一路径分隔符
                path.replace("\\", "/");
                m_pathList->addItem(path);
                paths << path;
            }
        }
    }
    
    m_pathList->scrollToBottom();
}

void PathAcquisitionWindow::onShowContextMenu(const QPoint& pos) {
    auto selectedItems = m_pathList->selectedItems();
    if (selectedItems.isEmpty()) {
        auto* item = m_pathList->itemAt(pos);
        if (item) {
            item->setSelected(true);
            selectedItems << item;
        }
    }

    if (selectedItems.isEmpty()) return;

    QStringList paths;
    for (auto* item : std::as_const(selectedItems)) {
        paths << item->text();
    }
    QString combinedPath = paths.join("\n");

    QMenu menu(this);
    menu.setStyleSheet("QMenu { background-color: #2D2D2D; color: #EEE; border: 1px solid #444; padding: 4px; } "
                       "QMenu::item { padding: 6px 10px 6px 10px; border-radius: 3px; } "
                       "QMenu::item:selected { background-color: #4a90e2; color: white; }");

    // 复制路径 (Copy Path)
    QString copyPathText = selectedItems.size() > 1 ? "复制选中路径" : "复制路径";
    menu.addAction(IconHelper::getIcon("copy", "#1abc9c", 18), copyPathText, [combinedPath]() {
        QApplication::clipboard()->setText(combinedPath);
    });

    // 复制文件 (Copy File)
    QString copyFileText = selectedItems.size() > 1 ? "复制选中文件" : "复制文件";
    menu.addAction(IconHelper::getIcon("file", "#3498db", 18), copyFileText, [paths]() {
        QMimeData* data = new QMimeData;
        QList<QUrl> urls;
        for (const QString& path : std::as_const(paths)) {
            urls << QUrl::fromLocalFile(path);
        }
        data->setUrls(urls);
        QApplication::clipboard()->setMimeData(data);
    });

    menu.addSeparator();

    if (selectedItems.size() == 1) {
        QString path = selectedItems.first()->text();
        // 定位文件 (Locate File)
        menu.addAction(IconHelper::getIcon("search", "#e67e22", 18), "定位文件", [path]() {
            QProcess::startDetached("explorer.exe", { "/select,", QDir::toNativeSeparators(path) });
        });

        // 定位文件夹 (Locate Folder)
        menu.addAction(IconHelper::getIcon("folder", "#f1c40f", 18), "定位文件夹", [path]() {
            QFileInfo fi(path);
            QString dirPath = fi.isDir() ? path : fi.absolutePath();
            QDesktopServices::openUrl(QUrl::fromLocalFile(dirPath));
        });
    }

    menu.exec(m_pathList->mapToGlobal(pos));
}

void PathAcquisitionWindow::onBrowse() {
    QMenu menu(this);
    menu.setStyleSheet("QMenu { background-color: #2D2D2D; color: #EEE; border: 1px solid #444; padding: 4px; } "
                       "QMenu::item { padding: 6px 10px 6px 10px; border-radius: 3px; } "
                       "QMenu::item:selected { background-color: #4a90e2; color: white; }");

    menu.addAction(IconHelper::getIcon("file", "#3498db", 18), "选择文件", [this]() {
        QStringList filePaths = QFileDialog::getOpenFileNames(this, "选择文件", "", "所有文件 (*.*)");
        if (!filePaths.isEmpty()) {
            m_currentUrls.clear();
            for (const QString& path : std::as_const(filePaths)) {
                m_currentUrls << QUrl::fromLocalFile(path);
            }
            processStoredUrls();
        }
    });

    menu.addAction(IconHelper::getIcon("folder", "#f1c40f", 18), "选择文件夹", [this]() {
        QString dirPath = QFileDialog::getExistingDirectory(this, "选择文件夹", "");
        if (!dirPath.isEmpty()) {
            m_currentUrls.clear();
            m_currentUrls << QUrl::fromLocalFile(dirPath);
            processStoredUrls();
        }
    });

    menu.exec(QCursor::pos());
}

void PathAcquisitionWindow::onCopyAll() {
    if (m_pathList->count() == 0) {
        QToolTip::showText(QCursor::pos(), StringUtils::wrapToolTip("<b style='color:#e74c3c;'>✖ 列表为空</b>"), this, {}, 2000);
        return;
    }

    QStringList paths;
    for (int i = 0; i < m_pathList->count(); ++i) {
        paths << m_pathList->item(i)->text();
    }

    QApplication::clipboard()->setText(paths.join("\n"));
    QToolTip::showText(QCursor::pos(), StringUtils::wrapToolTip("<b style='color:#2ecc71;'>✔ 已复制全部路径</b>"), this, {}, 2000);
}

