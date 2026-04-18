#include "ScreenshotTool.h"
#include "StringUtils.h"

#include "IconHelper.h"
#include <QApplication>
#include <QScreen>
#include <QPainterPathStroker>
#include <QFileDialog>
#include <QClipboard>
#include <QMenu>
#include <QDateTime>
#include <QInputDialog>
#include <QStyle>
#include <QStyleOption>
#include <QColorDialog>
#include <QSettings>
#include <QToolTip>
#include <QDir>
#include <QCoreApplication>
#include <cmath>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#include <tchar.h>
#pragma comment(lib, "dwmapi.lib")

QRect getActualWindowRect(HWND hwnd) {
    RECT rect;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(rect)))) {
        return QRect(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
    }
    GetWindowRect(hwnd, &rect);
    return QRect(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
}
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

class IconFactory {
public:
    static QIcon createArrowStyleIcon(ArrowStyle style) {
        QPixmap pix(40, 20);
        pix.fill(Qt::transparent);
        QPainter p(&pix);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(Qt::white, 2, Qt::SolidLine, Qt::FlatCap, Qt::RoundJoin));
        p.setBrush(Qt::white);
        
        QPointF start(5, 10), end(35, 10);
        QPointF dir = end - start;
        double angle = std::atan2(dir.y(), dir.x());

        bool isOutline = (style == ArrowStyle::OutlineSingle || style == ArrowStyle::OutlineDouble || style == ArrowStyle::OutlineDot);
        
        if (style == ArrowStyle::SolidSingle || style == ArrowStyle::OutlineSingle) {
            double hLen = 15;
            double bWid = 8;
            double wLen = 11;
            double wWid = 2;
            QPointF unit_dir = dir / 30.0;
            QPointF perp_dir(-unit_dir.y(), unit_dir.x());

            if (isOutline) {
                p.setPen(QPen(Qt::white, 1.5));
                p.setBrush(Qt::transparent);
            } else {
                p.setPen(Qt::NoPen);
                p.setBrush(Qt::white);
            }
            p.drawPolygon(QPolygonF() << end 
                << end - unit_dir * hLen + perp_dir * bWid
                << end - unit_dir * wLen + perp_dir * wWid
                << start
                << end - unit_dir * wLen - perp_dir * wWid
                << end - unit_dir * hLen - perp_dir * bWid);
        } else if (style == ArrowStyle::Thin) {
            p.setPen(QPen(Qt::white, 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawLine(start, end);
            p.drawLine(end, end - QPointF(11 * std::cos(angle - 0.55), 11 * std::sin(angle - 0.55)));
            p.drawLine(end, end - QPointF(11 * std::cos(angle + 0.55), 11 * std::sin(angle + 0.55)));
        } else if (style == ArrowStyle::SolidDouble || style == ArrowStyle::OutlineDouble) {
            p.setPen(QPen(Qt::white, 1.5));
            if (isOutline) p.setBrush(Qt::transparent); else p.setBrush(Qt::white);
            auto drawH = [&](const QPointF& e, double ang) {
                QPointF du(std::cos(ang), std::sin(ang));
                QPointF dp(-du.y(), du.x());
                p.drawPolygon(QPolygonF() << e << e - du * 10 + dp * 5 << e - du * 7 + dp * 1 << e - du * 7 - dp * 1 << e - du * 10 - dp * 5);
            };
            p.drawLine(start + (dir/30.0)*6, end - (dir/30.0)*6);
            drawH(end, angle); drawH(start, angle + M_PI);
        } else if (style == ArrowStyle::SolidDot || style == ArrowStyle::OutlineDot) {
            p.setPen(QPen(Qt::white, 1.5));
            if (isOutline) p.setBrush(Qt::transparent); else p.setBrush(Qt::white);
            p.drawLine(start, end - (dir/30.0)*6);
            p.drawEllipse(start, 3, 3);
            QPointF du(std::cos(angle), std::sin(angle));
            QPointF dp(-du.y(), du.x());
            p.drawPolygon(QPolygonF() << end << end - du * 10 + dp * 5 << end - du * 7 + dp * 1 << end - du * 7 - dp * 1 << end - du * 10 - dp * 5);
        }
        return QIcon(pix);
    }
};

PinnedScreenshotWidget::PinnedScreenshotWidget(const QPixmap& pixmap, const QRect& screenRect, QWidget* parent)
    : QWidget(nullptr, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool), m_pixmap(pixmap)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);
    setFixedSize(pixmap.size() / pixmap.devicePixelRatio());
    move(screenRect.topLeft());
}

void PinnedScreenshotWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.drawPixmap(rect(), m_pixmap);
    p.setPen(QPen(QColor(0, 120, 255, 200), 2));
    p.drawRect(rect().adjusted(0, 0, -1, -1));
}

void PinnedScreenshotWidget::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) m_dragPos = e->globalPosition().toPoint() - frameGeometry().topLeft();
}

void PinnedScreenshotWidget::mouseMoveEvent(QMouseEvent* e) {
    if (e->buttons() & Qt::LeftButton) move(e->globalPosition().toPoint() - m_dragPos);
}

void PinnedScreenshotWidget::mouseDoubleClickEvent(QMouseEvent*) { close(); }
void PinnedScreenshotWidget::contextMenuEvent(QContextMenuEvent* e) {
    QMenu menu(this);
    menu.addAction("复制", [this](){ QApplication::clipboard()->setPixmap(m_pixmap); });
    menu.addAction("保存", [this](){
        QString fileName = QString("RPN_%1.png").arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
        QString f = QFileDialog::getSaveFileName(this, "保存截图", fileName, "PNG(*.png)");
        if(!f.isEmpty()) m_pixmap.save(f);
    });
    menu.addSeparator();
    menu.addAction("关闭", this, &QWidget::close);
    menu.exec(e->globalPos());
}

SelectionInfoBar::SelectionInfoBar(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFixedSize(180, 28);
    hide();
}
void SelectionInfoBar::updateInfo(const QRect& rect) {
    m_text = QString("%1, %2 | %3 x %4").arg(rect.x()).arg(rect.y()).arg(rect.width()).arg(rect.height());
    update();
}
void SelectionInfoBar::paintEvent(QPaintEvent*) {
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(QColor(0, 0, 0, 200)); p.setPen(Qt::NoPen);
    p.drawRoundedRect(rect(), 4, 4);
    p.setPen(Qt::white); p.setFont(QFont("Arial", 9));
    p.drawText(rect(), Qt::AlignCenter, m_text);
}

ScreenshotToolbar::ScreenshotToolbar(ScreenshotTool* tool) 
    : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint) 
{
    m_tool = tool;
    setObjectName("ScreenshotToolbar");
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_AlwaysShowToolTips);
    setMouseTracking(true);

    setStyleSheet(R"(
        #ScreenshotToolbar { background-color: #2D2D2D; border: 1px solid #555; border-radius: 6px; }
        QPushButton { background: transparent; border: none; border-radius: 4px; padding: 4px; }
        QPushButton:hover { background-color: #444; }
        QPushButton:checked { background-color: #007ACC; }
        QWidget#OptionWidget { background-color: #333; border-top: 1px solid #555; border-bottom-left-radius: 6px; border-bottom-right-radius: 6px; }
        QPushButton[colorBtn="true"] { border: 2px solid transparent; border-radius: 2px; }
        QPushButton[colorBtn="true"]:checked { border-color: white; }
        QPushButton[sizeBtn="true"] { background-color: #777; border-radius: 50%; }
        QPushButton[sizeBtn="true"]:checked { background-color: #007ACC; }
    )");

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0); mainLayout->setSpacing(0);

    QWidget* toolRow = new QWidget;
    auto* layout = new QHBoxLayout(toolRow);
    layout->setContentsMargins(8, 6, 8, 6); layout->setSpacing(8);

    addToolButton(layout, ScreenshotToolType::Rect, "screenshot_rect", "矩形 (R)");
    addToolButton(layout, ScreenshotToolType::Ellipse, "screenshot_ellipse", "椭圆 (E)");
    addToolButton(layout, ScreenshotToolType::Arrow, "screenshot_arrow", "箭头 (A)");
    addToolButton(layout, ScreenshotToolType::Line, "screenshot_line", "直线 (L)");
    
    auto* line = new QFrame; line->setFrameShape(QFrame::VLine); line->setStyleSheet("color: #666;"); layout->addWidget(line);
    
    addToolButton(layout, ScreenshotToolType::Pen, "screenshot_pen", "画笔 (P)");
    addToolButton(layout, ScreenshotToolType::Marker, "screenshot_marker", "记号笔 (M)");
    addToolButton(layout, ScreenshotToolType::Text, "screenshot_text", "文字 (T)");
    addToolButton(layout, ScreenshotToolType::Mosaic, "screenshot_mosaic", "画笔马赛克 (Z)");
    addToolButton(layout, ScreenshotToolType::MosaicRect, "screenshot_rect", "矩形马赛克 (M)");
    addToolButton(layout, ScreenshotToolType::Eraser, "screenshot_eraser", "橡皮擦 (X)");

    layout->addStretch();
    
    addActionButton(layout, "undo", "撤销 (Ctrl+Z)", [tool]{ tool->undo(); });
    addActionButton(layout, "redo", "重做 (Ctrl+Shift+Z)", [tool]{ tool->redo(); });
    addActionButton(layout, "screenshot_pin", "置顶截图 (F)", [tool]{ tool->pin(); });
    addActionButton(layout, "screenshot_ocr", "文字识别 (O)", [tool]{ tool->executeOCR(); });
    addActionButton(layout, "screenshot_save", "保存", [tool]{ tool->save(); });
    addActionButton(layout, "screenshot_copy", "复制 (Ctrl+C)", [tool]{ tool->copyToClipboard(); });
    addActionButton(layout, "screenshot_close", "取消 (Esc)", [tool]{ tool->cancel(); }); 
    addActionButton(layout, "screenshot_confirm", "完成 (Enter)", [tool]{ tool->confirm(); });

    mainLayout->addWidget(toolRow);
    createOptionWidget();
    mainLayout->addWidget(m_optionWidget);
}

void ScreenshotToolbar::addToolButton(QBoxLayout* layout, ScreenshotToolType type, const QString& iconName, const QString& tip) {
    auto* btn = new QPushButton();
    btn->setIcon(IconHelper::getIcon(iconName)); btn->setIconSize(QSize(20, 20));
    btn->setToolTip(tip); btn->setCheckable(true); btn->setFixedSize(32, 32);
    layout->addWidget(btn); m_buttons[type] = btn;
    connect(btn, &QPushButton::clicked, [this, type]{ selectTool(type); });
}

void ScreenshotToolbar::addActionButton(QBoxLayout* layout, const QString& iconName, const QString& tip, std::function<void()> func) {
    auto* btn = new QPushButton();
    btn->setIcon(IconHelper::getIcon(iconName)); btn->setIconSize(QSize(20, 20));
    btn->setToolTip(tip); btn->setFixedSize(32, 32);
    layout->addWidget(btn); connect(btn, &QPushButton::clicked, func);
}

void ScreenshotToolbar::createOptionWidget() {
    m_optionWidget = new QWidget; m_optionWidget->setObjectName("OptionWidget");
    auto* layout = new QHBoxLayout(m_optionWidget); layout->setContentsMargins(10, 6, 10, 8); layout->setSpacing(12);

    m_arrowStyleBtn = new QPushButton(); m_arrowStyleBtn->setFixedSize(40, 24);
    updateArrowButtonIcon(m_tool->m_currentArrowStyle);
    connect(m_arrowStyleBtn, &QPushButton::clicked, this, &ScreenshotToolbar::showArrowMenu);
    layout->addWidget(m_arrowStyleBtn);

    int sizes[] = {2, 4, 8};
    auto* sizeGrp = new QButtonGroup(this);
    for(int s : sizes) {
        auto* btn = new QPushButton; btn->setProperty("sizeBtn", true); btn->setFixedSize(14 + s, 14 + s);
        btn->setCheckable(true); if(s == m_tool->m_currentStrokeWidth) btn->setChecked(true);
        layout->addWidget(btn); sizeGrp->addButton(btn);
        connect(btn, &QPushButton::clicked, [this, s]{ m_tool->setDrawWidth(s); });
    }

    QList<QColor> colors = {Qt::red, QColor(255, 165, 0), Qt::green, QColor(0, 120, 255), Qt::white};
    auto* colGrp = new QButtonGroup(this);
    for(const auto& c : colors) {
        auto* btn = new QPushButton; btn->setProperty("colorBtn", true); btn->setFixedSize(20, 20);
        btn->setStyleSheet(QString("background-color: %1;").arg(c.name()));
        btn->setCheckable(true); if(c == m_tool->m_currentColor) btn->setChecked(true);
        layout->addWidget(btn); colGrp->addButton(btn);
        connect(btn, &QPushButton::clicked, [this, c]{ m_tool->setDrawColor(c); });
    }
    layout->addStretch();
    m_optionWidget->setVisible(false);
}

void ScreenshotToolbar::showArrowMenu() {
    QMenu menu(this);
    auto addAct = [&](ArrowStyle s, const QString& txt) {
        QAction* act = menu.addAction(IconFactory::createArrowStyleIcon(s), txt);
        connect(act, &QAction::triggered, [this, s]{ m_tool->setArrowStyle(s); updateArrowButtonIcon(s); });
    };
    addAct(ArrowStyle::SolidSingle, "单向实心");
    addAct(ArrowStyle::OutlineSingle, "单向空心");
    addAct(ArrowStyle::Thin, "单向细线");
    menu.addSeparator();
    addAct(ArrowStyle::SolidDouble, "双向实心");
    addAct(ArrowStyle::OutlineDouble, "双向空心");
    menu.addSeparator();
    addAct(ArrowStyle::SolidDot, "圆点实心");
    addAct(ArrowStyle::OutlineDot, "圆点空心");
    menu.exec(mapToGlobal(m_arrowStyleBtn->geometry().bottomLeft()));
}

void ScreenshotToolbar::updateArrowButtonIcon(ArrowStyle style) {
    m_arrowStyleBtn->setIcon(IconFactory::createArrowStyleIcon(style)); m_arrowStyleBtn->setIconSize(QSize(32, 16));
}

void ScreenshotToolbar::selectTool(ScreenshotToolType type) {
    for(auto* b : m_buttons) b->setChecked(false);
    if(m_buttons.contains(type)) m_buttons[type]->setChecked(true);
    m_optionWidget->setVisible(type != ScreenshotToolType::None);
    m_arrowStyleBtn->setVisible(type == ScreenshotToolType::Arrow);
    m_tool->setTool(type); adjustSize(); m_tool->updateToolbarPosition();
}

void ScreenshotToolbar::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        m_isDragging = true; m_dragPosition = event->globalPosition().toPoint() - frameGeometry().topLeft();
    }
}
void ScreenshotToolbar::mouseMoveEvent(QMouseEvent *event) {
    if (m_isDragging) move(event->globalPosition().toPoint() - m_dragPosition);
}
void ScreenshotToolbar::mouseReleaseEvent(QMouseEvent *) { m_isDragging = false; }
void ScreenshotToolbar::paintEvent(QPaintEvent *) {
    QStyleOption opt; opt.initFrom(this); QPainter p(this); style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
}

ScreenshotTool::ScreenshotTool(QWidget* parent) 
    : QWidget(parent, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint) 
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_QuitOnClose, false);
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowState(Qt::WindowFullScreen);
    setMouseTracking(true);
    m_screenPixmap = QGuiApplication::primaryScreen()->grabWindow(0);
    QImage img = m_screenPixmap.toImage();
    QImage small = img.scaled(img.width()/15, img.height()/15, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    m_mosaicPixmap = QPixmap::fromImage(small.scaled(img.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation));

    QSettings settings("RapidNotes", "Screenshot");
    m_currentColor = settings.value("color", QColor(255, 50, 50)).value<QColor>();
    m_currentStrokeWidth = settings.value("strokeWidth", 3).toInt();
    m_currentArrowStyle = static_cast<ArrowStyle>(settings.value("arrowStyle", 0).toInt());
    m_currentTool = static_cast<ScreenshotToolType>(settings.value("tool", 0).toInt());

    m_toolbar = new ScreenshotToolbar(this); m_toolbar->hide();
    m_infoBar = new SelectionInfoBar(this);
    m_textInput = new QLineEdit(this); m_textInput->hide(); m_textInput->setFrame(false);
    connect(m_textInput, &QLineEdit::editingFinished, this, &ScreenshotTool::commitTextInput);
}

void ScreenshotTool::showEvent(QShowEvent* event) { QWidget::showEvent(event); detectWindows(); }
void ScreenshotTool::cancel() { emit screenshotCanceled(); if (m_toolbar) m_toolbar->close(); close(); }

void ScreenshotTool::drawArrow(QPainter& p, const QPointF& start, const QPointF& end, const DrawingAnnotation& ann) {
    QPointF dir = end - start;
    double len = std::sqrt(QPointF::dotProduct(dir, dir));
    if (len < 2) return;
    QPointF unit = dir / len; QPointF perp(-unit.y(), unit.x());
    double angle = std::atan2(dir.y(), dir.x());
    double headLen = 22 + ann.strokeWidth * 2.5;
    
    bool isOutline = (ann.arrowStyle == ArrowStyle::OutlineSingle || ann.arrowStyle == ArrowStyle::OutlineDouble || ann.arrowStyle == ArrowStyle::OutlineDot);
    
    if (ann.arrowStyle == ArrowStyle::SolidSingle || ann.arrowStyle == ArrowStyle::OutlineSingle) {
        if (isOutline) {
            QPointF neck = end - unit * (headLen * 0.8); double w = ann.strokeWidth/2.0 + 2;
            QPolygonF poly; poly << end << neck + perp * (headLen*0.5) << neck + perp * w << start + perp * w << start - perp * w << neck - perp * w << neck - perp * (headLen*0.5);
            p.setBrush(Qt::transparent); p.setPen(QPen(ann.color, 2)); p.drawPolygon(poly);
        } else {
            double barbWidth = 12 + ann.strokeWidth * 2.0; double waistLen = headLen * 0.75; double waistWidth = 2 + ann.strokeWidth * 0.8;
            p.setPen(Qt::NoPen); p.setBrush(ann.color);
            p.drawPolygon(QPolygonF() << end << end - unit * headLen + perp * barbWidth << end - unit * waistLen + perp * waistWidth << start << end - unit * waistLen - perp * waistWidth << end - unit * headLen - perp * barbWidth);
        }
    } else if (ann.arrowStyle == ArrowStyle::Thin) {
        p.setPen(QPen(ann.color, ann.strokeWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawLine(start, end);
        double thinAngle = 0.5;
        double thinLen = headLen * 0.85;
        p.drawLine(end, end - QPointF(thinLen * std::cos(angle - thinAngle), thinLen * std::sin(angle - thinAngle)));
        p.drawLine(end, end - QPointF(thinLen * std::cos(angle + thinAngle), thinLen * std::sin(angle + thinAngle)));
    } else if (ann.arrowStyle == ArrowStyle::SolidDouble || ann.arrowStyle == ArrowStyle::OutlineDouble) {
        p.setPen(QPen(ann.color, ann.strokeWidth)); p.drawLine(start + unit * (headLen*0.7), end - unit * (headLen*0.7));
        p.setPen(isOutline ? QPen(ann.color, 1.5) : Qt::NoPen);
        p.setBrush(isOutline ? Qt::transparent : ann.color);
        auto drawH = [&](const QPointF& e, double ang) {
            QPointF u(std::cos(ang), std::sin(ang)), pr(-u.y(), u.x());
            p.drawPolygon(QPolygonF() << e << e - u * headLen + pr * (headLen*0.5) << e - u * (headLen*0.7) + pr * (ann.strokeWidth*0.5) << e - u * (headLen*0.7) - pr * (ann.strokeWidth*0.5) << e - u * headLen - pr * (headLen*0.5));
        };
        drawH(end, angle); drawH(start, angle + M_PI);
    } else if (ann.arrowStyle == ArrowStyle::SolidDot || ann.arrowStyle == ArrowStyle::OutlineDot) {
        p.setPen(QPen(ann.color, ann.strokeWidth)); p.drawLine(start, end - unit * (headLen*0.7));
        p.setPen(isOutline ? QPen(ann.color, 1.5) : Qt::NoPen);
        p.setBrush(isOutline ? Qt::transparent : ann.color);
        p.drawEllipse(start, 4+ann.strokeWidth, 4+ann.strokeWidth);
        p.drawPolygon(QPolygonF() << end << end - unit * headLen + perp * (headLen*0.5) << end - unit * (headLen*0.7) + perp * (ann.strokeWidth*0.5) << end - unit * (headLen*0.7) - perp * (ann.strokeWidth*0.5) << end - unit * headLen - perp * (headLen*0.5));
    }
}

void ScreenshotTool::drawAnnotation(QPainter& p, const DrawingAnnotation& ann) {
    if (ann.points.size() < 2 && ann.type != ScreenshotToolType::Text && ann.type != ScreenshotToolType::Marker) return;
    p.setPen(QPen(ann.color, ann.strokeWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    if (ann.type == ScreenshotToolType::Rect) p.drawRect(QRectF(ann.points[0], ann.points[1]).normalized());
    else if (ann.type == ScreenshotToolType::Ellipse) p.drawEllipse(QRectF(ann.points[0], ann.points[1]).normalized());
    else if (ann.type == ScreenshotToolType::Line) p.drawLine(ann.points[0], ann.points[1]);
    else if (ann.type == ScreenshotToolType::Arrow) drawArrow(p, ann.points[0], ann.points[1], ann);
    else if (ann.type == ScreenshotToolType::Pen) {
        QPainterPath path; path.moveTo(ann.points[0]); for(int i=1; i<ann.points.size(); ++i) path.lineTo(ann.points[i]);
        p.drawPath(path);
    } else if (ann.type == ScreenshotToolType::Marker) {
        p.setBrush(ann.color); p.setPen(Qt::NoPen); int r = 12 + ann.strokeWidth;
        p.drawEllipse(ann.points[0], r, r); p.setPen(Qt::white); p.setFont(QFont("Arial", r, QFont::Bold));
        p.drawText(QRectF(ann.points[0].x()-r, ann.points[0].y()-r, r*2, r*2), Qt::AlignCenter, ann.text);
    } else if (ann.type == ScreenshotToolType::Mosaic || ann.type == ScreenshotToolType::MosaicRect) {
        p.save();
        if (ann.type == ScreenshotToolType::MosaicRect) {
            p.setClipRect(QRectF(ann.points[0], ann.points[1]).normalized());
        } else {
            QPainterPath path; path.moveTo(ann.points[0]);
            for(int i=1; i<ann.points.size(); ++i) path.lineTo(ann.points[i]);
            QPainterPathStroker s; s.setWidth(ann.strokeWidth * 6);
            p.setClipPath(s.createStroke(path));
        }
        p.drawPixmap(0, 0, m_mosaicPixmap); p.restore();
    } else if (ann.type == ScreenshotToolType::Text && !ann.text.isEmpty()) {
        p.setPen(ann.color); p.setFont(QFont("Microsoft YaHei", 12 + ann.strokeWidth*2, QFont::Bold));
        p.drawText(ann.points[0], ann.text);
    }
}

void ScreenshotTool::mousePressEvent(QMouseEvent* e) {
    setFocus();
    if(m_textInput->isVisible() && !m_textInput->geometry().contains(e->pos())) commitTextInput();
    if(e->button() != Qt::LeftButton) return;
    if (m_state == ScreenshotState::Selecting) {
        m_dragHandle = -1; m_startPoint = e->pos(); m_endPoint = m_startPoint; m_isDragging = true; m_toolbar->hide(); m_infoBar->hide();
    } else {
        int handle = getHandleAt(e->pos());
        if (selectionRect().contains(e->pos()) && m_currentTool != ScreenshotToolType::None && handle == -1) {
            if (m_currentTool == ScreenshotToolType::Text) { showTextInput(e->pos()); return; }
            m_isDrawing = true; m_currentAnnotation = {m_currentTool, {e->pos()}, m_currentColor, "", m_currentStrokeWidth, LineStyle::Solid, m_currentArrowStyle};
            if(m_currentTool == ScreenshotToolType::Marker) {
                int c = 1; for(auto& a: m_annotations) if(a.type == ScreenshotToolType::Marker) c++;
                m_currentAnnotation.text = QString::number(c);
            }
        } else if (handle != -1) {
            m_dragHandle = handle; m_isDragging = true;
        } else if (selectionRect().contains(e->pos())) {
            m_dragHandle = 8; m_startPoint = e->pos() - m_startPoint; m_isDragging = true;
        } else {
            m_state = ScreenshotState::Selecting; m_startPoint = e->pos(); m_endPoint = m_startPoint; m_isDragging = true; m_toolbar->hide();
        }
    }
    update();
}

void ScreenshotTool::mouseMoveEvent(QMouseEvent* e) {
    if (m_state == ScreenshotState::Selecting && !m_isDragging) {
        QRect smallest; long long minArea = -1;
        for (const QRect& r : m_detectedRects) {
            if (r.contains(e->pos())) {
                long long area = (long long)r.width() * r.height();
                if (minArea == -1 || area < minArea) { minArea = area; smallest = r; }
            }
        }
        if (m_highlightedRect != smallest) { m_highlightedRect = smallest; update(); }
    }
    if (m_isDragging) {
        QPoint p = e->pos();
        if (m_currentTool == ScreenshotToolType::Eraser) {
            bool changed = false;
            for (int i = m_annotations.size() - 1; i >= 0; --i) {
                bool hit = false;
                for (const auto& pt : m_annotations[i].points) {
                    if ((pt - p).manhattanLength() < 20) { hit = true; break; }
                }
                if (hit) { m_redoStack.append(m_annotations.takeAt(i)); changed = true; }
            }
            if (changed) update();
            return;
        }

        if (m_state == ScreenshotState::Selecting || m_dragHandle == -1) m_endPoint = e->pos();
        else if (m_dragHandle == 8) {
            QRect r = selectionRect(); r.moveTo(e->pos() - m_startPoint); m_startPoint = r.topLeft(); m_endPoint = r.bottomRight();
        } else {
            QPoint p = e->pos();
            if(m_dragHandle==0) m_startPoint = p; else if(m_dragHandle==1) m_startPoint.setY(p.y()); else if(m_dragHandle==2) { m_startPoint.setY(p.y()); m_endPoint.setX(p.x()); }
            else if(m_dragHandle==3) m_endPoint.setX(p.x()); else if(m_dragHandle==4) m_endPoint = p; else if(m_dragHandle==5) m_endPoint.setY(p.y());
            else if(m_dragHandle==6) { m_endPoint.setY(p.y()); m_startPoint.setX(p.x()); } else if(m_dragHandle==7) m_startPoint.setX(p.x());
        }
        if (!selectionRect().isEmpty()) {
            m_infoBar->updateInfo(selectionRect()); m_infoBar->show(); m_infoBar->move(selectionRect().left(), selectionRect().top() - 35); m_infoBar->raise();
            updateToolbarPosition();
        }
    } else if (m_isDrawing) {
        updateToolbarPosition();
        if (m_currentTool == ScreenshotToolType::Arrow || m_currentTool == ScreenshotToolType::Line || m_currentTool == ScreenshotToolType::Rect || m_currentTool == ScreenshotToolType::Ellipse) {
            if (m_currentAnnotation.points.size() > 1) m_currentAnnotation.points[1] = e->pos(); else m_currentAnnotation.points.append(e->pos());
        } else m_currentAnnotation.points.append(e->pos());
    } else updateCursor(e->pos());
    update();
}

void ScreenshotTool::mouseReleaseEvent(QMouseEvent* e) {
    if (m_isDrawing) { m_isDrawing = false; m_annotations.append(m_currentAnnotation); m_redoStack.clear(); }
    else if (m_isDragging) {
        m_isDragging = false;
        if (m_state == ScreenshotState::Selecting) {
            if ((e->pos() - m_startPoint).manhattanLength() < 5) {
                if (!m_highlightedRect.isEmpty()) { m_startPoint = m_highlightedRect.topLeft(); m_endPoint = m_highlightedRect.bottomRight(); }
            }
            if (selectionRect().width() > 2 && selectionRect().height() > 2) m_state = ScreenshotState::Editing;
        }
    }
    if (m_state == ScreenshotState::Editing) {
        updateToolbarPosition(); m_toolbar->show(); m_infoBar->updateInfo(selectionRect());
        m_infoBar->show(); m_infoBar->move(selectionRect().left(), selectionRect().top() - 35);
    }
    update();
}

void ScreenshotTool::updateToolbarPosition() {
    QRect r = selectionRect(); if(r.isEmpty()) return; m_toolbar->adjustSize();
    int x = r.right() - m_toolbar->width(), y = r.bottom() + 10;
    if (x < 0) x = 0; if (y + m_toolbar->height() > height()) y = r.top() - m_toolbar->height() - 10;
    m_toolbar->move(x, y); if (m_state == ScreenshotState::Editing && !m_toolbar->isVisible()) m_toolbar->show();
    if (m_toolbar->isVisible()) m_toolbar->raise();
}

void ScreenshotTool::paintEvent(QPaintEvent*) {
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
    p.drawPixmap(0,0,m_screenPixmap);
    QRect r = selectionRect(); QPainterPath path; path.addRect(rect());
    if(r.isValid()) path.addRect(r); p.fillPath(path, QColor(0,0,0,120));

    if (m_state == ScreenshotState::Selecting && !m_isDragging && !m_highlightedRect.isEmpty()) {
        p.setPen(QPen(QColor(0, 120, 255, 200), 2)); p.setBrush(QColor(0, 120, 255, 30)); p.drawRect(m_highlightedRect);
    }
    if(r.isValid()) {
        p.setPen(QPen(QColor(0, 120, 255), 2)); p.drawRect(r);
        auto h = getHandleRects(); p.setBrush(Qt::white); for(auto& hr : h) p.drawEllipse(hr);
        p.setClipRect(r); for(auto& a : m_annotations) drawAnnotation(p, a);
        if(m_isDrawing) drawAnnotation(p, m_currentAnnotation);
    }
}

void ScreenshotTool::setTool(ScreenshotToolType t) { if(m_textInput->isVisible()) commitTextInput(); m_currentTool = t; QSettings("RapidNotes", "Screenshot").setValue("tool", static_cast<int>(t)); }
void ScreenshotTool::setDrawColor(const QColor& c) { m_currentColor = c; QSettings("RapidNotes", "Screenshot").setValue("color", c); }
void ScreenshotTool::setDrawWidth(int w) { m_currentStrokeWidth = w; QSettings("RapidNotes", "Screenshot").setValue("strokeWidth", w); }
void ScreenshotTool::setArrowStyle(ArrowStyle s) { m_currentArrowStyle = s; QSettings("RapidNotes", "Screenshot").setValue("arrowStyle", static_cast<int>(s)); }

void ScreenshotTool::undo() { if(!m_annotations.isEmpty()) { m_redoStack.append(m_annotations.takeLast()); update(); } }
void ScreenshotTool::redo() { if(!m_redoStack.isEmpty()) { m_annotations.append(m_redoStack.takeLast()); update(); } }
void ScreenshotTool::copyToClipboard() { 
    QImage img = generateFinalImage();
    QApplication::clipboard()->setImage(img); 
    autoSaveImage(img);
    cancel(); 
}
void ScreenshotTool::save() { 
    QImage img = generateFinalImage();
    QString fileName = QString("RPN_%1.png").arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
    QString f = QFileDialog::getSaveFileName(this, "保存截图", fileName, "PNG(*.png)"); 
    if(!f.isEmpty()) img.save(f); 
    autoSaveImage(img);
    cancel(); 
}
void ScreenshotTool::confirm() { 
    QImage img = generateFinalImage();
    emit screenshotCaptured(img); 
    autoSaveImage(img);
    cancel(); 
}
void ScreenshotTool::pin() { QImage img = generateFinalImage(); if (img.isNull()) return; auto* widget = new PinnedScreenshotWidget(QPixmap::fromImage(img), selectionRect()); widget->show(); cancel(); }

QRect ScreenshotTool::selectionRect() const { return QRect(m_startPoint, m_endPoint).normalized(); }
QList<QRect> ScreenshotTool::getHandleRects() const {
    QRect r = selectionRect(); QList<QRect> l; if(r.isEmpty()) return l; int s = 8;
    l << QRect(r.left()-s/2, r.top()-s/2, s, s) << QRect(r.center().x()-s/2, r.top()-s/2, s, s)
      << QRect(r.right()-s/2, r.top()-s/2, s, s) << QRect(r.right()-s/2, r.center().y()-s/2, s, s)
      << QRect(r.right()-s/2, r.bottom()-s/2, s, s) << QRect(r.center().x()-s/2, r.bottom()-s/2, s, s)
      << QRect(r.left()-s/2, r.bottom()-s/2, s, s) << QRect(r.left()-s/2, r.center().y()-s/2, s, s);
    return l;
}
int ScreenshotTool::getHandleAt(const QPoint& p) const { auto l = getHandleRects(); for(int i=0; i<l.size(); ++i) if(l[i].contains(p)) return i; return -1; }
void ScreenshotTool::updateCursor(const QPoint& p) {
    if (m_state == ScreenshotState::Editing) {
        int handle = getHandleAt(p); if (handle != -1) {
            switch (handle) { case 0: case 4: setCursor(Qt::SizeFDiagCursor); break; case 1: case 5: setCursor(Qt::SizeVerCursor); break; case 2: case 6: setCursor(Qt::SizeBDiagCursor); break; case 3: case 7: setCursor(Qt::SizeHorCursor); break; }
            return;
        }
        if (selectionRect().contains(p)) { if (m_currentTool != ScreenshotToolType::None) setCursor(Qt::CrossCursor); else setCursor(Qt::SizeAllCursor); return; }
    }
    setCursor(Qt::ArrowCursor);
}
void ScreenshotTool::showTextInput(const QPoint& p) { m_textInput->move(p); m_textInput->resize(100, 30); m_textInput->show(); m_textInput->setFocus(); }
void ScreenshotTool::commitTextInput() { if(m_textInput->text().isEmpty()) { m_textInput->hide(); return; } m_annotations.append({ScreenshotToolType::Text, {m_textInput->pos()}, m_currentColor, m_textInput->text(), m_currentStrokeWidth}); m_textInput->hide(); m_textInput->clear(); update(); }

#ifdef Q_OS_WIN
BOOL CALLBACK EnumChildProc(HWND hwnd, LPARAM lParam) {
    QList<QRect>* rects = reinterpret_cast<QList<QRect>*>(lParam);
    if (IsWindowVisible(hwnd)) {
        QRect qr = getActualWindowRect(hwnd); if (qr.width() > 5 && qr.height() > 5) rects->append(qr);
    }
    return TRUE;
}
BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    QList<QRect>* rects = reinterpret_cast<QList<QRect>*>(lParam);
    if (IsWindowVisible(hwnd) && !IsIconic(hwnd)) {
        TCHAR className[256]; GetClassName(hwnd, className, 256);
        if (_tcscmp(className, _T("Qt662QWindowIcon")) == 0) return TRUE;
        int cloaked = 0; DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
        if (cloaked) return TRUE;
        QRect qr = getActualWindowRect(hwnd); if (qr.width() > 10 && qr.height() > 10) { rects->append(qr); EnumChildWindows(hwnd, EnumChildProc, lParam); }
    }
    return TRUE;
}
#endif
void ScreenshotTool::detectWindows() { m_detectedRects.clear();
#ifdef Q_OS_WIN
    EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&m_detectedRects));
    QPoint offset = mapToGlobal(QPoint(0,0)); for(QRect& r : m_detectedRects) r.translate(-offset);
#endif
}
#include "OCRResultWindow.h"
#include "../core/OCRManager.h"
void ScreenshotTool::executeOCR() {
    QImage img = generateFinalImage();
    for (QWidget* widget : QApplication::topLevelWidgets()) { if (widget->objectName() == "OCRResultWindow") widget->close(); }
    OCRResultWindow* resWin = new OCRResultWindow(img, nullptr); resWin->setObjectName("OCRResultWindow"); resWin->show();
    connect(&OCRManager::instance(), &OCRManager::recognitionFinished, resWin, &OCRResultWindow::setRecognizedText);
    OCRManager::instance().recognizeAsync(img, 9999); cancel();
}

QImage ScreenshotTool::generateFinalImage() {
    QRect r = selectionRect(); if(r.isEmpty()) return QImage();
    QPixmap p = m_screenPixmap.copy(r); QPainter painter(&p); painter.translate(-r.topLeft());
    for(auto& a : m_annotations) drawAnnotation(painter, a);
    return p.toImage();
}

void ScreenshotTool::autoSaveImage(const QImage& img) {
    if (img.isNull()) return;
    
    QSettings settings("RapidNotes", "Screenshot");
    QString defaultPath = QCoreApplication::applicationDirPath() + "/RPN_screenshot";
    QString savePath = settings.value("savePath", defaultPath).toString();
    
    QDir dir(savePath);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    
    QString fileName = QString("RPN_%1.png").arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
    QString fullPath = dir.absoluteFilePath(fileName);
    
    if (img.save(fullPath)) {
        // 使用非阻塞彩色反馈告知用户已自动保存
        QToolTip::showText(QCursor::pos(), StringUtils::wrapToolTip(QString("<span style='color: #2ecc71; font-weight: bold;'>✔ 已自动保存至:</span><br>%1")).arg(fileName));
    } else {
        QToolTip::showText(QCursor::pos(), StringUtils::wrapToolTip("<span style='color: #e74c3c; font-weight: bold;'>✖ 自动保存失败，请检查路径权限</span>"));
    }
}
void ScreenshotTool::keyPressEvent(QKeyEvent* e) { 
    if(e->key() == Qt::Key_Escape) cancel(); 
    else if(e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter || e->key() == Qt::Key_Space) { if(m_state == ScreenshotState::Editing) copyToClipboard(); }
    else if (e->modifiers() == Qt::ControlModifier && e->key() == Qt::Key_Z) undo();
    else if (e->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier) && e->key() == Qt::Key_Z) redo();
    else if (e->modifiers() == Qt::ControlModifier && e->key() == Qt::Key_O) executeOCR();
    else if (e->key() == Qt::Key_F) pin();
}
void ScreenshotTool::mouseDoubleClickEvent(QMouseEvent* e) { if(selectionRect().contains(e->pos())) confirm(); }