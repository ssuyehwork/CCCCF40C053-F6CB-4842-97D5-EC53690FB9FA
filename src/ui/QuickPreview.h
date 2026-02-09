#ifndef QUICKPREVIEW_H
#define QUICKPREVIEW_H

#include <QWidget>
#include "StringUtils.h"

#include <QTextEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QKeyEvent>
#include <QGraphicsDropShadowEffect>
#include <QMouseEvent>
#include <QApplication>
#include <QClipboard>
#include <QToolTip>
#include <QCursor>
#include <QFrame>
#include <QShortcut>
#include <QAction>
#include <QScreen>
#include <QGuiApplication>
#include "IconHelper.h"

class QuickPreview : public QWidget {
    Q_OBJECT
signals:
    void editRequested(int noteId);
    void prevRequested();
    void nextRequested();

public:
    explicit QuickPreview(QWidget* parent = nullptr) : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint) {
        setObjectName("QuickPreview");
        setAttribute(Qt::WA_TranslucentBackground);
        setFocusPolicy(Qt::StrongFocus);
        
        auto* mainLayout = new QVBoxLayout(this);
        // [CRITICAL] 边距调整为 20px 以容纳阴影，防止出现“断崖式”阴影截止
        mainLayout->setContentsMargins(20, 20, 20, 20);

        m_container = new QFrame();
        m_container->setObjectName("previewContainer");
        m_container->setStyleSheet(
            "QFrame#previewContainer { background-color: #1e1e1e; border: 1px solid #444; border-radius: 8px; }"
            "QFrame#previewTitleBar { background-color: #1e1e1e; border-top-left-radius: 7px; border-top-right-radius: 7px; border-bottom: 1px solid #333; }"
            "QTextEdit { border-bottom-left-radius: 7px; border-bottom-right-radius: 7px; background: transparent; border: none; color: #ddd; font-size: 14px; padding: 10px; }"
            "QPushButton { border: none; border-radius: 4px; background: transparent; padding: 4px; }"
            "QPushButton:hover { background-color: #3e3e42; }"
            "QPushButton#btnClose:hover { background-color: #E81123; }"
        );
        
        auto* containerLayout = new QVBoxLayout(m_container);
        containerLayout->setContentsMargins(0, 0, 0, 0);
        containerLayout->setSpacing(0);

        // --- 标题栏 ---
        m_titleBar = new QFrame();
        m_titleBar->setObjectName("previewTitleBar");
        m_titleBar->setFixedHeight(36);
        m_titleBar->setAttribute(Qt::WA_StyledBackground);
        auto* titleLayout = new QHBoxLayout(m_titleBar);
        titleLayout->setContentsMargins(10, 0, 5, 0);
        titleLayout->setSpacing(5);

        QLabel* titleLabel = new QLabel("预览");
        titleLabel->setStyleSheet("color: #888; font-size: 12px; font-weight: bold;");
        titleLayout->addWidget(titleLabel);
        titleLayout->addStretch();

        auto createBtn = [this](const QString& icon, const QString& tooltip, const QString& objName = "") {
            QPushButton* btn = new QPushButton();
            btn->setIcon(IconHelper::getIcon(icon, "#aaaaaa"));
            btn->setIconSize(QSize(16, 16));
            btn->setFixedSize(32, 32);
            btn->setToolTip(StringUtils::wrapToolTip(QString("%1").arg(tooltip)));
            if (!objName.isEmpty()) btn->setObjectName(objName);
            return btn;
        };

        QPushButton* btnPrev = createBtn("nav_prev", "上一个 (Alt+Up)");
        btnPrev->setFocusPolicy(Qt::NoFocus);
        QPushButton* btnNext = createBtn("nav_next", "下一个 (Alt+Down)");
        btnNext->setFocusPolicy(Qt::NoFocus);
        QPushButton* btnCopy = createBtn("copy", "复制内容 (Ctrl+C)");
        btnCopy->setFocusPolicy(Qt::NoFocus);
        m_btnPin = createBtn("pin", "置顶显示");
        m_btnPin->setFocusPolicy(Qt::NoFocus);

        QPushButton* btnEdit = createBtn("edit", "编辑 (Ctrl+B)");
        btnEdit->setFocusPolicy(Qt::NoFocus);
        QPushButton* btnMin = createBtn("minimize", "最小化");
        btnMin->setFocusPolicy(Qt::NoFocus);
        QPushButton* btnMax = createBtn("maximize", "最大化");
        btnMax->setFocusPolicy(Qt::NoFocus);
        QPushButton* btnClose = createBtn("close", "关闭", "btnClose");
        btnClose->setFocusPolicy(Qt::NoFocus);

        connect(btnPrev, &QPushButton::clicked, this, &QuickPreview::prevRequested);
        connect(btnNext, &QPushButton::clicked, this, &QuickPreview::nextRequested);
        connect(btnCopy, &QPushButton::clicked, [this]() {
            // 仅复制正文内容，不包含预览窗口添加的标题和分割线
            if (m_pureContent.isEmpty()) {
                QApplication::clipboard()->setText(m_textEdit->toPlainText());
            } else {
                // 如果是 HTML 且包含 <html> 标签，复制为富文本
                if (m_pureContent.contains("<html", Qt::CaseInsensitive)) {
                    QMimeData* mime = new QMimeData();
                    mime->setHtml(m_pureContent);
                    mime->setText(StringUtils::htmlToPlainText(m_pureContent));
                    QApplication::clipboard()->setMimeData(mime);
                } else {
                    QApplication::clipboard()->setText(m_pureContent);
                }
            }
            QToolTip::showText(QCursor::pos(), StringUtils::wrapToolTip("<b style='color: #2ecc71;'>✔ 内容已复制到剪贴板</b>"));
        });
        connect(m_btnPin, &QPushButton::clicked, [this]() {
            m_isPinned = !m_isPinned;
            setWindowFlag(Qt::WindowStaysOnTopHint, m_isPinned);
            m_btnPin->setIcon(IconHelper::getIcon("pin", m_isPinned ? "#2ecc71" : "#aaaaaa"));
            show(); // 改变 flag 后需要 show 出来
        });

        connect(btnEdit, &QPushButton::clicked, [this]() {
            emit editRequested(m_currentNoteId);
        });
        connect(btnMin, &QPushButton::clicked, this, &QuickPreview::showMinimized);
        connect(btnMax, &QPushButton::clicked, [this]() {
            if (isMaximized()) showNormal();
            else showMaximized();
        });
        connect(btnClose, &QPushButton::clicked, this, &QuickPreview::hide);

        titleLayout->addWidget(btnPrev);
        titleLayout->addWidget(btnNext);
        titleLayout->addSpacing(5);
        titleLayout->addWidget(btnCopy);
        titleLayout->addWidget(m_btnPin);
        titleLayout->addSpacing(5);
        titleLayout->addWidget(btnEdit);
        titleLayout->addWidget(btnMin);
        titleLayout->addWidget(btnMax);
        titleLayout->addWidget(btnClose);

        containerLayout->addWidget(m_titleBar);

        m_textEdit = new QTextEdit();
        m_textEdit->setReadOnly(true);
        m_textEdit->setFocusPolicy(Qt::NoFocus); // 防止拦截空格键
        containerLayout->addWidget(m_textEdit);
        
        mainLayout->addWidget(m_container);
        
        auto* shadow = new QGraphicsDropShadowEffect(this);
        // [CRITICAL] 阴影模糊半径设为 20，配合 20px 的边距可确保阴影平滑过渡不被裁剪
        shadow->setBlurRadius(20);
        shadow->setColor(QColor(0, 0, 0, 120));
        shadow->setOffset(0, 4);
        m_container->setGraphicsEffect(shadow);
        
        resize(920, 720);

    }

    void showPreview(int noteId, const QString& title, const QString& content, const QPoint& pos) {
        showPreview(noteId, title, content, "text", QByteArray(), pos);
    }

    bool isPinned() const { return m_isPinned; }

    void showPreview(int noteId, const QString& title, const QString& content, const QString& type, const QByteArray& data, const QPoint& pos) {
        m_currentNoteId = noteId;
        m_pureContent = content; // 记忆原始纯净内容
        QString html;
        QString titleHtml = QString("<h3 style='color: #eee; margin-bottom: 5px;'>%1</h3>").arg(title.toHtmlEscaped());
        QString hrHtml = "<hr style='border: 0; border-top: 1px solid #444; margin: 10px 0;'>";

        if (type == "image" && !data.isEmpty()) {
            html = QString("%1%2<div style='text-align: center;'><img src='data:image/png;base64,%3' width='450'></div>")
                   .arg(titleHtml, hrHtml, QString(data.toBase64()));
        } else {
            // 判定是否已经是 HTML
            QString trimmed = content.trimmed();
            bool isHtml = trimmed.startsWith("<!DOCTYPE", Qt::CaseInsensitive) || 
                          trimmed.startsWith("<html", Qt::CaseInsensitive) || 
                          trimmed.contains("<style", Qt::CaseInsensitive) ||
                          Qt::mightBeRichText(content);
            
            QString body;
            if (isHtml) {
                body = content; // 直接使用 HTML
            } else {
                body = content.toHtmlEscaped();
                body.replace("\n", "<br>");
                body = QString("<div style='line-height: 1.6; color: #ccc; font-size: 13px;'>%1</div>").arg(body);
            }
            html = QString("%1%2%3").arg(titleHtml, hrHtml, body);
        }
        m_textEdit->setHtml(html);
        
        // 边缘检测：确保预览窗口不超出当前屏幕
        QPoint adjustedPos = pos;
        QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
        if (!screen) screen = QGuiApplication::primaryScreen();
        if (screen) {
            QRect screenGeom = screen->availableGeometry();
            if (adjustedPos.x() + width() > screenGeom.right()) adjustedPos.setX(screenGeom.right() - width());
            if (adjustedPos.x() < screenGeom.left()) adjustedPos.setX(screenGeom.left());
            if (adjustedPos.y() + height() > screenGeom.bottom()) adjustedPos.setY(screenGeom.bottom() - height());
            if (adjustedPos.y() < screenGeom.top()) adjustedPos.setY(screenGeom.top());
        }

        move(adjustedPos);
        show();
    }

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && m_titleBar->rect().contains(m_titleBar->mapFrom(this, event->pos()))) {
            m_dragging = true;
            m_dragPos = event->globalPosition().toPoint() - frameGeometry().topLeft();
            event->accept();
        }
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (m_dragging && event->buttons() & Qt::LeftButton) {
            move(event->globalPosition().toPoint() - m_dragPos);
            event->accept();
        }
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        m_dragging = false;
        QWidget::mouseReleaseEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override {
        if (m_titleBar->rect().contains(m_titleBar->mapFrom(this, event->pos()))) {
            if (isMaximized()) showNormal();
            else showMaximized();
            event->accept();
        }
    }

protected:
    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Space || event->key() == Qt::Key_Escape) {
            hide();
            event->accept();
            return;
        }
        if (event->modifiers() & Qt::AltModifier) {
            if (event->key() == Qt::Key_Up) {
                emit prevRequested();
                event->accept();
                return;
            } else if (event->key() == Qt::Key_Down) {
                emit nextRequested();
                event->accept();
                return;
            }
        }
        if (event->modifiers() & Qt::ControlModifier && event->key() == Qt::Key_C) {
            // 同步按钮逻辑
            if (!m_pureContent.isEmpty()) {
                if (m_pureContent.contains("<html", Qt::CaseInsensitive)) {
                    QMimeData* mime = new QMimeData();
                    mime->setHtml(m_pureContent);
                    mime->setText(StringUtils::htmlToPlainText(m_pureContent));
                    QApplication::clipboard()->setMimeData(mime);
                } else {
                    QApplication::clipboard()->setText(m_pureContent);
                }
            } else {
                QApplication::clipboard()->setText(m_textEdit->toPlainText());
            }
            QToolTip::showText(QCursor::pos(), StringUtils::wrapToolTip("<b style='color: #2ecc71;'>✔ 内容已复制到剪贴板</b>"));
            event->accept();
            return;
        }
        QWidget::keyPressEvent(event);
    }

private:
    QFrame* m_container;
    QWidget* m_titleBar;
    QTextEdit* m_textEdit;
    QString m_pureContent; // 纯净内容暂存
    int m_currentNoteId = -1;
    bool m_dragging = false;
    bool m_isPinned = false;
    QPushButton* m_btnPin = nullptr;
    QPoint m_dragPos;
};

#endif // QUICKPREVIEW_H
