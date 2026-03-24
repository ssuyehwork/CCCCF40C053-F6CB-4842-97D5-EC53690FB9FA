#include "MetadataPanel.h"
#include "TagCapsule.h"
#include "AdvancedTagSelector.h"
#include "../core/DatabaseManager.h"
#include "IconHelper.h"
#include "FlowLayout.h"
#include "../meta/AmMetaJson.h"
#include "../db/FileDatabase.h"
#include <QVBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFileInfo>
#include <QDebug>
#include <QRegularExpression>
#include <utility>
#include <QHBoxLayout>
#include <QPushButton>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QApplication>
#include <QTextEdit>
#include <QDialog>
#include <QCursor>
#include <QKeyEvent>


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
        "  border-top-left-radius: 0px;"
        "  border-top-right-radius: 0px;"
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
        "border-top-left-radius: 0px; "
        "border-top-right-radius: 0px; "
        "border-bottom: 1px solid #333;" // 统一通过 border 实现分割线
    );
    auto* titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(15, 0, 4, 0);
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
        "QPushButton:hover { background-color: #3e3e42; }" // 2026-03-xx 统一悬停色
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

    // [NEW] 2026-03-24 备注定时器 (防抖，800ms) - 支持物理文件备注保存
    m_remarkSaveTimer = new QTimer(this);
    m_remarkSaveTimer->setSingleShot(true);
    m_remarkSaveTimer->setInterval(800);
    connect(m_remarkSaveTimer, &QTimer::timeout, this, [this]() {
        if (!m_remarkEdit) return;
        
        if (m_currentNoteId != -1) {
            DatabaseManager::instance().updateNoteState(m_currentNoteId, "remark", m_remarkEdit->toPlainText());
        } else if (!m_currentFilePath.isEmpty()) {
            savePhysicalMeta(); // 调用统一的物理元数据保存逻辑
        }
    });

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
    
    // [NEW] 备注输入区
    auto* remarkSection = new QWidget();
    remarkSection->setObjectName("RemarkSection");
    remarkSection->setAttribute(Qt::WA_StyledBackground, true);
    remarkSection->setStyleSheet(
        "#RemarkSection { background-color: rgba(255, 255, 255, 0.05); "
        "border: 1px solid rgba(255, 255, 255, 0.1); "
        "border-radius: 10px; }"
    );
    auto* remarkSectionLayout = new QVBoxLayout(remarkSection);
    remarkSectionLayout->setContentsMargins(12, 10, 8, 10);
    remarkSectionLayout->setSpacing(6);

    auto* remarkHeader = new QHBoxLayout();
    auto* remarkIcon = new QLabel();
    remarkIcon->setPixmap(IconHelper::getIcon("edit", "#AAA", 14).pixmap(14, 14));
    remarkIcon->setStyleSheet("border: none; background: transparent;");
    auto* remarkLabel = new QLabel("备注");
    remarkLabel->setStyleSheet("font-size: 11px; color: #AAA; border: none; background: transparent;");
    remarkHeader->addWidget(remarkIcon);
    remarkHeader->addWidget(remarkLabel);
    remarkHeader->addStretch();
    remarkSectionLayout->addLayout(remarkHeader);

    m_remarkEdit = new QTextEdit();
    m_remarkEdit->installEventFilter(this);
    m_remarkEdit->setPlaceholderText("添加备注说明...");
    m_remarkEdit->setFixedHeight(80);
    m_remarkEdit->setStyleSheet(
        "QTextEdit {"
        "  background-color: transparent;"
        "  border: none;"
        "  color: #EEE;"
        "  font-size: 12px;"
        "  padding: 0px;"
        "}"
        "QScrollBar:vertical { width: 5px; background: transparent; }"
        "QScrollBar::handle:vertical { background: #444; border-radius: 2px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }"
    );
    // 输入后 800ms 自动保存
    connect(m_remarkEdit, &QTextEdit::textChanged, this, [this]() {
        if (m_remarkSaveTimer) m_remarkSaveTimer->start();
    });
    remarkSectionLayout->addWidget(m_remarkEdit);
    layout->addWidget(remarkSection);

    // 标签高度增加的容器
    auto* tagSection = new QWidget();
    tagSection->setObjectName("TagSection");
    tagSection->setAttribute(Qt::WA_StyledBackground, true);
    tagSection->setStyleSheet(
        "#TagSection { background-color: rgba(255, 255, 255, 0.05); "
        "border: 1px solid rgba(255, 255, 255, 0.1); "
        "border-radius: 10px; }"
    );
    auto* tagSectionLayout = new QVBoxLayout(tagSection);
    tagSectionLayout->setContentsMargins(12, 10, 8, 10);
    tagSectionLayout->setSpacing(8);
    
    auto* tagHeader = new QHBoxLayout();
    auto* tagIcon = new QLabel();
    tagIcon->setPixmap(IconHelper::getIcon("tag", "#AAA", 14).pixmap(14, 14));
    tagIcon->setStyleSheet("border: none; background: transparent;"); // 强制去边框
    auto* tagLabel = new QLabel("标签");
    tagLabel->setStyleSheet("font-size: 11px; color: #AAA; border: none; background: transparent;");
    tagHeader->addWidget(tagIcon);
    tagHeader->addWidget(tagLabel);
    tagHeader->addStretch();
    tagSectionLayout->addLayout(tagHeader);
    
    auto* scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    scrollArea->setFixedHeight(120);
    scrollArea->setStyleSheet(
        "QScrollArea { background: transparent; border: none; }"
        "QScrollBar:vertical { width: 6px; background: transparent; }"
        "QScrollBar::handle:vertical { background: #444; border-radius: 3px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }"
    );

    m_tagContainer = new QWidget();
    m_tagContainer->setStyleSheet("background: transparent; border: none;");
    m_tagFlowLayout = new FlowLayout(m_tagContainer, 0, 6, 6);
    scrollArea->setWidget(m_tagContainer);
    
    tagSectionLayout->addWidget(scrollArea);
    
    layout->addWidget(tagSection);

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
    m_capsuleRows[key] = row;
    return row;
}

void MetadataPanel::setFile(const QString& path) {
    if (path.isEmpty() || path == "Desktop" || path == "PC") {
        clearSelection();
        return;
    }
    m_currentNoteId = -1;
    m_currentFilePath = path;
    m_stack->setCurrentIndex(2); // 详情页
    
    m_tagEdit->setEnabled(true);
    m_tagEdit->setPlaceholderText("输入标签备注物理项目...");
    m_separatorLine->show();

    QFileInfo info(path);
    m_capsules["created"]->setText(info.birthTime().toString("yyyy-MM-dd HH:mm"));
    m_capsules["updated"]->setText(info.lastModified().toString("yyyy-MM-dd HH:mm"));
    
    // 显示文件大小
    qint64 size = info.size();
    QString sizeStr = info.isDir() ? "文件夹" : QString::number(size / 1024.0, 'f', 2) + " KB";
    m_capsules["status"]->setText(sizeStr);
    m_capsules["category"]->setText(info.suffix().toUpper() + " 文件");
    
    m_capsuleRows["rating"]->hide(); // 物理文件初始隐藏星级，除非从 .am_meta.json 读取

    if (m_remarkEdit) {
        m_remarkEdit->clear();
        m_remarkEdit->setEnabled(true);
    }
    refreshTags(""); // 初始清空标签
}

void MetadataPanel::setNote(const QVariantMap& note) {
    if(note.isEmpty()) {
        clearSelection();
        return;
    }
    m_currentNoteId = note.value("id").toInt();
    m_stack->setCurrentIndex(2); // 详情页
    
    m_tagEdit->setEnabled(true);
    m_tagEdit->setPlaceholderText("输入标签添加... (双击更多)");
    m_separatorLine->show();

    // [NEW] 填入备注内容（先断开信号避免触发自动保存）
    if (m_remarkEdit) {
        m_remarkEdit->blockSignals(true);
        m_remarkEdit->setPlainText(note.value("remark").toString());
        m_remarkEdit->blockSignals(false);
        m_remarkEdit->setEnabled(true);
    }

    m_capsules["created"]->setText(note.value("created_at").toString().left(16).replace("T", " "));
    m_capsules["updated"]->setText(note.value("updated_at").toString().left(16).replace("T", " "));
    
    int rating = note.value("rating").toInt();
    m_capsuleRows["rating"]->show(); // 始终显示星级行
    if (rating > 0) {
        QString stars = QString("★").repeated(rating);
        m_capsules["rating"]->setText(stars);
        m_capsules["rating"]->setStyleSheet("font-size: 12px; color: #FFD700; border: none; font-weight: bold; background: transparent;");
    } else {
        m_capsules["rating"]->setText(""); // 星级为0时不显示星号
    }
    
    QStringList status;
    if (note.value("is_pinned").toInt() > 0) status << "置顶";
    if (note.value("is_favorite").toInt() > 0) status << "收藏";
    // 2026-03-xx 按照用户要求：移除笔记级锁定显示
    m_capsules["status"]->setText(status.isEmpty() ? "未置顶" : status.join(", "));

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
    refreshTags(note.value("tags").toString());
}

void MetadataPanel::setMultipleNotes(int count) {
    m_currentNoteId = -1;
    m_stack->setCurrentIndex(1); // 多选页
    m_tagEdit->setEnabled(true);
    m_tagEdit->setPlaceholderText("输入标签批量添加...");
    m_separatorLine->show();
}

void MetadataPanel::clearSelection() {
    m_currentNoteId = -1;
    m_stack->setCurrentIndex(0); // 无选择页
    m_tagEdit->setEnabled(false);
    m_tagEdit->setPlaceholderText("请先选择一个项目");
    m_separatorLine->hide();
    if (m_remarkEdit) {
        m_remarkEdit->blockSignals(true);
        m_remarkEdit->clear();
        m_remarkEdit->blockSignals(false);
        m_remarkEdit->setEnabled(false);
    }
}

void MetadataPanel::refreshTags(const QString& tagsStr) {
    // 清空旧胶囊
    QLayoutItem* item;
    while ((item = m_tagFlowLayout->takeAt(0))) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    
    if (tagsStr.isEmpty()) {
        auto* placeholder = new QLabel("无标签");
        placeholder->setStyleSheet("color: #666; font-size: 11px; font-style: italic; border: none; background: transparent;");
        m_tagFlowLayout->addWidget(placeholder);
        return;
    }
    
    QStringList tags = tagsStr.split(QRegularExpression("[,，]"), Qt::SkipEmptyParts);
    for (const QString& tag : std::as_const(tags)) {
        QString trimmed = tag.trimmed();
        if (trimmed.isEmpty()) continue;
        
        auto* capsule = new TagCapsule(trimmed, m_tagContainer);
        connect(capsule, &TagCapsule::removeRequested, this, &MetadataPanel::removeTag);
        m_tagFlowLayout->addWidget(capsule);
    }
}

void MetadataPanel::removeTag(const QString& tag) {
    if (m_currentNoteId == -1) return;
    
    QVariantMap note = DatabaseManager::instance().getNoteById(m_currentNoteId);
    QString currentTagsStr = note.value("tags").toString();
    QStringList currentTags = currentTagsStr.split(QRegularExpression("[,，]"), Qt::SkipEmptyParts);
    
    QStringList newTags;
    for (QString t : currentTags) {
        t = t.trimmed();
        if (t != tag && !t.isEmpty()) newTags << t;
    }
    
    QString newTagsStr = newTags.join(", ");
    DatabaseManager::instance().updateNoteState(m_currentNoteId, "tags", newTagsStr);
    
    // 刷新显示
    refreshTags(newTagsStr);
}

void MetadataPanel::handleTagInput() {
    QString text = m_tagEdit->text().trimmed();
    if (text.isEmpty()) return;
    
    QStringList tags = text.split(QRegularExpression("[,，]"), Qt::SkipEmptyParts);
    for (QString& t : tags) t = t.trimmed();
    
    if (m_currentNoteId != -1) {
        DatabaseManager::instance().addTagsToNote(m_currentNoteId, tags);
        // 刷新显示
        QVariantMap note = DatabaseManager::instance().getNoteById(m_currentNoteId);
        refreshTags(note.value("tags").toString());
    } else if (!m_currentFilePath.isEmpty()) {
        // [NEW] 2026-03-24 按照用户需求：物理项目标签添加，同步至 .am_meta.json
        QFileInfo info(m_currentFilePath);
        std::wstring folderPath = info.absolutePath().toStdWString();
        std::string name = info.fileName().toStdString();
        
        AmMetaJson::FolderMeta folder;
        std::map<std::string, AmMetaJson::ItemMeta> items;
        AmMetaJson::load(folderPath, folder, items);
        
        AmMetaJson::ItemMeta& meta = items[name];
        meta.type = info.isDir() ? "folder" : "file";
        for (const auto& t : tags) {
            if (std::find(meta.tags.begin(), meta.tags.end(), t.toStdString()) == meta.tags.end()) {
                meta.tags.push_back(t.toStdString());
            }
        }
        
        if (AmMetaJson::save(folderPath, folder, items)) {
            // 同步显示
            QStringList currentTags;
            for (const auto& t : meta.tags) currentTags << QString::fromStdString(t);
            refreshTags(currentTags.join(","));
            
            // 异步触发 FileDatabase 同步 (后续由 SyncQueue 处理)
            syncToFileDatabase(m_currentFilePath, meta);
        }
    } else {
        emit tagAdded(tags);
    }
    m_tagEdit->clear();
}

void MetadataPanel::savePhysicalMeta() {
    if (m_currentFilePath.isEmpty()) return;
    
    QFileInfo info(m_currentFilePath);
    std::wstring folderPath = info.absolutePath().toStdWString();
    std::string name = info.fileName().toStdString();
    
    AmMetaJson::FolderMeta folder;
    std::map<std::string, AmMetaJson::ItemMeta> items;
    AmMetaJson::load(folderPath, folder, items);
    
    AmMetaJson::ItemMeta& meta = items[name];
    meta.type = info.isDir() ? "folder" : "file";
    meta.remark = m_remarkEdit->toPlainText().toStdString();
    // 可从 UI 状态获取 rating, color, pinned 等
    
    if (AmMetaJson::save(folderPath, folder, items)) {
        syncToFileDatabase(m_currentFilePath, meta);
    }
}

void MetadataPanel::syncToFileDatabase(const QString& path, const AmMetaJson::ItemMeta& meta) {
    // 2026-03-24 [NEW] JSON 写入成功后同步至 SQLite 索引
    QVariantMap dbMeta;
    dbMeta["path"] = path;
    dbMeta["type"] = QString::fromStdString(meta.type);
    dbMeta["rating"] = meta.rating;
    dbMeta["color"] = QString::fromStdString(meta.color);
    dbMeta["pinned"] = meta.pinned ? 1 : 0;
    dbMeta["parent_path"] = QFileInfo(path).absolutePath();
    
    QJsonArray tagArr;
    for (const auto& t : meta.tags) tagArr.append(QString::fromStdString(t));
    dbMeta["tags"] = QJsonDocument(tagArr).toJson(QJsonDocument::Compact);
    
    FileDatabase::instance().updateItemMeta(path, dbMeta);
}

bool MetadataPanel::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        if (auto* edit = qobject_cast<QLineEdit*>(watched)) {
            if (keyEvent->key() == Qt::Key_Up) {
                edit->setCursorPosition(0);
                return true;
            } else if (keyEvent->key() == Qt::Key_Down) {
                edit->setCursorPosition(edit->text().length());
                return true;
            }
        } else if (auto* textEdit = qobject_cast<QTextEdit*>(watched)) {
            if (keyEvent->key() == Qt::Key_Up) {
                QTextCursor cursor = textEdit->textCursor();
                int pos = cursor.position();
                cursor.movePosition(QTextCursor::Up);
                if (cursor.position() == pos) {
                    cursor.movePosition(QTextCursor::Start);
                    textEdit->setTextCursor(cursor);
                    return true;
                }
            } else if (keyEvent->key() == Qt::Key_Down) {
                QTextCursor cursor = textEdit->textCursor();
                int pos = cursor.position();
                cursor.movePosition(QTextCursor::Down);
                if (cursor.position() == pos) {
                    cursor.movePosition(QTextCursor::End);
                    textEdit->setTextCursor(cursor);
                    return true;
                }
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void MetadataPanel::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        // [MODIFIED] 拦截元数据面板输入状态下的 Esc（两段式逻辑）。
        QLineEdit* focused = qobject_cast<QLineEdit*>(focusWidget());
        if (focused && focused == m_tagEdit) {
            if (!focused->text().isEmpty()) {
                focused->clear();
            } else {
                focused->clearFocus();
            }
            event->accept();
            return;
        }
    }
    QWidget::keyPressEvent(event);
}

void MetadataPanel::openTagSelector() {
    if (m_currentNoteId == -1) return;
    
    QVariantMap note = DatabaseManager::instance().getNoteById(m_currentNoteId);
    QString currentTagsStr = note.value("tags").toString();
    QStringList currentTags = currentTagsStr.split(QRegularExpression("[,，]"), Qt::SkipEmptyParts);
    for (QString& t : currentTags) t = t.trimmed();

    auto* selector = new AdvancedTagSelector(this);
    // 获取最近使用的标签 (20个) 和全量标签
    auto recentTags = DatabaseManager::instance().getRecentTagsWithCounts(20);
    auto allTags = DatabaseManager::instance().getAllTags();
    selector->setup(recentTags, allTags, currentTags);
    connect(selector, &AdvancedTagSelector::tagsConfirmed, [this](const QStringList& tags){
        if (m_currentNoteId != -1) {
            QString newTagsStr = tags.join(", ");
            DatabaseManager::instance().updateNoteState(m_currentNoteId, "tags", newTagsStr);
            // 刷新本地显示
            refreshTags(newTagsStr);
        }
    });
    selector->showAtCursor();
}
