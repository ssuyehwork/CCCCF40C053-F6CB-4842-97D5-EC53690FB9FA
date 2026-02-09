#include "TagManagerWindow.h"
#include "StringUtils.h"

#include "IconHelper.h"
#include "../core/DatabaseManager.h"
#include "FramelessDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMouseEvent>
#include <QGraphicsDropShadowEffect>
#include <QToolTip>

TagManagerWindow::TagManagerWindow(QWidget* parent) : FramelessDialog("标签管理", parent) {
    resize(430, 580);

    initUI();
    refreshData();
}

TagManagerWindow::~TagManagerWindow() {
}

void TagManagerWindow::initUI() {
    auto* contentLayout = new QVBoxLayout(m_contentArea);
    contentLayout->setContentsMargins(20, 10, 20, 20);
    contentLayout->setSpacing(12);

    // Search Bar
    m_searchEdit = new QLineEdit();
    m_searchEdit->setPlaceholderText("搜索标签...");
    m_searchEdit->setStyleSheet("QLineEdit { background-color: #2D2D2D; border: 1px solid #444; border-radius: 6px; color: white; padding: 6px 10px; font-size: 13px; } "
                               "QLineEdit:focus { border-color: #f1c40f; }");
    connect(m_searchEdit, &QLineEdit::textChanged, this, &TagManagerWindow::handleSearch);
    contentLayout->addWidget(m_searchEdit);

    // Table
    m_tagTable = new QTableWidget(0, 2);
    m_tagTable->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_tagTable->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_tagTable->setHorizontalHeaderLabels({"标签名称", "使用次数"});
    m_tagTable->horizontalHeader()->setStretchLastSection(false);
    m_tagTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tagTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tagTable->verticalHeader()->setVisible(false);
    m_tagTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tagTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tagTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tagTable->setStyleSheet(
        "QTableWidget { background-color: #252526; border: 1px solid #333; border-radius: 6px; color: #CCC; gridline-color: #333; outline: none; } "
        "QTableWidget::item { padding: 5px; } "
        "QTableWidget::item:selected { background-color: #3E3E42; color: #FFF; } "
        "QHeaderView::section { background-color: #2D2D30; color: #888; border: none; height: 30px; font-weight: bold; font-size: 12px; border-bottom: 1px solid #333; }"
    );
    contentLayout->addWidget(m_tagTable);

    // Action Buttons
    auto* btnLayout = new QHBoxLayout();
    
    auto* btnRename = new QPushButton("重命名");
    btnRename->setStyleSheet("QPushButton { background-color: #333; color: #EEE; border: none; border-radius: 4px; padding: 8px 15px; font-weight: bold; } "
                             "QPushButton:hover { background-color: #444; }");
    connect(btnRename, &QPushButton::clicked, this, &TagManagerWindow::handleRename);
    btnLayout->addWidget(btnRename);

    auto* btnDelete = new QPushButton("删除");
    btnDelete->setStyleSheet("QPushButton { background-color: rgba(231, 76, 60, 0.2); color: #e74c3c; border: 1px solid rgba(231, 76, 60, 0.4); border-radius: 4px; padding: 8px 15px; font-weight: bold; } "
                             "QPushButton:hover { background-color: rgba(231, 76, 60, 0.3); }");
    connect(btnDelete, &QPushButton::clicked, this, &TagManagerWindow::handleDelete);
    btnLayout->addWidget(btnDelete);

    contentLayout->addLayout(btnLayout);
}

void TagManagerWindow::refreshData() {
    m_tagTable->setRowCount(0);
    
    QVariantMap filterStats = DatabaseManager::instance().getFilterStats();
    QVariantMap tagStats = filterStats.value("tags").toMap();
    
    QString keyword = m_searchEdit->text().trimmed().toLower();
    
    // Sort by name
    QStringList tagNames = tagStats.keys();
    tagNames.sort();

    for (const QString& name : std::as_const(tagNames)) {
        if (!keyword.isEmpty() && !name.toLower().contains(keyword)) continue;

        int row = m_tagTable->rowCount();
        m_tagTable->insertRow(row);
        
        auto* nameItem = new QTableWidgetItem(name);
        auto* countItem = new QTableWidgetItem(tagStats.value(name).toString());
        countItem->setTextAlignment(Qt::AlignCenter);
        
        m_tagTable->setItem(row, 0, nameItem);
        m_tagTable->setItem(row, 1, countItem);
    }
}

void TagManagerWindow::handleRename() {
    int row = m_tagTable->currentRow();
    if (row < 0) return;

    QString oldName = m_tagTable->item(row, 0)->text();
    auto* dlg = new FramelessInputDialog("重命名标签", "新标签名称:", oldName, this);
    connect(dlg, &FramelessInputDialog::accepted, [this, oldName, dlg](){
        QString newName = dlg->text().trimmed();
        if (!newName.isEmpty() && newName != oldName) {
            if (DatabaseManager::instance().renameTagGlobally(oldName, newName)) {
                QToolTip::showText(QCursor::pos(), StringUtils::wrapToolTip("✅ 标签已重命名并同步至所有笔记"));
                refreshData();
            }
        }
    });
    dlg->show();
}

void TagManagerWindow::handleDelete() {
    int row = m_tagTable->currentRow();
    if (row < 0) return;

    QString tagName = m_tagTable->item(row, 0)->text();
    auto* dlg = new FramelessMessageBox("确认删除", QString("确定要从所有笔记中移除标签 '%1' 吗？").arg(tagName), this);
    connect(dlg, &FramelessMessageBox::confirmed, [this, tagName](){
        if (DatabaseManager::instance().deleteTagGlobally(tagName)) {
            QToolTip::showText(QCursor::pos(), StringUtils::wrapToolTip("✅ 标签已从所有笔记中移除"));
            refreshData();
        }
    });
    dlg->show();
}

void TagManagerWindow::handleSearch(const QString& text) {
    refreshData();
}

