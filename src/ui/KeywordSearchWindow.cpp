#include "KeywordSearchWindow.h"
#include "KeywordSearchWidget.h"
#include <QVBoxLayout>

KeywordSearchWindow::KeywordSearchWindow(QWidget* parent) : FramelessDialog("关键字查找", parent) {
    resize(1100, 750);
    m_searchWidget = new KeywordSearchWidget(m_contentArea);
    auto* layout = new QVBoxLayout(m_contentArea);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_searchWidget);
}

void KeywordSearchWindow::resizeEvent(QResizeEvent* event) {
    FramelessDialog::resizeEvent(event);
}
