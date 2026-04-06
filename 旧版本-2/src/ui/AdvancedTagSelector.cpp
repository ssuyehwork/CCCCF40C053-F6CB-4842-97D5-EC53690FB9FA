#include "AdvancedTagSelector.h"
#include "IconHelper.h"
#include <QPushButton>
#include <QLabel>
#include <QKeyEvent>
#include <QGuiApplication>
#include <QScreen>
#include <QGraphicsDropShadowEffect>

AdvancedTagSelector::AdvancedTagSelector(QWidget* parent) : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint) {
    setAttribute(Qt::WA_TranslucentBackground); // 透明背景以便绘制圆角和阴影
    setAttribute(Qt::WA_DeleteOnClose);
    setFixedSize(420, 520); // 增大尺寸以容纳更大的阴影边距

    // 主布局
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(30, 30, 30, 30); // 增加边距，防止阴影被裁切

    // 内部容器 (模拟 Python #mainContainer)
    auto* container = new QWidget(this);
    container->setObjectName("mainContainer");
    container->setStyleSheet(
        "#mainContainer {"
        "  background-color: #1E1E1E;"
        "  border: 1px solid #333;"
        "  border-radius: 8px;"
        "}"
    );

    // 阴影效果: 参考 QuickPreview 的参数并加强，使其更加深邃自然
    auto* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(25);
    shadow->setXOffset(0);
    shadow->setYOffset(6);
    shadow->setColor(QColor(0, 0, 0, 160));
    container->setGraphicsEffect(shadow);

    mainLayout->addWidget(container);

    // 容器布局
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);

    // 1. 搜索框 (对齐 Python 样式: 无边框，底部下划线风格)
    m_search = new QLineEdit();
    m_search->setPlaceholderText("搜索或新建...");
    m_search->setStyleSheet(
        "QLineEdit {"
        "  background-color: #2D2D2D;"
        "  border: none;"
        "  border-bottom: 1px solid #444;"
        "  border-radius: 4px;"
        "  padding: 8px;"
        "  font-size: 13px;"
        "  color: #DDD;"
        "}"
        "QLineEdit:focus { border-bottom: 1px solid #4a90e2; }"
    );
    connect(m_search, &QLineEdit::textChanged, this, &AdvancedTagSelector::updateList);
    connect(m_search, &QLineEdit::returnPressed, this, [this](){
        QString text = m_search->text().trimmed();
        if (!text.isEmpty()) {
            if (!m_selected.contains(text)) {
                m_selected.append(text);
                emit tagsChanged();
                updateList();
            }
            m_search->clear();
        } else {
            emit tagsConfirmed(m_selected);
            close();
        }
    });
    layout->addWidget(m_search);

    // 2. 提示标签
    m_tipsLabel = new QLabel("最近使用");
    m_tipsLabel->setStyleSheet("color: #888; font-size: 12px; font-weight: bold; margin-top: 5px;");
    layout->addWidget(m_tipsLabel);

    // 3. 滚动区域
    auto* scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet(
        "QScrollArea { background: transparent; border: none; }"
        "QScrollBar:vertical { width: 6px; background: transparent; }"
        "QScrollBar::handle:vertical { background: #444; border-radius: 3px; }"
    );

    m_tagContainer = new QWidget();
    m_tagContainer->setStyleSheet("background: transparent;");
    
    // FlowLayout 参数: margin=0, hSpacing=8, vSpacing=8
    m_flow = new FlowLayout(m_tagContainer, 0, 8, 8);
    scroll->setWidget(m_tagContainer);
    layout->addWidget(scroll);
}

void AdvancedTagSelector::setup(const QList<QVariantMap>& recentTags, const QStringList& allTags, const QStringList& selectedTags) {
    m_recentTags = recentTags;
    m_allTags = allTags;
    m_selected = selectedTags;
    updateList();
}

void AdvancedTagSelector::setTags(const QStringList& allTags, const QStringList& selectedTags) {
    m_allTags = allTags;
    m_recentTags.clear();
    for (const QString& t : allTags) {
        QVariantMap m;
        m["name"] = t;
        m["count"] = 0;
        m_recentTags.append(m);
    }
    m_selected = selectedTags;
    updateList();
}

void AdvancedTagSelector::updateList() {
    // 清空现有项
    QLayoutItem* child;
    while ((child = m_flow->takeAt(0)) != nullptr) {
        if (child->widget()) {
            child->widget()->deleteLater();
        }
        delete child;
    }

    QString filter = m_search->text().trimmed();
    QString filterLower = filter.toLower();
    
    QList<QVariantMap> displayList;

    if (filter.isEmpty()) {
        m_tipsLabel->setText(QString("最近使用 (%1)").arg(m_recentTags.count()));
        // 1. 整理显示列表：确保已选中的如果不在最近列表中，也要显示出来
        displayList = m_recentTags;
        QStringList recentNames;
        for(const auto& m : m_recentTags) recentNames << m["name"].toString();
        
        for(const auto& t : m_selected) {
            if (!recentNames.contains(t)) {
                QVariantMap m;
                m["name"] = t;
                m["count"] = 0;
                displayList.append(m);
            }
        }
    } else {
        // 搜索模式：从 m_allTags 中筛选
        for (const QString& tag : m_allTags) {
            if (tag.toLower().contains(filterLower)) {
                QVariantMap m;
                m["name"] = tag;
                // 尝试从 m_recentTags 找匹配的 count
                int count = 0;
                for (const auto& rm : m_recentTags) {
                    if (rm["name"].toString() == tag) {
                        count = rm["count"].toInt();
                        break;
                    }
                }
                m["count"] = count;
                displayList.append(m);
            }
        }
        m_tipsLabel->setText(QString("搜索结果 (%1)").arg(displayList.count()));
    }

    for (const auto& tagData : displayList) {
        QString tag = tagData["name"].toString();
        int count = tagData["count"].toInt();

        bool isSelected = m_selected.contains(tag);
        
        auto* btn = new QPushButton();
        btn->setCheckable(true);
        btn->setChecked(isSelected);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setProperty("tag_name", tag);
        btn->setProperty("tag_count", count);
        
        updateChipState(btn, isSelected);
        
        connect(btn, &QPushButton::clicked, this, [this, btn, tag](){ 
            toggleTag(tag); 
        });
        m_flow->addWidget(btn);
    }
}

void AdvancedTagSelector::updateChipState(QPushButton* btn, bool checked) {
    QString name = btn->property("tag_name").toString();
    int count = btn->property("tag_count").toInt();
    
    QString text = name;
    if (count > 0) text += QString(" (%1)").arg(count);
    btn->setText(text);
    
    QIcon icon = checked ? IconHelper::getIcon("select", "#ffffff", 14) 
                         : IconHelper::getIcon("clock", "#bbbbbb", 14);
    btn->setIcon(icon);
    btn->setIconSize(QSize(14, 14));

    if (checked) {
        btn->setStyleSheet(
            "QPushButton {"
            "  background-color: #4a90e2;"
            "  color: white;"
            "  border: 1px solid #4a90e2;"
            "  border-radius: 14px;"
            "  padding: 6px 12px;"
            "  font-size: 12px;"
            "  font-family: 'Segoe UI', 'Microsoft YaHei';"
            "}"
        );
    } else {
        btn->setStyleSheet(
            "QPushButton {"
            "  background-color: #2D2D2D;"
            "  color: #BBB;"
            "  border: 1px solid #444;"
            "  border-radius: 14px;"
            "  padding: 6px 12px;"
            "  font-size: 12px;"
            "  font-family: 'Segoe UI', 'Microsoft YaHei';"
            "}"
            "QPushButton:hover {"
            "  background-color: #383838;"
            "  border-color: #666;"
            "  color: white;"
            "}"
        );
    }
}

void AdvancedTagSelector::toggleTag(const QString& tag) {
    if (m_selected.contains(tag)) {
        m_selected.removeAll(tag);
    } else {
        m_selected.append(tag);
    }
    emit tagsChanged();
    updateList();
    m_search->setFocus(); // 保持焦点以便继续打字
}

void AdvancedTagSelector::showAtCursor() {
    QPoint pos = QCursor::pos();
    
    QScreen *screen = QGuiApplication::screenAt(pos);
    if (screen) {
        QRect geo = screen->geometry();
        // 考虑 30px 的阴影边距，尝试让容器的左上角靠近鼠标位置
        int x = pos.x() - 40; 
        int y = pos.y() - 10;

        // 边界检查
        if (x + width() > geo.right()) x = geo.right() - width();
        if (x < geo.left()) x = geo.left();
        if (y + height() > geo.bottom()) y = geo.bottom() - height();
        if (y < geo.top()) y = geo.top();
        
        move(x, y);
    }
    show();
    activateWindow();
    m_search->setFocus();
}

void AdvancedTagSelector::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        emit tagsConfirmed(m_selected); // Esc 关闭时也确认并关闭
        close();
    } else {
        QWidget::keyPressEvent(event);
    }
}
