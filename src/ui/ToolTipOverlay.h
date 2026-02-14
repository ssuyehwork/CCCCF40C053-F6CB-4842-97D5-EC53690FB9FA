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
#include <QPainterPath>

// ----------------------------------------------------------------------------
// ToolTipOverlay: 全局统一的自定义 Tooltip
// [CRITICAL] 本项目严禁使用任何形式的“Windows 系统默认 Tip 样式”！
// [RULE] 1. 杜绝原生内容带来的系统阴影和不透明度。
// [RULE] 2. 所有的 ToolTip 逻辑必须通过此 ToolTipOverlay 渲染。
// [RULE] 3. 此组件必须保持扁平化 (Flat)，严禁添加任何阴影特效。
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
        
        m_text = text;
        m_doc.setHtml(m_text);
        
        // 1. 计算尺寸 
        m_doc.setTextWidth(450);
        QSize textSize = m_doc.size().toSize();
        
        int padX = 12; 
        int padY = 8;
        
        int w = textSize.width() + padX * 2;
        int h = textSize.height() + padY * 2;
        
        // 最小宽高限制
        w = qMax(w, 40);
        h = qMax(h, 24);
        
        resize(w, h);
        
        // 2. 位置计算 (视觉偏移 15, 15)
        QPoint pos = globalPos + QPoint(15, 15);
        
        // 3. 边缘检测
        QScreen* screen = QGuiApplication::screenAt(globalPos);
        if (!screen) screen = QGuiApplication::primaryScreen();
        if (screen) {
            QRect screenGeom = screen->geometry();
            if (pos.x() + width() > screenGeom.right()) {
                pos.setX(globalPos.x() - width() - 15);
            }
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
        // [CRITICAL] 彻底杜绝系统阴影：必须显式包含 Qt::NoDropShadowWindowHint 标志。
        setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | 
                      Qt::WindowTransparentForInput | Qt::X11BypassWindowManagerHint | 
                      Qt::NoDropShadowWindowHint);
        // 显式禁用阴影（特定于某些环境）
        setObjectName("ToolTipOverlay");
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        
        m_doc.setUndoRedoEnabled(false);
        QFont f = font();
        f.setPointSize(9);
        m_doc.setDefaultFont(f);

        hide();
    }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        
        // 彻底去阴影，保持扁平矩形风格
        // 1 像素物理偏移校准位
        QRectF rectF(0.5, 0.5, width() - 1, height() - 1);
        
        // 背景色: #2B2B2B
        // 边框色: #B0B0B0, 宽度 1px
        p.setPen(QPen(QColor("#B0B0B0"), 1));
        p.setBrush(QColor("#2B2B2B"));
        p.drawRoundedRect(rectF, 4, 4);
        
        // 绘制内容预览
        p.save();
        p.translate(12, 8); // Padding Offset
        m_doc.drawContents(&p);
        p.restore();
    }

private:
    QString m_text;
    QTextDocument m_doc;
};

#endif // TOOLTIPOVERLAY_H
