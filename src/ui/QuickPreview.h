#include "ToolTipOverlay.h"
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
#include <QKeyEvent>
#include <QElapsedTimer>
#include <QTextDocument>
#include <QTextCursor>
#include <QSettings>
#include "ToolTipOverlay.h"
#include <QCursor>
#include <QFrame>
#include <QShortcut>
#include <QAction>
#include <QScreen>
#include <QGuiApplication>
#include <QLineEdit>
#include "IconHelper.h"
#include "../core/ShortcutManager.h"

class QuickPreview : public QWidget {
    Q_OBJECT
signals:
    void editRequested(int noteId);
    void prevRequested();
    void nextRequested();
    void historyNavigationRequested(int noteId);

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

        m_titleLabel = new QLabel("预览");
        m_titleLabel->setStyleSheet("color: #888; font-size: 12px; font-weight: bold;");
        titleLayout->addWidget(m_titleLabel);

        // --- 集成搜索框 (增强视觉对比度) ---
        m_searchEdit = new QLineEdit();
        m_searchEdit->setFocusPolicy(Qt::ClickFocus); // [UX] 防止打开预览窗时自动夺取焦点，仅在点击或快捷键激活时获焦
        m_searchEdit->setPlaceholderText("查找内容...");
        m_searchEdit->setFixedWidth(250); // 增加宽度
        
        // 使用更明显的样式，添加内联搜索图标
        QAction* searchAction = new QAction(this);
        searchAction->setIcon(IconHelper::getIcon("search", "#888888"));
        m_searchEdit->addAction(searchAction, QLineEdit::LeadingPosition);
        
        m_searchEdit->setStyleSheet(
            "QLineEdit {"
            "  background-color: #2d2d2d; color: #eee; border: 1px solid #555; border-radius: 6px;"
            "  padding: 2px 10px; font-size: 12px;"
            "}"
            "QLineEdit:focus {"
            "  background-color: #383838; border-color: #007acc; color: #fff;"
            "}"
            "QLineEdit::placeholder { color: #666; }"
        );
        titleLayout->addSpacing(20);
        titleLayout->addWidget(m_searchEdit);

        m_searchCountLabel = new QLabel("0 / 0");
        m_searchCountLabel->setStyleSheet("color: #007acc; font-size: 11px; font-weight: bold; margin-left: 5px;");
        titleLayout->addWidget(m_searchCountLabel);

        titleLayout->addStretch();

        auto createBtn = [this](const QString& icon, const QString& tooltip, const QString& objName = "") {
            QPushButton* btn = new QPushButton();
            btn->setIcon(IconHelper::getIcon(icon, "#aaaaaa"));
            btn->setIconSize(QSize(16, 16));
            btn->setFixedSize(32, 32);
            btn->setToolTip(tooltip);
            if (!objName.isEmpty()) btn->setObjectName(objName);
            return btn;
        };

        m_btnBack = createBtn("nav_first", "后退 (Alt+Left)");
        m_btnBack->setFocusPolicy(Qt::NoFocus);
        m_btnForward = createBtn("nav_last", "前进 (Alt+Right)");
        m_btnForward->setFocusPolicy(Qt::NoFocus);

        QPushButton* btnPrev = createBtn("nav_prev", "上一个 (Alt+Up)");
        btnPrev->setFocusPolicy(Qt::NoFocus);
        QPushButton* btnNext = createBtn("nav_next", "下一个 (Alt+Down)");
        btnNext->setFocusPolicy(Qt::NoFocus);
        QPushButton* btnCopy = createBtn("copy", "复制内容 (Ctrl+C)");
        btnCopy->setFocusPolicy(Qt::NoFocus);
        m_btnPin = createBtn("pin_tilted", "置顶显示");
        m_btnPin->setCheckable(true);
        m_btnPin->setFocusPolicy(Qt::NoFocus);
        
        // 加载记忆状态
        QSettings settings("RapidNotes", "WindowStates");
        m_isPinned = settings.value("QuickPreview/StayOnTop", false).toBool();
        if (m_isPinned) {
            m_btnPin->setChecked(true);
            m_btnPin->setIcon(IconHelper::getIcon("pin_vertical", "#ffffff"));
            setWindowFlag(Qt::WindowStaysOnTopHint, true);
        }

        QPushButton* btnEdit = createBtn("edit", "编辑 (Ctrl+B)");
        btnEdit->setFocusPolicy(Qt::NoFocus);
        QPushButton* btnMin = createBtn("minimize", "最小化");
        btnMin->setFocusPolicy(Qt::NoFocus);
        QPushButton* btnMax = createBtn("maximize", "最大化");
        btnMax->setFocusPolicy(Qt::NoFocus);
        QPushButton* btnClose = createBtn("close", "关闭", "btnClose");
        btnClose->setFocusPolicy(Qt::NoFocus);

        connect(m_btnBack, &QPushButton::clicked, this, &QuickPreview::navigateBack);
        connect(m_btnForward, &QPushButton::clicked, this, &QuickPreview::navigateForward);

        connect(btnPrev, &QPushButton::clicked, this, &QuickPreview::prevRequested);
        connect(btnNext, &QPushButton::clicked, this, &QuickPreview::nextRequested);
        connect(btnCopy, &QPushButton::clicked, this, &QuickPreview::copyFullContent);
        connect(m_btnPin, &QPushButton::toggled, [this](bool checked) {
            m_isPinned = checked;
            setWindowFlag(Qt::WindowStaysOnTopHint, m_isPinned);
            m_btnPin->setIcon(IconHelper::getIcon(m_isPinned ? "pin_vertical" : "pin_tilted", m_isPinned ? "#ffffff" : "#aaaaaa"));
            
            // 持久化记忆
            QSettings settings("RapidNotes", "WindowStates");
            settings.setValue("QuickPreview/StayOnTop", m_isPinned);
            
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

        titleLayout->addWidget(m_btnBack);
        titleLayout->addWidget(m_btnForward);
        titleLayout->addSpacing(5);
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

        connect(m_searchEdit, &QLineEdit::textChanged, this, &QuickPreview::performSearch);
        connect(m_searchEdit, &QLineEdit::returnPressed, this, &QuickPreview::findNext);

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

        setupShortcuts();
        connect(&ShortcutManager::instance(), &ShortcutManager::shortcutsChanged, this, &QuickPreview::updateShortcuts);
    }

    void showPreview(int noteId, const QString& title, const QString& content, const QPoint& pos, const QString& catName = "") {
        showPreview(noteId, title, content, "text", QByteArray(), pos, catName);
    }

    bool isPinned() const { return m_isPinned; }

    void showPreview(int noteId, const QString& title, const QString& content, const QString& type, const QByteArray& data, const QPoint& pos, const QString& catName = "") {
        m_currentNoteId = noteId;
        // 每次显示新笔记时重置搜索状态
        if (m_searchEdit) {
            m_searchEdit->clear();
        }
        addToHistory(noteId);
        if (!catName.isEmpty()) {
            m_titleLabel->setText(QString("预览 - %1").arg(catName));
        } else {
            m_titleLabel->setText("预览");
        }
        m_pureContent = content; // 记忆原始纯净内容
        QString html;
        QString titleHtml = QString("<h3 style='color: #eee; margin-bottom: 5px;'>%1</h3>").arg(title.toHtmlEscaped());
        QString hrHtml = "<hr style='border: 0; border-top: 1px solid #444; margin: 10px 0;'>";

        if (type == "color") {
            html = QString("%1%2"
                           "<div style='margin: 20px; text-align: center;'>"
                           "  <div style='background-color: %3; width: 100%; height: 200px; border-radius: 12px; border: 1px solid #555;'></div>"
                           "  <h1 style='color: white; margin-top: 20px; font-family: Consolas; font-size: 32px;'>%3</h1>"
                           "</div>")
                   .arg(titleHtml, hrHtml, content);
        } else if (type == "image" && !data.isEmpty()) {
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
        setFocus(); // [UX] 确保预览窗口打开后，焦点在窗口本身而非搜索框
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

    void setupShortcuts() {
        auto add = [&](const QString& id, std::function<void()> func) {
            auto* sc = new QShortcut(ShortcutManager::instance().getShortcut(id), this, func);
            sc->setProperty("id", id);
            m_shortcuts.append(sc);
        };

        add("pv_prev", [this](){ emit prevRequested(); });
        add("pv_next", [this](){ emit nextRequested(); });
        add("pv_back", [this](){ navigateBack(); });
        add("pv_forward", [this](){ navigateForward(); });
        add("pv_edit", [this](){ emit editRequested(m_currentNoteId); });
        add("pv_copy", [this](){
            if (m_searchEdit && m_searchEdit->hasFocus()) {
                m_searchEdit->copy();
            } else {
                m_textEdit->copy();
            }
        });
        add("pv_close", [this](){ hide(); });
        add("pv_search", [this](){ toggleSearch(true); });

        new QShortcut(QKeySequence("Escape"), this, [this](){ hide(); });
    }

    void updateShortcuts() {
        for (auto* sc : m_shortcuts) {
            QString id = sc->property("id").toString();
            sc->setKey(ShortcutManager::instance().getShortcut(id));
        }
    }

    void addToHistory(int noteId) {
        if (m_isNavigatingHistory) return;
        
        // 只有当新 ID 与当前历史位置 ID 不同时才记录
        if (!m_history.isEmpty() && m_historyIndex >= 0 && m_historyIndex < m_history.size()) {
            if (m_history.at(m_historyIndex) == noteId) return;
        }

        // 如果我们在历史中间进行了新操作，截断之后的前进记录
        while (m_historyIndex < m_history.size() - 1) {
            m_history.removeLast();
        }

        m_history.append(noteId);
        m_historyIndex = m_history.size() - 1;
        updateHistoryButtons();
    }

    void navigateBack() {
        if (m_historyIndex > 0) {
            m_historyIndex--;
            m_isNavigatingHistory = true;
            emit historyNavigationRequested(m_history.at(m_historyIndex));
            m_isNavigatingHistory = false;
            updateHistoryButtons();
        }
    }

    void navigateForward() {
        if (m_historyIndex < m_history.size() - 1) {
            m_historyIndex++;
            m_isNavigatingHistory = true;
            emit historyNavigationRequested(m_history.at(m_historyIndex));
            m_isNavigatingHistory = false;
            updateHistoryButtons();
        }
    }

    void toggleSearch(bool show) {
        if (show) {
            m_searchEdit->setFocus();
            m_searchEdit->selectAll();
            if (!m_searchEdit->text().isEmpty()) {
                performSearch(m_searchEdit->text());
            }
        } else {
            // [UX] 关闭搜索时清空输入并取消高亮，让正文重获焦点
            m_searchEdit->clear();
            m_searchEdit->clearFocus();
            QList<QTextEdit::ExtraSelection> empty;
            m_textEdit->setExtraSelections(empty);
            m_textEdit->setFocus();
            if (m_searchCountLabel) m_searchCountLabel->setText("0 / 0");
        }
    }

    void performSearch(const QString& text) {
        if (text.isEmpty()) {
            if (m_searchCountLabel) m_searchCountLabel->setText("0/0");
            m_textEdit->setExtraSelections({});
            return;
        }

        QList<QTextEdit::ExtraSelection> selections;
        
        // 记录当前位置
        QTextCursor originalCursor = m_textEdit->textCursor();
        
        m_textEdit->moveCursor(QTextCursor::Start);
        
        QColor color = QColor(255, 255, 0, 100);
        
        int count = 0;
        while (m_textEdit->find(text)) {
            count++;
            QTextEdit::ExtraSelection selection;
            selection.format.setBackground(color);
            selection.cursor = m_textEdit->textCursor();
            selections.append(selection);
        }
        
        m_textEdit->setExtraSelections(selections);
        m_textEdit->setTextCursor(originalCursor);
        
        updateSearchCount();
    }

    void findNext() {
        QString text = m_searchEdit->text();
        if (text.isEmpty()) return;
        
        if (!m_textEdit->find(text)) {
            // 如果到底了，回到开头继续找
            m_textEdit->moveCursor(QTextCursor::Start);
            m_textEdit->find(text);
        }
        updateSearchCount();
    }

    void findPrev() {
        QString text = m_searchEdit->text();
        if (text.isEmpty()) return;
        
        if (!m_textEdit->find(text, QTextDocument::FindBackward)) {
            // 如果到头了，回到末尾继续找
            m_textEdit->moveCursor(QTextCursor::End);
            m_textEdit->find(text, QTextDocument::FindBackward);
        }
        updateSearchCount();
    }

    void updateSearchCount() {
        QString text = m_searchEdit->text();
        if (text.isEmpty() || !m_searchCountLabel) return;

        QTextCursor currentCursor = m_textEdit->textCursor();
        int total = m_textEdit->extraSelections().size();
        int current = 0;
        
        // 如果没有匹配项
        if (total == 0) {
            m_searchCountLabel->setText("0/0");
            return;
        }

        // 计算当前位置是第几个匹配项
        QTextDocument* doc = m_textEdit->document();
        QTextCursor tempCursor(doc);
        while (!(tempCursor = doc->find(text, tempCursor)).isNull()) {
            current++;
            if (tempCursor.selectionEnd() >= currentCursor.selectionEnd()) {
                break;
            }
        }
        
        m_searchCountLabel->setText(QString("%1/%2").arg(current).arg(total));
    }

    void copyFullContent() {
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
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #2ecc71;'>✔ 全部正文已提取到剪贴板</b>");
    }

    void updateHistoryButtons() {
        if (m_btnBack) m_btnBack->setEnabled(m_historyIndex > 0);
        if (m_btnForward) m_btnForward->setEnabled(m_historyIndex < m_history.size() - 1);
        
        if (m_btnBack) m_btnBack->setIcon(IconHelper::getIcon("nav_first", m_historyIndex > 0 ? "#aaaaaa" : "#444444"));
        if (m_btnForward) m_btnForward->setIcon(IconHelper::getIcon("nav_last", m_historyIndex < m_history.size() - 1 ? "#aaaaaa" : "#444444"));
    }

protected:
    void keyPressEvent(QKeyEvent* event) override {
        // [PROFESSIONAL FIX] 再次按下空格时彻底关闭并接受事件，防止冒泡回父窗口导致重新打开
        if (event->key() == Qt::Key_Space) {
            // 防抖
            static QElapsedTimer spaceTimer;
            if (spaceTimer.isValid() && spaceTimer.elapsed() < 200) return;
            spaceTimer.restart();

            QWidget* focus = QApplication::focusWidget();
            if (auto* le = qobject_cast<QLineEdit*>(focus)) {
                if (!le->isReadOnly()) {
                    QWidget::keyPressEvent(event);
                    return;
                }
            }
            hide();
            event->accept(); // 关键：接受事件，阻止进一步传播
            return;
        }

        // [CRITICAL] 优先响应搜索框的交互逻辑
        if (event->key() == Qt::Key_Escape) {
            // 如果搜索框内有文字或正在输入，则优先清空/退出搜索状态
            if (m_searchEdit && (m_searchEdit->hasFocus() || !m_searchEdit->text().isEmpty())) {
                toggleSearch(false);
            } else {
                hide();
            }
            event->accept();
            return;
        }
        
        // [CRITICAL] 显式拦截 Ctrl+W 确保在任何焦点状态下都能快速关闭预览窗口
        if (event->key() == Qt::Key_W && (event->modifiers() & Qt::ControlModifier)) {
            hide();
            event->accept();
            return;
        }
        
        QWidget::keyPressEvent(event);
    }

    void hideEvent(QHideEvent* event) override {
        // 窗口关闭时，将焦点还给父窗口（列表），方便用户继续使用键盘导航
        if (parentWidget()) {
            parentWidget()->activateWindow();
            parentWidget()->setFocus();
        }
        QWidget::hideEvent(event);
    }

private:
    QFrame* m_container;
    QList<QShortcut*> m_shortcuts;
    QWidget* m_titleBar;
    QLabel* m_titleLabel;
    QLineEdit* m_searchEdit = nullptr;
    QLabel* m_searchCountLabel = nullptr;
    QTextEdit* m_textEdit;
    QString m_pureContent; // 纯净内容暂存
    int m_currentNoteId = -1;
    bool m_dragging = false;
    bool m_isPinned = false;
    QPushButton* m_btnPin = nullptr;
    QPushButton* m_btnBack = nullptr;
    QPushButton* m_btnForward = nullptr;
    QPoint m_dragPos;

    QList<int> m_history;
    int m_historyIndex = -1;
    bool m_isNavigatingHistory = false;
};

#endif // QUICKPREVIEW_H
