#include "QuickLookWindow.h"
#include <QKeyEvent>
#include <QFileInfo>
#include <QFile>
#include <QGraphicsPixmapItem>
#include <QLabel>

namespace ArcMeta {

QuickLookWindow& QuickLookWindow::instance() {
    static QuickLookWindow inst;
    return inst;
}

QuickLookWindow::QuickLookWindow() : QWidget(nullptr) {
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setStyleSheet("QWidget { background-color: rgba(30, 30, 30, 0.95); border: 1px solid #444; border-radius: 12px; }");
    
    resize(800, 600);
    initUi();
}

void QuickLookWindow::initUi() {
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(10, 10, 10, 10);

    m_titleLabel = new QLabel(this);
    m_titleLabel->setStyleSheet("color: #B0B0B0; font-size: 14px; font-weight: bold; margin-bottom: 5px;");
    m_mainLayout->addWidget(m_titleLabel);

    // 图片渲染层
    m_graphicsView = new QGraphicsView(this);
    m_graphicsView->setRenderHint(QPainter::Antialiasing);
    m_graphicsView->setRenderHint(QPainter::SmoothPixmapTransform);
    m_graphicsView->setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    m_graphicsView->setStyleSheet("background: transparent; border: none;");
    m_scene = new QGraphicsScene(this);
    m_graphicsView->setScene(m_scene);
    
    // 文本渲染层
    m_textPreview = new QPlainTextEdit(this);
    m_textPreview->setReadOnly(true);
    m_textPreview->setStyleSheet("background: transparent; color: #EEEEEE; border: none; font-family: 'Consolas';");
    
    m_mainLayout->addWidget(m_graphicsView);
    m_mainLayout->addWidget(m_textPreview);

    m_graphicsView->hide();
    m_textPreview->hide();
}

/**
 * @brief 预览文件分发逻辑
 */
void QuickLookWindow::previewFile(const QString& path) {
    QFileInfo info(path);
    m_titleLabel->setText(info.fileName());
    
    QString ext = info.suffix().toLower();
    if (ext == "jpg" || ext == "png" || ext == "bmp" || ext == "webp") {
        renderImage(path);
    } else {
        renderText(path);
    }

    show();
    raise();
    activateWindow();
}

/**
 * @brief 硬件加速图片渲染
 */
void QuickLookWindow::renderImage(const QString& path) {
    m_textPreview->hide();
    m_graphicsView->show();
    m_scene->clear();

    QPixmap pix(path);
    if (!pix.isNull()) {
        auto item = m_scene->addPixmap(pix);
        m_graphicsView->fitInView(item, Qt::KeepAspectRatio);
    }
}

/**
 * @brief 极速文本加载（红线：支持内存映射思想）
 */
void QuickLookWindow::renderText(const QString& path) {
    m_graphicsView->hide();
    m_textPreview->show();
    
    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        // 对于大文件，仅加载前 128KB (文档红线要求)
        QByteArray previewBytes = file.read(128 * 1024); 
        m_textPreview->setPlainText(QString::fromUtf8(previewBytes));
        file.close();
    }
}

/**
 * @brief 按键交互：ESC 或 Space 退出预览，1-5 快速打标预览点位
 */
void QuickLookWindow::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape || event->key() == Qt::Key_Space) {
        hide();
    } else if (event->key() >= Qt::Key_1 && event->key() <= Qt::Key_5) {
        int rating = event->key() - Qt::Key_0;
        emit ratingRequested(rating);
    }
}

} // namespace ArcMeta
