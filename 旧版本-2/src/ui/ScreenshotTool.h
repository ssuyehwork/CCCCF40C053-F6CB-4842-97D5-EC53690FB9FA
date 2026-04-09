#ifndef SCREENSHOTTOOL_H
#define SCREENSHOTTOOL_H

#include <QWidget>
#include <QMouseEvent>
#include <QGuiApplication>
#include <QScreen>
#include <QPixmap>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMap>
#include <QLineEdit>
#include <QButtonGroup>
#include <QMenu>
#include <QColorDialog>
#include <QList>
#include <functional>

enum class ScreenshotState { Selecting, Editing };
enum class ScreenshotToolType { None, Rect, Ellipse, Arrow, Line, Pen, Marker, Text, Mosaic, MosaicRect, Eraser };
enum class ArrowStyle { 
    SolidSingle, OutlineSingle, 
    SolidDouble, OutlineDouble, 
    SolidDot, OutlineDot,
    Thin 
};
enum class LineStyle { Solid, Dash, Dot };

struct DrawingAnnotation {
    ScreenshotToolType type;
    QList<QPointF> points;
    QColor color;
    QString text;
    int strokeWidth;
    LineStyle lineStyle;
    ArrowStyle arrowStyle;
};

class ScreenshotTool;
class ScreenshotToolbar;

class PinnedScreenshotWidget : public QWidget {
    Q_OBJECT
public:
    explicit PinnedScreenshotWidget(const QPixmap& pixmap, const QRect& screenRect, QWidget* parent = nullptr);
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void contextMenuEvent(QContextMenuEvent*) override;
private:
    QPixmap m_pixmap;
    QPoint m_dragPos;
};

class ScreenshotToolbar : public QWidget {
    Q_OBJECT
public:
    explicit ScreenshotToolbar(ScreenshotTool* tool);
    void addToolButton(QBoxLayout* layout, ScreenshotToolType type, const QString& iconType, const QString& tip);
    void addActionButton(QBoxLayout* layout, const QString& iconName, const QString& tip, std::function<void()> func);
    void selectTool(ScreenshotToolType type);
    void updateArrowButtonIcon(ArrowStyle style);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void createOptionWidget();
    void showArrowMenu();

public:
    ScreenshotTool* m_tool;
    QMap<ScreenshotToolType, QPushButton*> m_buttons;
    QWidget* m_optionWidget = nullptr;
    QPushButton* m_arrowStyleBtn = nullptr;
    bool m_isDragging = false;
    QPoint m_dragPosition;
};

class SelectionInfoBar : public QWidget {
    Q_OBJECT
    friend class ScreenshotToolbar;
public:
    explicit SelectionInfoBar(QWidget* parent = nullptr);
    void updateInfo(const QRect& rect);
protected:
    void paintEvent(QPaintEvent*) override;
private:
    QString m_text;
};

class ScreenshotTool : public QWidget {
    Q_OBJECT
    friend class ScreenshotToolbar;
public:
    explicit ScreenshotTool(QWidget* parent = nullptr);
    
    void setDrawColor(const QColor& color);
    void setDrawWidth(int width);
    void setArrowStyle(ArrowStyle style);
    
    void updateToolbarPosition();
    void undo();
    void redo();
    void copyToClipboard();
    void save();
    void confirm();
    void pin();
    void cancel(); 
    void executeOCR();

signals:
    void screenshotCaptured(const QImage& image);
    void screenshotCanceled();

protected:
    void paintEvent(QPaintEvent*) override;
    void showEvent(QShowEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

public slots:
    void setTool(ScreenshotToolType type);

private:
    void drawAnnotation(QPainter& painter, const DrawingAnnotation& ann);
    void drawArrow(QPainter& painter, const QPointF& start, const QPointF& end, const DrawingAnnotation& ann);
    
    QRect selectionRect() const;
    QList<QRect> getHandleRects() const;
    int getHandleAt(const QPoint& pos) const;
    void updateCursor(const QPoint& pos);
    void showTextInput(const QPoint& pos);
    void commitTextInput();
    QImage generateFinalImage();
    void autoSaveImage(const QImage& img);
    void detectWindows();

    QPixmap m_screenPixmap;
    QPixmap m_mosaicPixmap;
    
    ScreenshotState m_state = ScreenshotState::Selecting;
    ScreenshotToolType m_currentTool = ScreenshotToolType::None;
    
    QList<QRect> m_detectedRects;
    QRect m_highlightedRect;

    QPoint m_startPoint, m_endPoint;
    bool m_isDragging = false;
    int m_dragHandle = -1; 

    QList<DrawingAnnotation> m_annotations;
    QList<DrawingAnnotation> m_redoStack;
    DrawingAnnotation m_currentAnnotation;
    bool m_isDrawing = false;

    ScreenshotToolbar* m_toolbar = nullptr;
    SelectionInfoBar* m_infoBar = nullptr;
    QLineEdit* m_textInput = nullptr;

    QColor m_currentColor = QColor(255, 50, 50); 
    int m_currentStrokeWidth = 3; 
    ArrowStyle m_currentArrowStyle = ArrowStyle::SolidSingle;
    LineStyle m_currentLineStyle = LineStyle::Solid;
};

#endif // SCREENSHOTTOOL_H
