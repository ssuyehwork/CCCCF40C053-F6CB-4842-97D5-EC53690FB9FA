#ifndef QUICKPREVIEW_H
#define QUICKPREVIEW_H

#include <QWidget>
#include "StringUtils.h"

#include <QTextEdit>
#include <QVariant>
#include <QStringList>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QKeyEvent>
#include <QGraphicsDropShadowEffect>
#include <QMouseEvent>
#include <QApplication>
#include <QClipboard>
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
#include <QSplitter>
#include "IconHelper.h"
#include "../core/ShortcutManager.h"
#include "../core/DatabaseManager.h"
#include <QMimeData>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

class QuickPreview : public QWidget {
  Q_OBJECT
public:
  static QuickPreview* instance() {
    static QuickPreview* inst = nullptr;
    if (!inst) {
      inst = new QuickPreview();
    }
    return inst;
  }

  QWidget* caller() const { return m_focusBackWidget; }

signals:
  void editRequested(int noteId);
  void prevRequested();
  void nextRequested();
  void historyNavigationRequested(int noteId);

private:
  explicit QuickPreview(QWidget* parent = nullptr) : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint) {
    setObjectName("QuickPreview");
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::StrongFocus);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);

    m_container = new QFrame();
    m_container->setObjectName("previewContainer");
    m_container->setStyleSheet(
      "QFrame#previewContainer { background-color: #1e1e1e; border: 1px solid #444; border-radius: 8px; }"
      "QFrame#previewTitleBar { background-color: #1e1e1e; border-top-left-radius: 7px; border-top-right-radius: 7px; border-bottom: 1px solid #333; }"
      "QTextEdit { border-bottom-left-radius: 7px; border-bottom-right-radius: 7px; background: transparent; border: none; color: #ddd; padding: 10px; }" // 移除了 font-size 锁定
      "QPushButton { border: none; border-radius: 4px; background: transparent; padding: 4px; }"
      "QPushButton:hover { background-color: #3e3e42; }"
      "QPushButton:checked { background-color: #FF551C; }"
      "QPushButton#btnClose:hover { background-color: #E81123; }"
    );

    auto* containerLayout = new QVBoxLayout(m_container);
    containerLayout->setContentsMargins(0, 0, 0, 0);
    containerLayout->setSpacing(0);

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

    m_searchEdit = new QLineEdit();
    m_searchEdit->setFocusPolicy(Qt::StrongFocus);
    m_searchEdit->setPlaceholderText("查找内容...");
    m_searchEdit->setFixedWidth(250);

    QAction* searchAction = new QAction(this);
    searchAction->setIcon(IconHelper::getIcon("search", "#888888"));
    m_searchEdit->addAction(searchAction, QLineEdit::LeadingPosition);

    m_searchEdit->setStyleSheet(
      "QLineEdit {"
      " background-color: #2d2d2d; color: #eee; border: 1px solid #555; border-radius: 6px;"
      " padding: 2px 10px; font-size: 12px;"
      "}"
      "QLineEdit:focus {"
      " background-color: #383838; border-color: #007acc; color: #fff;"
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
#ifdef Q_OS_WIN
      HWND hwnd = (HWND)winId();
      SetWindowPos(hwnd, checked ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
#else
      setWindowFlag(Qt::WindowStaysOnTopHint, m_isPinned);
      show();
#endif
      m_btnPin->setIcon(IconHelper::getIcon(m_isPinned ? "pin_vertical" : "pin_tilted", m_isPinned ? "#ffffff" : "#aaaaaa"));
      QSettings settings("RapidNotes", "WindowStates");
      settings.setValue("QuickPreview/StayOnTop", m_isPinned);
    });

    connect(btnEdit, &QPushButton::clicked, [this]() {
      emit editRequested(m_currentNoteId);
    });
    connect(btnMin, &QPushButton::clicked, this, &QuickPreview::showMinimized);
    connect(btnMax, &QPushButton::clicked, [this]() {
      auto* ml = qobject_cast<QVBoxLayout*>(layout());
      if (isMaximized()) {
        showNormal();
        if (ml) ml->setContentsMargins(20, 20, 20, 20);
        m_container->setStyleSheet(
          "QFrame#previewContainer { background-color: #1e1e1e; border: 1px solid #444; border-radius: 8px; }"
          "QFrame#previewTitleBar { background-color: #1e1e1e; border-top-left-radius: 7px; border-top-right-radius: 7px; border-bottom: 1px solid #333; }"
          "QTextEdit { border-bottom-left-radius: 7px; border-bottom-right-radius: 7px; background: transparent; border: none; color: #ddd; padding: 10px; }"
          "QPushButton { border: none; border-radius: 4px; background: transparent; padding: 4px; }"
          "QPushButton:hover { background-color: #3e3e42; }"
          "QPushButton:checked { background-color: #FF551C; }"
          "QPushButton#btnClose:hover { background-color: #E81123; }"
        );
      } else {
        showMaximized();
        if (ml) ml->setContentsMargins(0, 0, 0, 0);
        m_container->setStyleSheet(
          "QFrame#previewContainer { background-color: #1e1e1e; border: none; border-radius: 0px; }"
          "QFrame#previewTitleBar { background-color: #1e1e1e; border-radius: 0px; border-bottom: 1px solid #333; }"
          "QTextEdit { background: transparent; border: none; color: #ddd; padding: 10px; }"
          "QPushButton { border: none; border-radius: 4px; background: transparent; padding: 4px; }"
          "QPushButton:hover { background-color: #3e3e42; }"
          "QPushButton:checked { background-color: #FF551C; }"
          "QPushButton#btnClose:hover { background-color: #E81123; }"
        );
      }
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
    m_textEdit->setFocusPolicy(Qt::NoFocus);
    QFont previewFont = m_textEdit->font();
    previewFont.setPointSize(12);
    m_textEdit->setFont(previewFont);
    m_textEdit->installEventFilter(this);
    if (m_textEdit->viewport()) {
      m_textEdit->viewport()->installEventFilter(this);
    }

    // [NEW] 右侧元数据侧边栏
    m_metaPanel = new QWidget();
    m_metaPanel->setObjectName("previewMetaPanel");
    m_metaPanel->setMinimumWidth(200);
    m_metaPanel->setMaximumWidth(260);
    m_metaPanel->setStyleSheet(
      "QWidget#previewMetaPanel { background-color: #161616; border-left: 1px solid #333; }"
      "QLabel { color: #ccc; font-size: 12px; background: transparent; border: none; }"
    );
    auto* metaLayout = new QVBoxLayout(m_metaPanel);
    metaLayout->setContentsMargins(12, 14, 12, 14);
    metaLayout->setSpacing(0);

    auto addMetaRow = [&](const QString& iconName, const QString& labelText, QLabel*& valueLabel) {
      auto* row = new QWidget();
      row->setStyleSheet("background: transparent;");
      auto* rowLayout = new QVBoxLayout(row);
      rowLayout->setContentsMargins(0, 8, 0, 8);
      rowLayout->setSpacing(3);

      auto* header = new QHBoxLayout();
      header->setSpacing(5);
      auto* iconLbl = new QLabel();
      iconLbl->setPixmap(IconHelper::getIcon(iconName, "#666", 12).pixmap(12, 12));
      auto* keyLbl = new QLabel(labelText);
      keyLbl->setStyleSheet("color: #666; font-size: 11px;");
      header->addWidget(iconLbl);
      header->addWidget(keyLbl);
      header->addStretch();

      valueLabel = new QLabel("-");
      valueLabel->setWordWrap(true);
      valueLabel->setStyleSheet("color: #ddd; font-size: 12px; font-weight: bold;");

      auto* sepLine = new QFrame();
      sepLine->setFrameShape(QFrame::HLine);
      sepLine->setStyleSheet("background: #2a2a2a; max-height: 1px; border: none;");

      rowLayout->addLayout(header);
      rowLayout->addWidget(valueLabel);
      rowLayout->addWidget(sepLine);
      metaLayout->addWidget(row);
    };

    addMetaRow("text",    "标题",  m_metaTitle);
    addMetaRow("branch",   "分区",  m_metaCategory);
    addMetaRow("tag",    "标签",  m_metaTags);
    addMetaRow("star",    "评级",  m_metaRating);
    addMetaRow("pin_tilted", "状态",  m_metaStatus);
    addMetaRow("calendar",  "创建于", m_metaCreated);
    addMetaRow("edit",    "更新于", m_metaUpdated);

    // 备注单独处理（斜体蓝色）
    auto* remarkRow = new QWidget();
    remarkRow->setStyleSheet("background: transparent;");
    auto* remarkRowLayout = new QVBoxLayout(remarkRow);
    remarkRowLayout->setContentsMargins(0, 8, 0, 8);
    remarkRowLayout->setSpacing(3);
    auto* remarkHdr = new QHBoxLayout();
    remarkHdr->setSpacing(5);
    auto* riIcon = new QLabel();
    riIcon->setPixmap(IconHelper::getIcon("edit", "#666", 12).pixmap(12, 12));
    auto* riKey = new QLabel("备注");
    riKey->setStyleSheet("color: #666; font-size: 11px;");
    remarkHdr->addWidget(riIcon);
    remarkHdr->addWidget(riKey);
    remarkHdr->addStretch();
    m_metaRemark = new QLabel("-");
    m_metaRemark->setWordWrap(true);
    m_metaRemark->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_metaRemark->setStyleSheet("color: #4fc3f7; font-size: 12px; font-style: italic;");
    remarkRowLayout->addLayout(remarkHdr);
    remarkRowLayout->addWidget(m_metaRemark);
    metaLayout->addWidget(remarkRow);
    metaLayout->addStretch();

    // 内容区 QSplitter（左: 正文 | 右: 元数据）
    auto* contentSplitter = new QSplitter(Qt::Horizontal);
    contentSplitter->setHandleWidth(0);
    contentSplitter->addWidget(m_textEdit);
    contentSplitter->addWidget(m_metaPanel);
    contentSplitter->setStretchFactor(0, 1);
    contentSplitter->setStretchFactor(1, 0);
    contentSplitter->setStyleSheet("QSplitter { background: transparent; }");

    containerLayout->addWidget(contentSplitter);

    mainLayout->addWidget(m_container);

    auto* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(20);
    shadow->setColor(QColor(0, 0, 0, 120));
    shadow->setOffset(0, 4);
    m_container->setGraphicsEffect(shadow);

    resize(1100, 720);

    setupShortcuts();
    connect(&ShortcutManager::instance(), &ShortcutManager::shortcutsChanged, this, &QuickPreview::updateShortcuts);
  }

public:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (event->type() == QEvent::Wheel) {
      bool isTarget = (watched == m_textEdit || (m_textEdit && watched == m_textEdit->viewport()));
      if (isTarget) {
        auto* wheelEvent = static_cast<QWheelEvent*>(event);
        bool ctrlPressed = (wheelEvent->modifiers() & Qt::ControlModifier);

        if (ctrlPressed) {
          if (wheelEvent->angleDelta().y() > 0) {
            m_textEdit->zoomIn(1);
          } else {
            m_textEdit->zoomOut(1);
          }

          // [PERF] zoomIn 仅影响默认字号，无法覆盖 HTML 中显式指定的相对字号（em）或特殊属性（如图片宽度）。
          // 因此需要计算缩放因子并强制触发 HTML 重绘，以确保标题、图片和正文同步缩放。
          // 使用 pointSizeF() 获取更精确的缩放比例
          double factor = m_textEdit->font().pointSizeF() / 12.0;

          QString html = StringUtils::generateNotePreviewHtml(m_currentTitle, m_pureContent, m_currentType, m_currentData, factor);
          m_textEdit->setHtml(html);

          return true; // 拦截事件
        }
      }
    }
    return QWidget::eventFilter(watched, event);
  }

  void showPreview(int noteId, const QString& title, const QString& content, const QPoint& pos, const QString& catName = "", QWidget* caller = nullptr) {
    showPreview(noteId, title, content, "text", QByteArray(), pos, catName, caller);
  }

  void showPreview(int noteId, const QString& title, const QString& content, const QString& type, const QByteArray& data, const QPoint& pos, const QString& catName = "", QWidget* caller = nullptr) {
    if (caller) m_focusBackWidget = caller;
    m_currentNoteId = noteId;

    // 保存预览上下文，用于图片缩放时的 HTML 重绘
    m_currentTitle = title;
    m_currentType = type;
    m_currentData = data;

    if (m_searchEdit) {
      m_searchEdit->clear();
    }
    addToHistory(noteId);
    if (!catName.isEmpty()) {
      m_titleLabel->setText(QString("预览 - %1").arg(catName));
    } else {
      m_titleLabel->setText("预览");
    }
    m_pureContent = content;
    QString html = StringUtils::generateNotePreviewHtml(title, content, type, data);
    m_textEdit->setHtml(html);

    // [NEW] 填充元数据侧边栏
    QVariantMap note = DatabaseManager::instance().getNoteById(noteId);
    if (!note.isEmpty()) {
      if (m_metaTitle)  m_metaTitle->setText(title.isEmpty() ? "-" : title);
      if (m_metaCategory) m_metaCategory->setText(catName.isEmpty() ? "未分类" : catName);
      if (m_metaTags) {
        QString t = note.value("tags").toString().trimmed();
        m_metaTags->setText(t.isEmpty() ? "无" : t);
      }
      if (m_metaRating) {
        int r = note.value("rating").toInt();
        m_metaRating->setText(r > 0 ? QString("★").repeated(r) : "无");
        m_metaRating->setStyleSheet(r > 0
          ? "color: #FFD700; font-size: 14px; font-weight: bold;"
          : "color: #555; font-size: 12px; font-weight: normal;");
      }
      if (m_metaStatus) {
        QStringList s;
        if (note.value("is_pinned").toInt())  s << "📌 置顶";
        if (note.value("is_favorite").toInt()) s << "🔖 书签";
        if (note.value("is_locked").toInt())  s << "🔒 锁定";
        m_metaStatus->setText(s.isEmpty() ? "常规" : s.join(" "));
      }
      if (m_metaCreated)
        m_metaCreated->setText(note.value("created_at").toString().left(16).replace("T", " "));
      if (m_metaUpdated)
        m_metaUpdated->setText(note.value("updated_at").toString().left(16).replace("T", " "));
      if (m_metaRemark) {
        QString remark = note.value("remark").toString().trimmed();
        m_metaRemark->setText(remark.isEmpty() ? "-" : remark);
      }
    }
    QPoint adjustedPos = pos;
    QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) screen = QGuiApplication::primaryScreen();

    bool wasHidden = !isVisible();

    if (wasHidden && screen) {
      // [CRITICAL] 窗口首次打开时，必须位于屏幕中心
      QRect screenGeom = screen->availableGeometry();
      adjustedPos = screenGeom.center() - QRect(0, 0, width(), height()).center();
    } else if (screen) {
      // 实时同步内容时，保持当前位置并仅进行边界修正
      QRect screenGeom = screen->availableGeometry();
      if (adjustedPos.x() + width() > screenGeom.right()) adjustedPos.setX(screenGeom.right() - width());
      if (adjustedPos.x() < screenGeom.left()) adjustedPos.setX(screenGeom.left());
      if (adjustedPos.y() + height() > screenGeom.bottom()) adjustedPos.setY(screenGeom.bottom() - height());
      if (adjustedPos.y() < screenGeom.top()) adjustedPos.setY(screenGeom.top());
    }

    move(adjustedPos);
    // [CRITICAL] 核心逻辑：区分“首次打开”与“实时同步”。
    // 首次打开需要夺取焦点以响应快捷键；同步更新时禁止夺取焦点，以免干扰用户在列表上的连续点击或按键导航体验。
    if (wasHidden) {
      show();
      setFocus();
    } else {
      show();
    }
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
      // [UX] 使用 WidgetWithChildrenShortcut 确保快捷键仅在预览窗获焦时生效，避免与主窗口冲突
      auto* sc = new QShortcut(ShortcutManager::instance().getShortcut(id), this, func, Qt::WidgetWithChildrenShortcut);
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
  }

  void updateShortcuts() {
    for (auto* sc : m_shortcuts) {
      QString id = sc->property("id").toString();
      sc->setKey(ShortcutManager::instance().getShortcut(id));
    }
  }

  void addToHistory(int noteId) {
    if (m_isNavigatingHistory) return;
    if (!m_history.isEmpty() && m_historyIndex >= 0 && m_historyIndex < m_history.size()) {
      if (m_history.at(m_historyIndex) == noteId) return;
    }
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

    // [PERF] 在大数据量下，实时搜索所有匹配项会导致严重的 UI 滞后。
    // 如果文档很大且搜索词较短，限制全量高亮的数量。
    int docLen = m_textEdit->toPlainText().length();
    int maxSelections = (docLen > 100000) ? 500 : 2000;

    QList<QTextEdit::ExtraSelection> selections;
    QTextCursor originalCursor = m_textEdit->textCursor();
    m_textEdit->moveCursor(QTextCursor::Start);
    QColor color = QColor(255, 255, 0, 100);
    int count = 0;

    while (m_textEdit->find(text)) {
      count++;
      if (count <= maxSelections) {
        QTextEdit::ExtraSelection selection;
        selection.format.setBackground(color);
        selection.cursor = m_textEdit->textCursor();
        selections.append(selection);
      } else if (docLen < 500000) {
        // 对于中等大小文档，继续计数但不添加选择，以显示准确的总数
        // 对于超大文档，直接停止以保证响应速度
      } else {
        break;
      }

      // 安全退出：避免搜索导致死循环或过长时间阻塞
      if (count > 5000) break;
    }

    m_textEdit->setExtraSelections(selections);
    m_textEdit->setTextCursor(originalCursor);
    updateSearchCount();
  }

  void findNext() {
    QString text = m_searchEdit->text();
    if (text.isEmpty()) return;
    if (!m_textEdit->find(text)) {
      m_textEdit->moveCursor(QTextCursor::Start);
      m_textEdit->find(text);
    }
    updateSearchCount();
  }

  void findPrev() {
    QString text = m_searchEdit->text();
    if (text.isEmpty()) return;
    if (!m_textEdit->find(text, QTextDocument::FindBackward)) {
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
    if (total == 0) {
      m_searchCountLabel->setText("0/0");
      return;
    }
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
    if (m_pureContent.isEmpty()) {
      QApplication::clipboard()->setText(m_textEdit->toPlainText());
    } else {
      if (m_pureContent.contains("<html", Qt::CaseInsensitive)) {
        QMimeData* mime = new QMimeData();
        mime->setHtml(m_pureContent);
        mime->setText(StringUtils::htmlToPlainText(m_pureContent));
        QApplication::clipboard()->setMimeData(mime);
      } else {
        QApplication::clipboard()->setText(m_pureContent);
      }
    }
    ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #2ecc71;'>" + IconHelper::getIconHtml("screenshot_confirm", "#2ecc71") + " 全部正文已提取到剪贴板</b>");
  }

  void updateHistoryButtons() {
    if (m_btnBack) m_btnBack->setEnabled(m_historyIndex > 0);
    if (m_btnForward) m_btnForward->setEnabled(m_historyIndex < m_history.size() - 1);
    if (m_btnBack) m_btnBack->setIcon(IconHelper::getIcon("nav_first", m_historyIndex > 0 ? "#aaaaaa" : "#444444"));
    if (m_btnForward) m_btnForward->setIcon(IconHelper::getIcon("nav_last", m_historyIndex < m_history.size() - 1 ? "#aaaaaa" : "#444444"));
  }

protected:
  void keyPressEvent(QKeyEvent* event) override {
    if (event->key() == Qt::Key_Space) {
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
      event->accept();
      return;
    }
    if (event->key() == Qt::Key_Escape) {
      if (m_searchEdit && (m_searchEdit->hasFocus() || !m_searchEdit->text().isEmpty())) {
        toggleSearch(false);
        event->accept();
        return;
      }
      // 非搜索状态，关闭预览窗
      hide();
      event->accept();
      return;
    }
    if (event->key() == Qt::Key_W && (event->modifiers() & Qt::ControlModifier)) {
      hide();
      event->accept();
      return;
    }

    // [FALLBACK] 显式处理 Ctrl+F，确保在 QShortcut 失效时仍能定位到搜索框
    if (event->modifiers() == Qt::ControlModifier && event->key() == Qt::Key_F) {
      toggleSearch(true);
      event->accept();
      return;
    }

    QWidget::keyPressEvent(event);
  }

  void hideEvent(QHideEvent* event) override {
    if (m_focusBackWidget) {
      m_focusBackWidget->activateWindow();
      m_focusBackWidget->setFocus();
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
  QString m_pureContent;
  QString m_currentTitle;
  QString m_currentType;
  QByteArray m_currentData;
  int m_currentNoteId = -1;
  bool m_dragging = false;
  bool m_isPinned = false;
  QPushButton* m_btnPin = nullptr;
  QPushButton* m_btnBack = nullptr;
  QPushButton* m_btnForward = nullptr;
  QPoint m_dragPos;
  QWidget* m_focusBackWidget = nullptr;
  QList<int> m_history;
  int m_historyIndex = -1;
  bool m_isNavigatingHistory = false;

  // [NEW] 元数据侧边栏组件
  QWidget* m_metaPanel = nullptr;
  QLabel* m_metaTitle = nullptr;
  QLabel* m_metaCategory = nullptr;
  QLabel* m_metaTags = nullptr;
  QLabel* m_metaRating = nullptr;
  QLabel* m_metaStatus = nullptr;
  QLabel* m_metaCreated = nullptr;
  QLabel* m_metaUpdated = nullptr;
  QLabel* m_metaRemark = nullptr;
};

#endif // QUICKPREVIEW_H
