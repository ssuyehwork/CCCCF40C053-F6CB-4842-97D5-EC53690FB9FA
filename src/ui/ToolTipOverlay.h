#ifndef TOOLTIPOVERLAY_H
#define TOOLTIPOVERLAY_H

#include <QWidget>
#include <QPainter>
#include <QScreen>
#include <QApplication>
#include <QTimer>

// ----------------------------------------------------------------------------
// ToolTipOverlay: 自定义绘制的 Tooltip 覆盖层 (仿 PixelRulerOverlay 风格)
// ----------------------------------------------------------------------------
class ToolTipOverlay : public QWidget {
    Q_OBJECT
public:
    explicit ToolTipOverlay(QWidget* parent = nullptr) : QWidget(parent) {
        // 使用 ToolTip 标志确保它浮在顶层，但通过 FramelessWindowHint 去除系统边框
        // WindowTransparentForInput 确保鼠标可以穿透（尽管通常 Tooltip 不接收输入）
        setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowTransparentForInput);
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        hide();
    }

    void showText(const QPoint& globalPos, const QString& text) {
        if (text.isEmpty()) { hide(); return; }
        m_text = text;
        
        // Calculate size immediately
        QFont f = font();
        QFontMetrics fm(f);
        int padX = 10; 
        int padY = 6;
        // 限制最大宽度为 300，自动换行
        QRect textRect = fm.boundingRect(QRect(0, 0, 300, 1000), Qt::TextWordWrap, m_text);
        int w = textRect.width() + padX * 2;
        int h = textRect.height() + padY * 2;
        resize(w, h); // Resize before showing
        
        // Offset from cursor (15, 15) ensuring it doesn't cover the exact mouse point
        QPoint pos = globalPos + QPoint(15, 15);
        
        // Ensure within screen bounds
        QScreen* screen = QGuiApplication::screenAt(globalPos);
        if (!screen) screen = QGuiApplication::primaryScreen();
        if (screen) {
            QRect screenGeom = screen->geometry();
            // Right edge check
            if (pos.x() + width() > screenGeom.right()) {
                pos.setX(globalPos.x() - width() - 5);
            }
            // Bottom edge check
            if (pos.y() + height() > screenGeom.bottom()) {
                pos.setY(globalPos.y() - height() - 5);
            }
        }
        
        move(pos);
        show();
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        
        // Draw background
        // 边框色: #B0B0B0 (176, 176, 176)
        // 背景色: #2B2B2B (43, 43, 43)
        p.setPen(QPen(QColor(176, 176, 176), 1));
        p.setBrush(QColor(43, 43, 43));
        // 使用 QRectF 并偏移 0.5 像素以确保抗锯齿下的 1px 边框粗细均匀且不被裁剪
        p.drawRoundedRect(QRectF(0.5, 0.5, width() - 1, height() - 1), 4, 4);
        
        // Draw text
        p.setPen(Qt::white);
        p.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap, m_text);
    }

private:
    QString m_text;
};

#endif // TOOLTIPOVERLAY_H
