#ifndef TOOLTIPOVERLAY_H
#define TOOLTIPOVERLAY_H

#include <QWidget>
#include <QPainter>
#include <QScreen>
#include <QApplication>
#include <QTimer>
#include <QFontMetrics>
#include <QTextDocument>
#include <QPointer>

// ----------------------------------------------------------------------------
// ToolTipOverlay: 全局统一的自定义 Tooltip
// ----------------------------------------------------------------------------
class ToolTipOverlay : public QWidget {
    Q_OBJECT
public:
    static ToolTipOverlay* instance() {
        static QPointer<ToolTipOverlay> inst;
        if (!inst) {
            inst = new ToolTipOverlay();
        }
        return inst;
    }

    void showText(const QPoint& globalPos, const QString& text) {
        if (text.isEmpty()) { hide(); return; }
        
        // 预设属性 (去除所有 HTML 标签以符合“纯白色文字”要求)
        m_text = text;
        if (m_text.contains("<")) {
            QTextDocument doc;
            doc.setHtml(m_text);
            m_text = doc.toPlainText();
        }
        
        // 1. 计算尺寸 (水平 10px, 垂直 6px 留白)
        QFont f = font();
        QFontMetrics fm(f);
        int padX = 10; 
        int padY = 6;
        
        // 提取纯文本以计算尺寸（防止富文本标签干扰计算，虽然我们主要显示纯文本或简单格式）
        // 如果 text 包含 HTML，fm.boundingRect 可能不准，但根据要求居中显示 HEX 或简单描述
        // 我们假设主要使用系统默认字体计算
        QRect textRect = fm.boundingRect(QRect(0, 0, 400, 1000), Qt::TextWordWrap, m_text);
        int w = textRect.width() + padX * 2;
        int h = textRect.height() + padY * 2;
        
        // 保证最小宽高
        w = qMax(w, 40);
        h = qMax(h, 24);
        
        resize(w, h);
        
        // 2. 位置计算 (偏移 15, 15)
        QPoint pos = globalPos + QPoint(15, 15);
        
        // 3. 边缘检测
        QScreen* screen = QGuiApplication::screenAt(globalPos);
        if (!screen) screen = QGuiApplication::primaryScreen();
        if (screen) {
            QRect screenGeom = screen->geometry();
            // 右边缘检测 -> 翻转到左侧
            if (pos.x() + width() > screenGeom.right()) {
                pos.setX(globalPos.x() - width() - 15);
            }
            // 底部检测 -> 翻转到上方
            if (pos.y() + height() > screenGeom.bottom()) {
                pos.setY(globalPos.y() - height() - 15);
            }
        }
        
        move(pos);
        show();
        raise();
        update();
    }

    static void hideTip() {
        if (instance()) instance()->hide();
    }

protected:
    explicit ToolTipOverlay() : QWidget(nullptr) {
        setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowTransparentForInput);
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        hide();
    }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        
        // [CRITICAL] 渲染精度：必须使用 QRectF(0.5, 0.5, width() - 1, height() - 1) 进行绘制。
        // 这确保了在开启抗锯齿的情况下，1 像素的边框能够精确对齐物理像素，不会出现模糊、重影或四边粗细不一的情况。
        QRectF rectF(0.5, 0.5, width() - 1, height() - 1);
        
        // 背景色: #2B2B2B
        // 边框色: #B0B0B0, 宽度 1px
        p.setPen(QPen(QColor("#B0B0B0"), 1));
        p.setBrush(QColor("#2B2B2B"));
        
        // 圆角半径: 4 像素
        p.drawRoundedRect(rectF, 4, 4);
        
        // 文字渲染: 纯白色，居中
        p.setPen(Qt::white);
        // 使用 rect() 进行文字绘制，因为渲染的是内容
        p.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap, m_text);
    }

private:
    QString m_text;
};

#endif // TOOLTIPOVERLAY_H
