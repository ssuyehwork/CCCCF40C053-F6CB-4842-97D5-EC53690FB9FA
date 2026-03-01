#include "MetadataPanel.h"
#include "AdvancedTagSelector.h"
#include "TitleEditorDialog.h"
#include "../core/DatabaseManager.h"
#include "IconHelper.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QApplication>
#include <QTextEdit>
#include <QDialog>
#include <QCursor>


// ==========================================
// MetadataPanel
// ==========================================
MetadataPanel::MetadataPanel(QWidget* parent) : QWidget(parent) {
    setMinimumWidth(230); // 最小宽度 230px，可拉伸
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet("background: transparent; border: none; outline: none;");
    initUI();
}

void MetadataPanel::initUI() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0); // 移除外部边距，由 MainWindow 的 Splitter 统一控制

    // 内部卡片容器
    auto* container = new QFrame(this);
    container->setObjectName("MetadataContainer");
    container->setStyleSheet(
        "#MetadataContainer {"
        "  background-color: #1e1e1e;"
        "  border: 1px solid #333333;"
        "  border-top-left-radius: 12px;"
        "  border-top-right-radius: 12px;"
        "  border-bottom-left-radius: 0px;"
        "  border-bottom-right-radius: 0px;"
        "}"
    );
    container->setAttribute(Qt::WA_StyledBackground, true);

    auto* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(10);
    shadow->setXOffset(0);
    shadow->setYOffset(4);
    shadow->setColor(QColor(0, 0, 0, 150));
    container->setGraphicsEffect(shadow);

    auto* containerLayout = new QVBoxLayout(container);
    containerLayout->setContentsMargins(0, 0, 0, 0); // 设为 0 以允许标题栏拉伸到边缘
    containerLayout->setSpacing(0);

    // 1. 顶部标题栏 (锁定 32px，标准配色)
    auto* titleBar = new QWidget();
    titleBar->setObjectName("MetadataHeader");
    titleBar->setFixedHeight(32);
    titleBar->setStyleSheet(
        "background-color: #252526; "
        "border-top-left-radius: 12px; "
        "border-top-right-radius: 12px; "
        "border-bottom: 1px solid #333;" // 统一通过 border 实现分割线
    );
    auto* titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(15, 0, 10, 0);
    titleLayout->setSpacing(8);

    auto* icon = new QLabel();
    icon->setPixmap(IconHelper::getIcon("all_data", "#4a90e2", 18).pixmap(18, 18));
    auto* lbl = new QLabel("元数据");
    lbl->setStyleSheet("font-size: 13px; font-weight: bold; color: #4a90e2; background: transparent; border: none;");
    titleLayout->addWidget(icon);
    titleLayout->addWidget(lbl);
    titleLayout->addStretch();

    auto* closeBtn = new QPushButton();
    closeBtn->setIcon(IconHelper::getIcon("close", "#888888"));
    closeBtn->setFixedSize(24, 24);
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setStyleSheet(
        "QPushButton { background-color: transparent; border: none; border-radius: 4px; }"
        "QPushButton:hover { background-color: #e74c3c; }"
    );
    connect(closeBtn, &QPushButton::clicked, this, [this](){
        hide();
        emit closed();
    });
    titleLayout->addWidget(closeBtn);
    containerLayout->addWidget(titleBar);

    // 3. 内容包裹容器 (带边距)
    auto* contentWidget = new QWidget();
    contentWidget->setStyleSheet(
        "QWidget { "
        "  background-color: transparent; "
        "  border: none; "
        "  border-bottom-left-radius: 0px; "
        "  border-bottom-right-radius: 0px; "
        "}"
    );
    auto* innerLayout = new QVBoxLayout(contentWidget);
    innerLayout->setContentsMargins(15, 10, 15, 15);
    innerLayout->setSpacing(10);

    m_stack = new QStackedWidget(this);
    m_stack->setStyleSheet("background-color: transparent;");
    
    m_stack->addWidget(createInfoWidget("select", "未选择项目", "请选择一个项目以查看其元数据"));
    m_stack->addWidget(createInfoWidget("all_data", "已选择多个项目", "请仅选择一项以查看其元数据"));
    
    m_metadataDisplayWidget = createMetadataDisplay();
    m_stack->addWidget(m_metadataDisplayWidget);
    
    innerLayout->addWidget(m_stack);

    // 标题输入框 (仅单选显示)
    m_titleEdit = new ClickableLineEdit();
    m_titleEdit->setPlaceholderText("标题");
    m_titleEdit->setStyleSheet(
        "background-color: rgba(255, 255, 255, 0.05); "
        "border: 1px solid rgba(255, 255, 255, 0.1); "
        "border-radius: 10px; "
        "color: #EEE; "
        "font-size: 13px; "
        "font-weight: bold; "
        "padding: 8px 12px; "
        "margin-top: 10px;"
    );
    connect(m_titleEdit, &QLineEdit::editingFinished, [this](){
        if(m_currentNoteId != -1) {
            // [OPTIMIZED] updateNoteState 内部已处理 FTS 同步及信号发射，此处不再重复 emit
            DatabaseManager::instance().updateNoteState(m_currentNoteId, "title", m_titleEdit->text());
        }
    });
    connect(m_titleEdit, &QLineEdit::returnPressed, m_titleEdit, &QWidget::clearFocus);
    connect(m_titleEdit, &ClickableLineEdit::doubleClicked, this, &MetadataPanel::openExpandedTitleEditor);
    innerLayout->addWidget(m_titleEdit);

    innerLayout->addStretch(1);

    m_separatorLine = new QFrame();
    m_separatorLine->setFrameShape(QFrame::HLine);
    m_separatorLine->setFrameShadow(QFrame::Plain);
    m_separatorLine->setStyleSheet("background-color: #505050; border: none; max-height: 1px; margin-bottom: 5px;");
    innerLayout->addWidget(m_separatorLine);

    // 标签输入框 (双击更多)
    m_tagEdit = new ClickableLineEdit();
    m_tagEdit->setPlaceholderText("输入标签添加... (双击更多)");
    m_tagEdit->setStyleSheet(
        "QLineEdit { background-color: rgba(255, 255, 255, 0.05); "
        "border: 1px solid rgba(255, 255, 255, 0.1); "
        "border-radius: 10px; "
        "padding: 8px 12px; "
        "font-size: 12px; "
        "color: #EEE; } "
        "QLineEdit:focus { border-color: #4a90e2; background-color: rgba(255, 255, 255, 0.08); } "
        "QLineEdit:disabled { background-color: transparent; border: 1px solid #333; color: #666; }"
    );
    connect(m_tagEdit, &QLineEdit::returnPressed, this, &MetadataPanel::handleTagInput);
    connect(m_tagEdit, &ClickableLineEdit::doubleClicked, this, &MetadataPanel::openTagSelector);
    innerLayout->addWidget(m_tagEdit);

    containerLayout->addWidget(contentWidget);
    mainLayout->addWidget(container);

    // 初始状态
    clearSelection();
}

QWidget* MetadataPanel::createInfoWidget(const QString& icon, const QString& title, const QString& subtitle) {
    auto* w = new QWidget();
    auto* layout = new QVBoxLayout(w);
    layout->setContentsMargins(20, 40, 20, 20);
    layout->setSpacing(15);
    layout->setAlignment(Qt::AlignCenter);

    auto* iconLabel = new QLabel();
    iconLabel->setPixmap(IconHelper::getIcon(icon, "#555", 64).pixmap(64, 64));
    iconLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(iconLabel);

    auto* titleLabel = new QLabel(title);
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setStyleSheet("font-size: 14px; font-weight: bold; color: #e0e0e0;");
    layout->addWidget(titleLabel);

    auto* subLabel = new QLabel(subtitle);
    subLabel->setAlignment(Qt::AlignCenter);
    subLabel->setStyleSheet("font-size: 12px; color: #888;");
    subLabel->setWordWrap(true);
    layout->addWidget(subLabel);

    layout->addStretch();
    return w;
}

QWidget* MetadataPanel::createMetadataDisplay() {
    auto* w = new QWidget();
    auto* layout = new QVBoxLayout(w);
    layout->setContentsMargins(0, 5, 0, 5);
    layout->setSpacing(8);
    layout->setAlignment(Qt::AlignTop);

    layout->addWidget(createCapsule("创建于", "created"));
    layout->addWidget(createCapsule("更新于", "updated"));
    layout->addWidget(createCapsule("分类", "category"));
    layout->addWidget(createCapsule("状态", "status"));
    layout->addWidget(createCapsule("星级", "rating"));
    layout->addWidget(createCapsule("标签", "tags"));

    return w;
}

QWidget* MetadataPanel::createCapsule(const QString& label, const QString& key) {
    auto* row = new QWidget();
    row->setAttribute(Qt::WA_StyledBackground, true);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(12, 8, 12, 8);
    layout->setSpacing(10);
    
    row->setStyleSheet(
        "QWidget { background-color: rgba(255, 255, 255, 0.05); "
        "border: 1px solid rgba(255, 255, 255, 0.1); "
        "border-radius: 10px; }"
    );
    
    auto* lbl = new QLabel(label);
    lbl->setStyleSheet("font-size: 11px; color: #AAA; border: none; min-width: 45px; background: transparent;");
    
    auto* val = new QLabel("-");
    val->setWordWrap(true);
    val->setStyleSheet("font-size: 12px; color: #FFF; border: none; font-weight: bold; background: transparent;");
    val->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    
    layout->addWidget(lbl);
    layout->addWidget(val);
    
    m_capsules[key] = val;
    return row;
}

void MetadataPanel::setNote(const QVariantMap& note) {
    if(note.isEmpty()) {
        clearSelection();
        return;
    }
    m_currentNoteId = note.value("id").toInt();
    m_stack->setCurrentIndex(2); // 详情页
    
    m_titleEdit->show();
    m_titleEdit->setText(note.value("title").toString());
    m_titleEdit->setCursorPosition(0);
    
    m_tagEdit->setEnabled(true);
    m_tagEdit->setPlaceholderText("输入标签添加... (双击更多)");
    m_separatorLine->show();

    m_capsules["created"]->setText(note.value("created_at").toString().left(16).replace("T", " "));
    m_capsules["updated"]->setText(note.value("updated_at").toString().left(16).replace("T", " "));
    
    int rating = note.value("rating").toInt();
    QString stars = QString("★").repeated(rating) + QString("☆").repeated(5 - rating);
    m_capsules["rating"]->setText(stars);
    
    QStringList status;
    if (note.value("is_pinned").toInt() > 0) status << "置顶";
    if (note.value("is_favorite").toInt() > 0) status << "书签";
    if (note.value("is_locked").toInt() > 0) status << "锁定";
    m_capsules["status"]->setText(status.isEmpty() ? "常规" : status.join(", "));

    // 分类
    int catId = note.value("category_id").toInt();
    if (catId > 0) {
        auto categories = DatabaseManager::instance().getAllCategories();
        for (const auto& cat : categories) {
            if (cat.value("id").toInt() == catId) {
                m_capsules["category"]->setText(cat.value("name").toString());
                break;
            }
        }
    } else {
        m_capsules["category"]->setText("未分类");
    }

    // 标签显示
    m_capsules["tags"]->setText(note.value("tags").toString().isEmpty() ? "无" : note.value("tags").toString());
}

void MetadataPanel::setMultipleNotes(int count) {
    m_currentNoteId = -1;
    m_stack->setCurrentIndex(1); // 多选页
    m_titleEdit->hide();
    m_tagEdit->setEnabled(true);
    m_tagEdit->setPlaceholderText("输入标签批量添加...");
    m_separatorLine->show();
}

void MetadataPanel::clearSelection() {
    m_currentNoteId = -1;
    m_stack->setCurrentIndex(0); // 无选择页
    m_titleEdit->hide();
    m_tagEdit->setEnabled(false);
    m_tagEdit->setPlaceholderText("请先选择一个项目");
    m_separatorLine->hide();
}

void MetadataPanel::openExpandedTitleEditor() {
    if (m_currentNoteId == -1) return;
    
    TitleEditorDialog dialog(m_titleEdit->text(), this);
    if (dialog.exec() == QDialog::Accepted) {
        QString newTitle = dialog.getText();
        if (!newTitle.isEmpty() && newTitle != m_titleEdit->text()) {
            m_titleEdit->setText(newTitle);
            DatabaseManager::instance().updateNoteState(m_currentNoteId, "title", newTitle);
        }
    }
}

void MetadataPanel::handleTagInput() {
    QString text = m_tagEdit->text().trimmed();
    if (text.isEmpty()) return;
    
    QStringList tags = { text };
    emit tagAdded(tags);
    m_tagEdit->clear();
}

void MetadataPanel::openTagSelector() {
    if (m_currentNoteId == -1) return;
    
    QStringList currentTags = m_capsules["tags"]->text().split(",", Qt::SkipEmptyParts);
    for (QString& t : currentTags) t = t.trimmed();
    if (m_capsules["tags"]->text() == "无") currentTags.clear();

    auto* selector = new AdvancedTagSelector(this);
    // 获取最近使用的标签 (20个) 和全量标签
    auto recentTags = DatabaseManager::instance().getRecentTagsWithCounts(20);
    auto allTags = DatabaseManager::instance().getAllTags();
    selector->setup(recentTags, allTags, currentTags);
    connect(selector, &AdvancedTagSelector::tagsConfirmed, [this](const QStringList& tags){
        if (m_currentNoteId != -1) {
            DatabaseManager::instance().updateNoteState(m_currentNoteId, "tags", tags.join(", "));
            // 刷新本地显示
            m_capsules["tags"]->setText(tags.join(", "));
        }
    });
    selector->showAtCursor();
}
