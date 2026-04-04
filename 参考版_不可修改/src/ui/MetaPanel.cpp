#include "MetaPanel.h"
#include "../../SvgIcons.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QMouseEvent>
#include <QScrollBar>
#include <QStyle>
#include <QScrollArea>
#include <QFileInfo>
#include <QLabel>
#include "UiHelper.h"
#include "../meta/AmMetaJson.h"

namespace ArcMeta {

// --- TagPill ---
TagPill::TagPill(const QString& text, QWidget* parent) 
    : QWidget(parent), m_text(text) {
    setProperty("tagText", text); // 修复：设置属性以便删除逻辑查找
    setFixedHeight(22);

    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 0, 4, 0);
    layout->setSpacing(4);

    QLabel* lbl = new QLabel(text, this);
    lbl->setStyleSheet("color: #EEEEEE; font-size: 12px; border: none; background: transparent;");
    
    m_closeBtn = new QPushButton(this);
    m_closeBtn->setFixedSize(14, 14);
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    m_closeBtn->setIcon(UiHelper::getIcon("x", QColor("#B0B0B0"), 12));
    m_closeBtn->setIconSize(QSize(10, 10));
    m_closeBtn->setStyleSheet(
        "QPushButton { border: none; background: transparent; }"
        "QPushButton:hover { background: rgba(255, 255, 255, 0.1); border-radius: 2px; }"
    );

    layout->addWidget(lbl);
    layout->addWidget(m_closeBtn);

    connect(m_closeBtn, &QPushButton::clicked, [this]() { 
        emit deleteRequested(m_text); 
    });

    // 根据内容自动计算宽度
    QFontMetrics fm(lbl->font());
    setFixedWidth(fm.horizontalAdvance(text) + 30); 
}

void TagPill::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(QColor("#2B2B2B"));
    painter.setPen(QPen(QColor("#444444"), 1));
    painter.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 11, 11);
}

// --- FlowLayout ---
FlowLayout::FlowLayout(QWidget *parent, int margin, int hSpacing, int vSpacing)
    : QLayout(parent), m_hSpace(hSpacing), m_vSpace(vSpacing) {
    setContentsMargins(margin, margin, margin, margin);
}

FlowLayout::~FlowLayout() {
    QLayoutItem *item;
    while ((item = takeAt(0))) delete item;
}

void FlowLayout::addItem(QLayoutItem *item) { itemList.append(item); }
int FlowLayout::horizontalSpacing() const { return m_hSpace >= 0 ? m_hSpace : 4; }
int FlowLayout::verticalSpacing() const { return m_vSpace >= 0 ? m_vSpace : 4; }
int FlowLayout::count() const { return itemList.size(); }
QLayoutItem *FlowLayout::itemAt(int index) const { return itemList.value(index); }
QLayoutItem *FlowLayout::takeAt(int index) { 
    return (index >= 0 && index < itemList.size()) ? itemList.takeAt(index) : nullptr; 
}
Qt::Orientations FlowLayout::expandingDirections() const { return Qt::Orientations(); }
bool FlowLayout::hasHeightForWidth() const { return true; }
int FlowLayout::heightForWidth(int width) const { return doLayout(QRect(0, 0, width, 0), true); }
void FlowLayout::setGeometry(const QRect &rect) { QLayout::setGeometry(rect); doLayout(rect, false); }
QSize FlowLayout::sizeHint() const { return minimumSize(); }
QSize FlowLayout::minimumSize() const {
    QSize size;
    for (QLayoutItem *item : itemList) size = size.expandedTo(item->minimumSize());
    size += QSize(2 * contentsMargins().top(), 2 * contentsMargins().top());
    return size;
}

int FlowLayout::doLayout(const QRect &rect, bool testOnly) const {
    int left, top, right, bottom;
    getContentsMargins(&left, &top, &right, &bottom);
    QRect effectiveRect = rect.adjusted(+left, +top, -right, -bottom);
    int x = effectiveRect.x();
    int y = effectiveRect.y();
    int lineHeight = 0;
    for (QLayoutItem *item : itemList) {
        int spaceX = horizontalSpacing();
        int spaceY = verticalSpacing();
        int nextX = x + item->sizeHint().width() + spaceX;
        if (nextX - spaceX > effectiveRect.right() && lineHeight > 0) {
            x = effectiveRect.x();
            y = y + lineHeight + spaceY;
            nextX = x + item->sizeHint().width() + spaceX;
            lineHeight = 0;
        }
        if (!testOnly) item->setGeometry(QRect(QPoint(x, y), item->sizeHint()));
        x = nextX;
        lineHeight = qMax(lineHeight, item->sizeHint().height());
    }
    return y + lineHeight - rect.y() + bottom;
}

// --- StarRatingWidget ---
StarRatingWidget::StarRatingWidget(QWidget* parent) : QWidget(parent) { 
    setFixedSize(5 * 20 + 4 * 4, 20); 
    setCursor(Qt::PointingHandCursor); 
}

void StarRatingWidget::setRating(int rating) { 
    m_rating = rating; 
    update(); 
}

void StarRatingWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this); 
    painter.setRenderHint(QPainter::Antialiasing); 
    
    int starSize = 20;
    int spacing = 4;
    QPixmap filledStar = UiHelper::getPixmap("star_filled", QSize(starSize, starSize), QColor("#EF9F27"));
    QPixmap emptyStar = UiHelper::getPixmap("star", QSize(starSize, starSize), QColor("#444444"));

    for (int i = 0; i < 5; ++i) { 
        QRect r(i * (starSize + spacing), 0, starSize, starSize);
        painter.drawPixmap(r, (i < m_rating) ? filledStar : emptyStar);
    }
}

void StarRatingWidget::mousePressEvent(QMouseEvent* e) { 
    // 2026-03-xx 按照用户要求：星级仅显示，禁止点击修改
    e->accept();
}

// --- ColorPickerWidget ---
ColorPickerWidget::ColorPickerWidget(QWidget* parent) : QWidget(parent) {
    m_colors = {{L"", QColor("#888780")}, {L"red", QColor("#E24B4A")}, {L"orange", QColor("#EF9F27")}, {L"yellow", QColor("#FAC775")}, {L"green", QColor("#639922")}, {L"cyan", QColor("#1D9E75")}, {L"blue", QColor("#378ADD")}, {L"purple", QColor("#7F77DD")}, {L"gray", QColor("#5F5E5A")}};
    int count = (int)m_colors.size();
    // 增加高度至 24px 以容纳选择外圈 (20px + padding)
    setFixedSize(count * 24, 24); 
    setCursor(Qt::PointingHandCursor);
}

void ColorPickerWidget::setColor(const std::wstring& name) { 
    m_currentColor = name; 
    update(); 
}

void ColorPickerWidget::paintEvent(QPaintEvent*) {
    QPainter p(this); 
    p.setRenderHint(QPainter::Antialiasing);
    for (int i = 0; i < (int)m_colors.size(); ++i) {
        // 圆点 18x18，外圈预留空间
        QRect r(i * 24 + 3, 3, 18, 18);
        if (m_colors[i].name == m_currentColor) { 
            p.setPen(QPen(QColor("#FFFFFF"), 1.5)); 
            // 绘制外层的高亮圈
            p.drawEllipse(r.adjusted(-2, -2, 2, 2)); 
        }
        
        p.setPen(Qt::NoPen);
        p.setBrush(m_colors[i].value); 
        p.drawEllipse(r);
    }
}

void ColorPickerWidget::mousePressEvent(QMouseEvent* e) { 
    // 2026-03-xx 按照用户要求：颜色标记仅显示，禁止点击修改
    e->accept();
}

// --- MetaPanel ---
MetaPanel::MetaPanel(QWidget* parent) : QFrame(parent) {
    setObjectName("MetadataContainer");
    setAttribute(Qt::WA_StyledBackground, true);
    setMinimumWidth(230); 
    
    // 核心修正：移除宽泛的 QWidget QSS，防止其屏蔽 MainWindow 赋予的 ID 边框样式
    setStyleSheet("color: #EEEEEE;");
    m_mainLayout = new QVBoxLayout(this); 
    m_mainLayout->setContentsMargins(0, 0, 0, 0); 
    m_mainLayout->setSpacing(0);


    initUi();
}

void MetaPanel::setFocusHighlight(bool visible) {
    if (m_focusLine) m_focusLine->setVisible(visible);
}

void MetaPanel::initUi() {
    // 物理还原：1px 翠绿高亮焦点线 (#2ecc71)
    m_focusLine = new QWidget(this);
    m_focusLine->setFixedHeight(1);
    m_focusLine->setStyleSheet("background-color: #2ecc71;");
    m_focusLine->hide(); // 初始隐藏
    m_mainLayout->addWidget(m_focusLine);

    // 面板标题 (还原旧版架构：Layout + Icon + Text + CloseBtn)
    QWidget* header = new QWidget(this);
    header->setObjectName("ContainerHeader");
    header->setFixedHeight(32);
    // 重新注入标题栏样式，确保背景色和边框还原
    header->setStyleSheet(
        "QWidget#ContainerHeader {"
        "  background-color: #252526;"
        "  border-bottom: 1px solid #333;"
        "}"
    );
    QHBoxLayout* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(15, 2, 15, 0); // 严格还原 15px 左右边距，顶部 2px 偏移以垂直居中
    headerLayout->setSpacing(8);

    QLabel* iconLabel = new QLabel(header);
    iconLabel->setPixmap(UiHelper::getIcon("all_data", QColor("#4a90e2"), 18).pixmap(18, 18));
    headerLayout->addWidget(iconLabel);

    QLabel* titleLabel = new QLabel("元数据", header);
    titleLabel->setStyleSheet("font-size: 13px; font-weight: bold; color: #4a90e2; background: transparent; border: none;");
    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();

    QPushButton* closeBtn = new QPushButton(header);
    closeBtn->setIcon(UiHelper::getIcon("x", QColor("#888888"), 16));
    closeBtn->setFixedSize(24, 24);
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setStyleSheet(
        "QPushButton { background: transparent; border: none; border-radius: 4px; }"
        "QPushButton:hover { background-color: #3e3e42; }"
    );
    connect(closeBtn, &QPushButton::clicked, [this]() {
        this->hide();
    });
    headerLayout->addWidget(closeBtn);

    m_mainLayout->addWidget(header);

    m_scrollArea = new QScrollArea(this); 
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setWidgetResizable(true); 
    m_scrollArea->setStyleSheet("QScrollArea { border: none; background: transparent; }");

    m_container = new QWidget(m_scrollArea); 
    m_containerLayout = new QVBoxLayout(m_container); 
    m_containerLayout->setContentsMargins(0, 0, 0, 0); 
    m_containerLayout->setSpacing(0);

    addInfoRow("名称", lblName); 
    addInfoRow("类型", lblType); 
    addInfoRow("大小", lblSize);
    addInfoRow("创建时间", lblCtime); 
    addInfoRow("修改时间", lblMtime); 
    addInfoRow("访问时间", lblAtime);
    addInfoRow("物理路径", lblPath); 
    addInfoRow("加密状态", lblEncrypted);

    m_containerLayout->addSpacing(10);
    m_containerLayout->addWidget(createSeparator());

    chkPinned = new QCheckBox("置顶条目", m_container); 
    // 2026-03-xx 按照用户要求：置顶仅显示勾选状态，禁止用户交互
    chkPinned->setAttribute(Qt::WA_TransparentForMouseEvents);
    chkPinned->setFocusPolicy(Qt::NoFocus);
    chkPinned->setStyleSheet(
        "QCheckBox { font-size: 11px; color: #5F5E5A; spacing: 5px; }"
        "QCheckBox::indicator { width: 13px; height: 13px; border: 1px solid #555; border-radius: 2px; background: #1E1E1E; }"
        "QCheckBox::indicator:checked { "
        "   border: 1px solid #378ADD; "
        "   background: #1E1E1E; "
        "   image: url(data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHZpZXdCb3g9IjAgMCAyNCAyNCIgZmlsbD0ibm9uZSIgc3Ryb2tlPSIjMzc4QUREIiBzdHJva2Utd2lkdGg9IjMuNSIgc3Ryb2tlLWxpbmVjYXA9InJvdW5kIiBzdHJva2UtbGluZWpvaW49InJvdW5kIj48cG9seWxpbmUgcG9pbnRzPSIyMCA2IDkgMTcgNCAxMiI+PC9wb2x5bGluZT48L3N2Zz4=);"
        "}"
    );
    m_containerLayout->addWidget(chkPinned);

    QLabel* lR = new QLabel("星级", m_container); 
    lR->setStyleSheet("font-size: 11px; color: #5F5E5A;"); 
    m_containerLayout->addWidget(lR);
    m_starRating = new StarRatingWidget(m_container); 
    m_containerLayout->addWidget(m_starRating);

    QLabel* lC = new QLabel("颜色标记", m_container); 
    lC->setStyleSheet("font-size: 11px; color: #5F5E5A;"); 
    m_containerLayout->addWidget(lC);
    m_colorPicker = new ColorPickerWidget(m_container); 
    m_containerLayout->addWidget(m_colorPicker);

    QLabel* lT = new QLabel("标签 / 关键字", m_container); 
    lT->setStyleSheet("font-size: 11px; color: #5F5E5A;"); 
    m_containerLayout->addWidget(lT);
    
    m_tagContainer = new QWidget(m_container); 
    m_tagFlowLayout = new FlowLayout(m_tagContainer, 0, 4, 4); 
    m_containerLayout->addWidget(m_tagContainer);

    m_tagEdit = new QLineEdit(m_container); 
    m_tagEdit->setPlaceholderText("添加标签按 Enter..."); 
    m_tagEdit->setFixedHeight(24); 
    m_tagEdit->setStyleSheet("QLineEdit { background: #2B2B2B; border: 1px solid #333333; border-radius: 6px; padding-left: 6px; font-size: 12px; color: #EEEEEE; }");
    connect(m_tagEdit, &QLineEdit::returnPressed, this, &MetaPanel::onTagAdded);
    m_containerLayout->addWidget(m_tagEdit);

    QLabel* lN = new QLabel("备注", m_container); 
    lN->setStyleSheet("font-size: 11px; color: #5F5E5A;"); 
    m_containerLayout->addWidget(lN);
    m_noteEdit = new QPlainTextEdit(m_container); 
    m_noteEdit->setMinimumHeight(80); 
    m_noteEdit->setMaximumHeight(160); 
    m_noteEdit->setStyleSheet("QPlainTextEdit { background: #2B2B2B; border: 1px solid #333333; border-radius: 6px; font-size: 12px; padding: 6px; color: #EEEEEE; }");
    m_containerLayout->addWidget(m_noteEdit);

    m_containerLayout->addWidget(createSeparator());

    btnEncrypt = new QPushButton("加密保护", m_container); 
    btnDecrypt = new QPushButton("解除加密", m_container); 
    btnChangePwd = new QPushButton("修改密码", m_container);
    for (auto b : {btnEncrypt, btnDecrypt, btnChangePwd}) { 
        b->setFixedHeight(32); 
        // 物理还原：功能按钮对齐 6px 圆角规范
        b->setStyleSheet("QPushButton { background: #378ADD; border: none; border-radius: 6px; color: white; font-weight: 500; } QPushButton:hover { background: #4A9BEF; }"); 
        m_containerLayout->addWidget(b); 
    }
    
    // 连接内部信号并向上转发
    connect(chkPinned, &QCheckBox::clicked, this, [this](bool checked) {
        QString currentPath = lblPath->text();
        if (currentPath != "-" && !currentPath.isEmpty()) {
            QFileInfo info(currentPath);
            AmMetaJson meta(info.absolutePath().toStdWString());
            meta.load();
            meta.items()[info.fileName().toStdWString()].pinned = checked;
            meta.save();
        }
    });

    connect(m_starRating, &StarRatingWidget::ratingChanged, [this](int r) {
        emit metadataChanged(r, L"__NO_CHANGE__");
    });
    connect(m_colorPicker, &ColorPickerWidget::colorChanged, [this](const std::wstring& c) {
        emit metadataChanged(-1, c);
    });

    m_containerLayout->addStretch(); 
    m_scrollArea->setWidget(m_container); 
    m_mainLayout->addWidget(m_scrollArea);
}

void MetaPanel::addInfoRow(const QString& label, QLabel*& valueLabel) {
    QWidget* row = new QWidget(m_container); 
    QVBoxLayout* rl = new QVBoxLayout(row); 
    // 2026-03-xx 回归：物理还原 12, 8, 12, 8 的旧版标准边距
    rl->setContentsMargins(12, 8, 12, 8); 
    rl->setSpacing(2);
    QLabel* kl = new QLabel(label, row); 
    kl->setStyleSheet("font-size: 11px; color: #5F5E5A;");
    valueLabel = new QLabel("-", row); 
    valueLabel->setWordWrap(true); 
    valueLabel->setStyleSheet("font-size: 13px; color: #EEEEEE;");
    rl->addWidget(kl); 
    rl->addWidget(valueLabel); 
    m_containerLayout->addWidget(row);
}

QFrame* MetaPanel::createSeparator() { 
    QFrame* l = new QFrame(this); 
    l->setFrameShape(QFrame::HLine); 
    l->setFixedHeight(1); 
    l->setStyleSheet("background-color: #333333; border: none;"); 
    return l; 
}

void MetaPanel::onTagAdded() {
    QString text = m_tagEdit->text().trimmed();
    if (!text.isEmpty()) {
        TagPill* pill = new TagPill(text, m_tagContainer);
        connect(pill, &TagPill::deleteRequested, this, &MetaPanel::onTagDeleted);
        m_tagFlowLayout->addWidget(pill);
        m_tagEdit->clear();

        // [持久化写入] 将标签物理保存至 .am-meta.json
        QString currentPath = lblPath->text();
        if (currentPath != "-" && !currentPath.isEmpty()) {
            QFileInfo info(currentPath);
            AmMetaJson meta(info.absolutePath().toStdWString());
            meta.load();
            auto& tags = meta.items()[info.fileName().toStdWString()].tags;
            std::wstring wText = text.toStdWString();
            if (std::find(tags.begin(), tags.end(), wText) == tags.end()) {
                tags.push_back(wText);
                meta.save();
            }
        }
    }
}

void MetaPanel::onTagDeleted(const QString& text) {
    for (int i = 0; i < m_tagFlowLayout->count(); ++i) {
        QLayoutItem* item = m_tagFlowLayout->itemAt(i);
        TagPill* pill = qobject_cast<TagPill*>(item->widget());
        if (pill && pill->property("tagText").toString() == text) {
            m_tagFlowLayout->takeAt(i);
            pill->deleteLater();
            delete item;

            // [持久化写入] 将标签从 .am-meta.json 中剥除
            QString currentPath = lblPath->text();
            if (currentPath != "-" && !currentPath.isEmpty()) {
                QFileInfo info(currentPath);
                AmMetaJson meta(info.absolutePath().toStdWString());
                meta.load();
                auto& tags = meta.items()[info.fileName().toStdWString()].tags;
                std::wstring wText = text.toStdWString();
                auto it = std::find(tags.begin(), tags.end(), wText);
                if (it != tags.end()) {
                    tags.erase(it);
                    meta.save();
                }
            }
            return;
        }
    }
}

void MetaPanel::updateInfo(const QString& n, const QString& t, const QString& s, const QString& ct, const QString& mt, const QString& at, const QString& p, bool e) {
    lblName->setText(n); lblType->setText(t); lblSize->setText(s); lblCtime->setText(ct); lblMtime->setText(mt); lblAtime->setText(at); lblPath->setText(p); lblEncrypted->setText(e ? "已加密" : "未加密");
    btnEncrypt->setVisible(!e); btnDecrypt->setVisible(e); btnChangePwd->setVisible(e);
}

void MetaPanel::setRating(int rating) {
    m_starRating->setRating(rating);
}

void MetaPanel::setColor(const std::wstring& color) {
    m_colorPicker->setColor(color);
}

void MetaPanel::setPinned(bool pinned) {
    chkPinned->blockSignals(true);
    chkPinned->setChecked(pinned);
    chkPinned->blockSignals(false);
}

void MetaPanel::setTags(const QStringList& tags) {
    // 清空旧标签
    while (QLayoutItem* item = m_tagFlowLayout->takeAt(0)) {
        if (QWidget* w = item->widget()) {
            w->deleteLater();
        }
        delete item;
    }
    
    for (const QString& tag : tags) {
        TagPill* pill = new TagPill(tag, m_tagContainer);
        connect(pill, &TagPill::deleteRequested, this, &MetaPanel::onTagDeleted);
        m_tagFlowLayout->addWidget(pill);
    }
}

} // namespace ArcMeta
