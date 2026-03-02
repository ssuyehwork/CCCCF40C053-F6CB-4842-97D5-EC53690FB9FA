#include "FileSearchWindow.h"
#include "FileSearchWidget.h"
#include "ResizeHandle.h"
#include <QVBoxLayout>

FileSearchWindow::FileSearchWindow(QWidget* parent) : FramelessDialog("查找文件", parent) {
    resize(1000, 600);
    m_searchWidget = new FileSearchWidget(m_contentArea);
    auto* layout = new QVBoxLayout(m_contentArea);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_searchWidget);
}

void FileSearchWindow::resizeEvent(QResizeEvent* event) {
    FramelessDialog::resizeEvent(event);
}
