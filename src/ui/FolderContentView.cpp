#include "FolderContentView.h"
#include <QHeaderView>
#include <QDesktopServices>
#include <QUrl>

FolderContentView::FolderContentView(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_view = new QListView(this);
    m_view->setViewMode(QListView::IconMode);
    m_view->setGridSize(QSize(100, 100));
    m_view->setSpacing(15);
    m_view->setResizeMode(QListView::Adjust);
    m_view->setWordWrap(true);
    m_view->setStyleSheet("QListView { background: transparent; border: none; color: #BBB; outline: none; } "
                          "QListView::item { border-radius: 6px; padding: 5px; } "
                          "QListView::item:hover { background-color: rgba(255, 255, 255, 0.05); } "
                          "QListView::item:selected { background-color: rgba(46, 204, 113, 0.2); color: #2ecc71; }");

    m_model = new QFileSystemModel(this);
    m_model->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot);
    
    m_view->setModel(m_model);
    layout->addWidget(m_view);

    // 2026-03-24 按照授权修改：增加双击交互逻辑
    connect(m_view, &QListView::doubleClicked, this, [this](const QModelIndex& index){
        if (!index.isValid()) return;

        QString path = m_model->filePath(index);
        bool isDir = m_model->isDir(index);

        emit itemDoubleClicked(path, isDir);

        if (isDir) {
            setRootPath(path);
            emit pathChanged(path);
        } else {
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        }
    });
}

void FolderContentView::setRootPath(const QString& path) {
    if (path.isEmpty() || path == "Desktop" || path == "PC") {
        m_view->setRootIndex(QModelIndex());
        return;
    }
    
    QModelIndex rootIndex = m_model->setRootPath(path);
    m_view->setRootIndex(rootIndex);
}

QString FolderContentView::currentPath() const {
    return m_model->rootPath();
}
