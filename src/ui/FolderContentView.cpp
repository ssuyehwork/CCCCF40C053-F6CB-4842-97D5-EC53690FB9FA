#include "FolderContentView.h"
#include <QHeaderView>

FolderContentView::FolderContentView(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_view = new QListView(this);
    m_view->setViewMode(QListView::IconMode);
    m_view->setGridSize(QSize(100, 100));
    m_view->setSpacing(10);
    m_view->setResizeMode(QListView::Adjust);
    m_view->setWordWrap(true);
    m_view->setStyleSheet("QListView { background: transparent; border: none; color: #BBB; } "
                          "QListView::item:hover { background-color: rgba(255, 255, 255, 0.05); border-radius: 4px; } "
                          "QListView::item:selected { background-color: rgba(46, 204, 113, 0.2); color: #2ecc71; border-radius: 4px; }");

    m_model = new QFileSystemModel(this);
    m_model->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot);

    m_view->setModel(m_model);
    layout->addWidget(m_view);
}

void FolderContentView::setRootPath(const QString& path) {
    if (path.isEmpty() || path == "Desktop" || path == "PC") {
        m_view->setRootIndex(QModelIndex());
        return;
    }

    m_view->setRootIndex(m_model->setRootPath(path));
}
