#include "ContentPanel.h"
#include "SvgIcons.h"
#include "TreeItemDelegate.h"
#include "DropTreeView.h"
#include "DropListView.h"
#include "ToolTipOverlay.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QSvgRenderer>
#include <QPainter>
#include <QHeaderView>
#include <QScrollBar>
#include <QStyle>
#include <QLabel>
#include <QAction>
#include <QMenu>
#include <QAbstractItemView>
#include <QStandardItem>
#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QStyleOptionViewItem>
#include <QItemSelectionModel>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QDesktopServices>
#include <QUrl>
#include <QFileIconProvider>
#include <QApplication>
#include <QProcess>
#include <QClipboard>
#include <QMimeData>
#include <QLineEdit>
#include <QTextBrowser>
#include <QInputDialog>
#include <windows.h>
#include <shellapi.h>
#include "../meta/AmMetaJson.h"
#include "../meta/MetadataManager.h"
#include "UiHelper.h"

namespace ArcMeta {

/**
 * @brief 内部代理类：专门处理高级筛选逻辑
 */
class FilterProxyModel : public QSortFilterProxyModel {
public:
    explicit FilterProxyModel(QObject* parent = nullptr) : QSortFilterProxyModel(parent) {}

    FilterState currentFilter;

    void updateFilter() {
        invalidate();
    }

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override {
        QModelIndex idx = sourceModel()->index(sourceRow, 0, sourceParent);
        
        // 1. 评级过滤
        if (!currentFilter.ratings.isEmpty()) {
            int r = idx.data(RatingRole).toInt();
            if (!currentFilter.ratings.contains(r)) return false;
        }

        // 2. 颜色过滤
        if (!currentFilter.colors.isEmpty()) {
            QString c = idx.data(ColorRole).toString();
            if (!currentFilter.colors.contains(c)) return false;
        }

        // 3. 标签过滤
        if (!currentFilter.tags.isEmpty()) {
            QStringList itemTags = idx.data(TagsRole).toStringList();
            bool matchTag = false;
            for (const QString& fTag : currentFilter.tags) {
                if (fTag == "__none__") {
                    if (itemTags.isEmpty()) { matchTag = true; break; }
                } else {
                    if (itemTags.contains(fTag)) { matchTag = true; break; }
                }
            }
            if (!matchTag) return false;
        }

        // 4. 类型过滤
        if (!currentFilter.types.isEmpty()) {
            QString type = idx.data(TypeRole).toString(); // "folder" or "file"
            QString ext = QFileInfo(idx.data(PathRole).toString()).suffix().toUpper();
            bool matchType = false;
            for (const QString& fType : currentFilter.types) {
                if (fType == "folder") {
                    if (type == "folder") { matchType = true; break; }
                } else {
                    if (ext == fType.toUpper()) { matchType = true; break; }
                }
            }
            if (!matchType) return false;
        }

        // 5. 创建日期过滤
        if (!currentFilter.createDates.isEmpty()) {
            QDate d = QFileInfo(idx.data(PathRole).toString()).birthTime().date();
            QDate today = QDate::currentDate();
            QString dStr = d.toString("yyyy-MM-dd");
            bool matchDate = false;
            for (const QString& fDate : currentFilter.createDates) {
                if (fDate == "today" && d == today) { matchDate = true; break; }
                if (fDate == "yesterday" && d == today.addDays(-1)) { matchDate = true; break; }
                if (fDate == dStr) { matchDate = true; break; }
            }
            if (!matchDate) return false;
        }

        // 6. 修改日期过滤
        if (!currentFilter.modifyDates.isEmpty()) {
            QDate d = QFileInfo(idx.data(PathRole).toString()).lastModified().date();
            QDate today = QDate::currentDate();
            QString dStr = d.toString("yyyy-MM-dd");
            bool matchDate = false;
            for (const QString& fDate : currentFilter.modifyDates) {
                if (fDate == "today" && d == today) { matchDate = true; break; }
                if (fDate == "yesterday" && d == today.addDays(-1)) { matchDate = true; break; }
                if (fDate == dStr) { matchDate = true; break; }
            }
            if (!matchDate) return false;
        }

        return true;
    }

    bool lessThan(const QModelIndex& source_left, const QModelIndex& source_right) const override {
        // 核心红线：置顶优先规则
        bool leftPinned = source_left.data(IsLockedRole).toBool();
        bool rightPinned = source_right.data(IsLockedRole).toBool();

        if (leftPinned != rightPinned) {
            if (sortOrder() == Qt::AscendingOrder)
                return leftPinned; 
            else
                return !leftPinned; 
        }

        return QSortFilterProxyModel::lessThan(source_left, source_right);
    }
};


ContentPanel::ContentPanel(QWidget* parent)
    : QFrame(parent) {
    setObjectName("EditorContainer");
    setAttribute(Qt::WA_StyledBackground, true);
    setMinimumWidth(230);
    setStyleSheet("color: #EEEEEE;");

    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);


    m_model = new QStandardItemModel(this);
    m_proxyModel = new FilterProxyModel(this);
    m_proxyModel->setSourceModel(m_model);

    m_zoomLevel = 96;
    m_isRecursive = false;

    initUi();
}

void ContentPanel::setFocusHighlight(bool visible) {
    if (m_focusLine) m_focusLine->setVisible(visible);
}

void ContentPanel::initUi() {
    m_focusLine = new QWidget(this);
    m_focusLine->setFixedHeight(1);
    m_focusLine->setStyleSheet("background-color: #2ecc71;");
    m_focusLine->hide();
    m_mainLayout->addWidget(m_focusLine);

    QWidget* titleBar = new QWidget(this);
    titleBar->setObjectName("ContainerHeader");
    titleBar->setFixedHeight(32);
    titleBar->setStyleSheet(
        "QWidget#ContainerHeader {"
        "  background-color: #252526;"
        "  border-bottom: 1px solid #333;"
        "}"
    );
    QHBoxLayout* titleL = new QHBoxLayout(titleBar);
    titleL->setContentsMargins(15, 2, 15, 0);

    QLabel* iconLabel = new QLabel(titleBar);
    iconLabel->setPixmap(UiHelper::getIcon("eye", QColor("#41F2F2"), 18).pixmap(18, 18));
    titleL->addWidget(iconLabel);

    QLabel* titleLabel = new QLabel("预览数据", titleBar);
    titleLabel->setStyleSheet("font-size: 13px; font-weight: bold; color: #41F2F2; background: transparent; border: none;");
    
    m_btnLayers = new QPushButton(titleBar);
    m_btnLayers->setCheckable(true);
    m_btnLayers->setFixedSize(24, 24);
    m_btnLayers->setIcon(UiHelper::getIcon("layers", QColor("#B0B0B0"), 18));
    m_btnLayers->setToolTip("递归显示子目录所有文件");
    m_btnLayers->setStyleSheet(
        "QPushButton { background: transparent; border: none; border-radius: 4px; }"
        "QPushButton:hover { background: rgba(255, 255, 255, 0.1); }"
        "QPushButton:checked { background: rgba(52, 152, 219, 0.2); border: 1px solid #3498db; }"
        "QPushButton:disabled { opacity: 0.3; }"
    );
    connect(m_btnLayers, &QPushButton::clicked, [this]() {
        if (m_currentPath.isEmpty() || m_currentPath == "computer://") {
            m_btnLayers->setChecked(false);
            return;
        }

        if (m_btnLayers->isChecked()) {
            // 探测是否有子文件夹
            QDir dir(m_currentPath);
            bool hasSubDirs = !dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty();
            if (!hasSubDirs) {
                m_btnLayers->setChecked(false);
                ToolTipOverlay::instance()->showText(QCursor::pos(), "当前文件夹不支持显示子文件夹项目", 1500, QColor("#E81123"));
                return;
            }
            loadDirectory(m_currentPath, true);
        } else {
            loadDirectory(m_currentPath, false);
        }
    });

    titleL->addWidget(titleLabel);
    titleL->addStretch();
    titleL->addWidget(m_btnLayers);

    m_mainLayout->addWidget(titleBar);

    m_viewStack = new QStackedWidget(this);
    
    initGridView();
    initListView();

    m_viewStack->addWidget(m_gridView);
    m_viewStack->addWidget(m_treeView);
    m_viewStack->setCurrentWidget(m_gridView);

    QVBoxLayout* contentWrapper = new QVBoxLayout();
    contentWrapper->setContentsMargins(2, 2, 2, 2);
    contentWrapper->setSpacing(0);
    contentWrapper->addWidget(m_viewStack);
    
    m_mainLayout->addLayout(contentWrapper);

    m_textPreview = new QTextBrowser(this);
    m_textPreview->setStyleSheet("background-color: #1E1E1E; color: #EEEEEE; border: none; padding: 20px; font-family: 'Segoe UI'; font-size: 14px;");
    m_textPreview->hide();
    m_mainLayout->addWidget(m_textPreview, 1);

    m_imagePreview = new QLabel(this);
    m_imagePreview->setStyleSheet("background-color: #1E1E1E; border: none;");
    m_imagePreview->setAlignment(Qt::AlignCenter);
    m_imagePreview->hide();
    m_mainLayout->addWidget(m_imagePreview, 1);

    m_gridView->installEventFilter(this);
}

void ContentPanel::updateGridSize() {
    m_zoomLevel = qBound(32, m_zoomLevel, 128);
    m_gridView->setIconSize(QSize(m_zoomLevel, m_zoomLevel));
    
    int cardW = m_zoomLevel + 30; 
    int cardH = m_zoomLevel + 60; 
    m_gridView->setGridSize(QSize(cardW, cardH));
}

bool ContentPanel::eventFilter(QObject* obj, QEvent* event) {
    if ((obj == m_gridView || obj == m_gridView->viewport()) && event->type() == QEvent::Wheel) {
        QWheelEvent* wEvent = static_cast<QWheelEvent*>(event);
        if (wEvent->modifiers() & Qt::ControlModifier) {
            int delta = wEvent->angleDelta().y();
            if (delta > 0) m_zoomLevel += 8;
            else m_zoomLevel -= 8;
            updateGridSize();
            return true;
        }
    }

    if (event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        QAbstractItemView* view = qobject_cast<QAbstractItemView*>(obj);
        if (!view) view = qobject_cast<QAbstractItemView*>(obj->parent());

        if (qobject_cast<QLineEdit*>(QApplication::focusWidget())) {
            return false;
        }

        if (view) {
            if ((keyEvent->modifiers() & Qt::ControlModifier) && 
                (keyEvent->key() >= Qt::Key_0 && keyEvent->key() <= Qt::Key_5)) {
                
                int rating = keyEvent->key() - Qt::Key_0;
                auto indexes = view->selectionModel()->selectedIndexes();
                for (const auto& idx : indexes) {
                    if (idx.column() == 0) {
                        QString path = idx.data(PathRole).toString();
                        if (!path.isEmpty()) {
                            MetadataManager::instance().setRating(path.toStdWString(), rating);
                            m_proxyModel->setData(idx, rating, RatingRole);
                        }
                    }
                }
                return true;
            }

            if (((keyEvent->modifiers() & Qt::AltModifier) || (keyEvent->modifiers() & (Qt::AltModifier | Qt::WindowShortcut))) && 
                (keyEvent->key() == Qt::Key_D)) {
                auto indexes = view->selectionModel()->selectedIndexes();
                for (const QModelIndex& idx : indexes) {
                    if (idx.column() == 0) {
                        QString itemPath = idx.data(PathRole).toString();
                        if (!itemPath.isEmpty()) {
                            bool current = idx.data(IsLockedRole).toBool();
                            MetadataManager::instance().setPinned(itemPath.toStdWString(), !current);
                            m_proxyModel->setData(idx, !current, IsLockedRole);
                        }
                    }
                }
                return true;
            }

            if ((keyEvent->modifiers() & Qt::AltModifier) && 
                (keyEvent->key() >= Qt::Key_1 && keyEvent->key() <= Qt::Key_9)) {
                
                QString color;
                switch (keyEvent->key()) {
                    case Qt::Key_1: color = "red"; break;
                    case Qt::Key_2: color = "orange"; break;
                    case Qt::Key_3: color = "yellow"; break;
                    case Qt::Key_4: color = "green"; break;
                    case Qt::Key_5: color = "cyan"; break;
                    case Qt::Key_6: color = "blue"; break;
                    case Qt::Key_7: color = "purple"; break;
                    case Qt::Key_8: color = "gray"; break;
                    case Qt::Key_9: color = ""; break;
                }

                auto indexes = view->selectionModel()->selectedIndexes();
                for (const auto& idx : indexes) {
                    if (idx.column() == 0) {
                        QString path = idx.data(PathRole).toString();
                        if (!path.isEmpty()) {
                            MetadataManager::instance().setColor(path.toStdWString(), color.toStdWString());
                            m_proxyModel->setData(idx, color, ColorRole);
                        }
                    }
                }
                return true;
            }

            if (keyEvent->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier)) {
                if (keyEvent->key() == Qt::Key_C) {
                    QStringList paths;
                    auto indexes = view->selectionModel()->selectedIndexes();
                    for (const auto& idx : indexes) if (idx.column() == 0) paths << QDir::toNativeSeparators(idx.data(PathRole).toString());
                    if (!paths.isEmpty()) QApplication::clipboard()->setText(paths.join("\r\n"));
                    return true;
                }
            }

            if (keyEvent->key() == Qt::Key_F2) {
                view->edit(view->currentIndex());
                return true;
            }
            if (keyEvent->key() == Qt::Key_Delete) {
                onCustomContextMenuRequested(view->mapFromGlobal(QCursor::pos()));
                return true;
            }
            
            if (keyEvent->modifiers() & Qt::ControlModifier) {
                if (keyEvent->key() == Qt::Key_C && !(keyEvent->modifiers() & Qt::ShiftModifier)) {
                    QList<QUrl> urls;
                    auto indexes = view->selectionModel()->selectedIndexes();
                    for (const auto& idx : indexes) if (idx.column() == 0) urls << QUrl::fromLocalFile(idx.data(PathRole).toString());
                    if (!urls.isEmpty()) {
                        QMimeData* mime = new QMimeData();
                        mime->setUrls(urls);
                        QApplication::clipboard()->setMimeData(mime);
                    }
                    return true;
                }
                if (keyEvent->key() == Qt::Key_V) {
                    const QMimeData* mime = QApplication::clipboard()->mimeData();
                    if (mime && mime->hasUrls()) {
                        QList<QUrl> urls = mime->urls();
                        std::wstring fromPaths;
                        for (const QUrl& url : urls) {
                            fromPaths += QDir::toNativeSeparators(url.toLocalFile()).toStdWString() + L'\0';
                        }
                        if (!fromPaths.empty()) {
                            fromPaths += L'\0';
                            std::wstring toPath = m_currentPath.toStdWString() + L'\0' + L'\0';
                            SHFILEOPSTRUCTW fileOp = { 0 };
                            fileOp.wFunc = FO_COPY;
                            fileOp.pFrom = fromPaths.c_str();
                            fileOp.pTo = toPath.c_str();
                            fileOp.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION;
                            if (SHFileOperationW(&fileOp) == 0) loadDirectory(m_currentPath);
                        }
                    }
                    return true;
                }
            }

            if (keyEvent->key() == Qt::Key_Space) {
                QModelIndex idx = view->currentIndex();
                if (idx.isValid()) emit requestQuickLook(idx.data(PathRole).toString());
                return true;
            }
            if (keyEvent->key() == Qt::Key_Backspace) {
                QDir dir(m_currentPath);
                if (dir.cdUp()) emit directorySelected(dir.absolutePath());
                return true;
            }
            if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
                onDoubleClicked(view->currentIndex());
                return true;
            }
            if (keyEvent->modifiers() & Qt::ControlModifier && keyEvent->key() == Qt::Key_Backslash) {
                setViewMode(m_viewStack->currentIndex() == 0 ? ListView : GridView);
                return true;
            }
        }
    }
    return QWidget::eventFilter(obj, event);
}

void ContentPanel::wheelEvent(QWheelEvent* event) {
    if (event->modifiers() & Qt::ControlModifier) {
        int delta = event->angleDelta().y();
        if (delta > 0) m_zoomLevel += 8;
        else m_zoomLevel -= 8;
        updateGridSize();
        event->accept();
    } else {
        QWidget::wheelEvent(event);
    }
}

void ContentPanel::setViewMode(ViewMode mode) {
    if (mode == GridView) m_viewStack->setCurrentWidget(m_gridView);
    else m_viewStack->setCurrentWidget(m_treeView);
}

void ContentPanel::initGridView() {
    m_gridView = new DropListView(this);
    m_gridView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_gridView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_gridView->setViewMode(QListView::IconMode);
    m_gridView->setMovement(QListView::Static);
    m_gridView->setSpacing(8);
    m_gridView->setResizeMode(QListView::Adjust);
    m_gridView->setWrapping(true);
    m_gridView->setIconSize(QSize(96, 96));
    m_gridView->setGridSize(QSize(126, 156)); 
    m_gridView->setSpacing(0);
    m_gridView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_gridView->setContextMenuPolicy(Qt::CustomContextMenu);

    m_gridView->setDragEnabled(true);
    m_gridView->setDragDropMode(QAbstractItemView::DragOnly);

    m_gridView->setEditTriggers(QAbstractItemView::EditKeyPressed | QAbstractItemView::SelectedClicked);

    m_gridView->setModel(m_proxyModel);
    m_gridView->setItemDelegate(new GridItemDelegate(this));
    m_gridView->viewport()->installEventFilter(this);

    connect(m_gridView, &QListView::doubleClicked, this, &ContentPanel::onDoubleClicked);

    m_gridView->setStyleSheet(
        "QListView { background-color: transparent; border: none; outline: none; }"
        "QListView::item { background: transparent; }"
        "QListView::item:selected { background-color: rgba(55, 138, 221, 0.2); border-radius: 8px; }"
        "QListView QLineEdit { background-color: #2D2D2D; color: #FFFFFF; border: 1px solid #378ADD; border-radius: 6px; padding: 2px; selection-background-color: #378ADD; selection-color: #FFFFFF; }"
    );

    connect(m_gridView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &ContentPanel::onSelectionChanged);
    connect(m_gridView, &QListView::customContextMenuRequested, this, &ContentPanel::onCustomContextMenuRequested);
}

void ContentPanel::initListView() {
    m_treeView = new DropTreeView(this);
    m_treeView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_treeView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_treeView->setSortingEnabled(true);
    m_treeView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_treeView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    
    m_treeView->setDragEnabled(true);
    m_treeView->setDragDropMode(QAbstractItemView::DragOnly);

    m_treeView->setExpandsOnDoubleClick(false);
    m_treeView->setRootIsDecorated(false);
    
    m_treeView->setItemDelegate(new TreeItemDelegate(this));

    m_treeView->setModel(m_proxyModel);
    m_treeView->viewport()->installEventFilter(this);

    m_treeView->setStyleSheet(
        "QTreeView { background-color: transparent; border: none; outline: none; font-size: 12px; }"
        "QTreeView::item { height: 28px; color: #EEEEEE; padding-left: 0px; }"
        "QTreeView QLineEdit { background-color: #2D2D2D; color: #FFFFFF; border: 1px solid #378ADD; border-radius: 6px; padding: 2px; selection-background-color: #378ADD; selection-color: #FFFFFF; }"
    );

    m_treeView->header()->setStyleSheet(
        "QHeaderView::section { background-color: #252525; color: #B0B0B0; padding-left: 10px; border: none; height: 32px; font-size: 11px; }"
    );

    connect(m_treeView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &ContentPanel::onSelectionChanged);
    connect(m_treeView, &QTreeView::customContextMenuRequested, this, &ContentPanel::onCustomContextMenuRequested);
    connect(m_treeView, &QTreeView::doubleClicked, this, &ContentPanel::onDoubleClicked);
}

void ContentPanel::onCustomContextMenuRequested(const QPoint& pos) {
    QMenu menu(this);
    menu.setStyleSheet(
        "QMenu { background-color: #2D2D2D; color: #EEE; border: 1px solid #444; padding: 4px; border-radius: 8px; }"
        "QMenu::item { padding: 6px 25px 6px 10px; border-radius: 4px; font-size: 12px; }"
        "QMenu::item:selected { background-color: #3E3E42; color: white; }"
        "QMenu::separator { height: 1px; background: #444; margin: 4px 8px; }"
        "QMenu::right-arrow { image: url(data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHZpZXdCb3g9IjAgMCAyNCAyNCIgZmlsbD0ibm9uZSIgc3Ryb2tlPSIjRUVFRUVFIiBzdHJva2Utd2lkdGg9IjIiIHN0cm9rZS1saW5lY2FwPSJyb3VuZCIgc3Ryb2tlLWxpbmVqb2luPSJyb3VuZCI+PHBvbHlsaW5lIHBvaW50cz0iOSAxOCAxNSAxMiA5IDYiPjwvcG9seWxpbmU+PC9zdmc+); width: 12px; height: 12px; right: 8px; }"
    );

    menu.addAction("打开");
    menu.addAction("用系统默认程序打开");
    menu.addAction("在“资源管理器”中显示");
    menu.addSeparator();

    QMenu* newMenu = menu.addMenu("新建...");
    newMenu->setIcon(UiHelper::getIcon("ruler_spacing", QColor("#EEEEEE")));
    QAction* actNewFolder = newMenu->addAction(UiHelper::getIcon("folder", QColor("#EEEEEE")), "创建文件夹");
    QAction* actNewMd     = newMenu->addAction(UiHelper::getIcon("text", QColor("#EEEEEE")), "创建 Markdown");
    QAction* actNewTxt    = newMenu->addAction(UiHelper::getIcon("text", QColor("#EEEEEE")), "创建纯文本文件 (txt)");
    menu.addSeparator();
    
    QMenu* categorizeMenu = menu.addMenu("归类到...");
    categorizeMenu->addAction("（暂无分类）"); 
    
    menu.addSeparator();
    
    menu.addAction("置顶 / 取消置顶");
    QMenu* cryptoMenu = menu.addMenu("加密保护");
    cryptoMenu->addAction("加密保护");
    cryptoMenu->addAction("解除加密");
    cryptoMenu->addAction("修改密码");
    
    menu.addSeparator();
    menu.addAction("批量重命名 (Ctrl+Shift+R)");
    menu.addSeparator();
    
    menu.addAction("重命名");
    menu.addAction("复制");
    menu.addAction("剪切");
    menu.addAction("粘贴");
    menu.addAction("删除（移入回收站）");
    menu.addSeparator();
    
    menu.addAction("复制路径");
    menu.addAction("属性");

    QAbstractItemView* view = qobject_cast<QAbstractItemView*>(sender());
    if (!view) return;
    
    QAction* selectedAction = menu.exec(view->viewport()->mapToGlobal(pos));
    if (!selectedAction) return;

    QString actionText = selectedAction->text();
    QModelIndex currentIndex = view->indexAt(pos);
    QString path = currentIndex.data(PathRole).toString();

    if (actionText == "在资源管理器中显示") {
        QStringList args;
        args << "/select," << QDir::toNativeSeparators(path);
        QProcess::startDetached("explorer", args);
    } else if (selectedAction == actNewFolder) {
        createNewItem("folder");
    } else if (selectedAction == actNewMd) {
        createNewItem("md");
    } else if (selectedAction == actNewTxt) {
        createNewItem("txt");
    } else if (actionText == "复制路径") {
        QApplication::clipboard()->setText(QDir::toNativeSeparators(path));
    } else if (actionText == "重命名") {
        view->edit(currentIndex);
    } else if (actionText.startsWith("删除")) {
        std::wstring wpath = path.toStdWString() + L'\0' + L'\0';
        SHFILEOPSTRUCTW fileOp = { 0 };
        fileOp.wFunc = FO_DELETE;
        fileOp.pFrom = wpath.c_str();
        fileOp.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION;
        if (SHFileOperationW(&fileOp) == 0) loadDirectory(m_currentPath);
    } else if (actionText == "置顶 / 取消置顶") {
        auto indexes = view->selectionModel()->selectedIndexes();
        for (const QModelIndex& idx : indexes) {
            if (idx.column() == 0) {
                QString itemPath = idx.data(PathRole).toString();
                if (!itemPath.isEmpty()) {
                    bool current = idx.data(IsLockedRole).toBool();
                    MetadataManager::instance().setPinned(itemPath.toStdWString(), !current);
                    m_proxyModel->setData(idx, !current, IsLockedRole);
                }
            }
        }
    } else if (actionText == "属性") {
        SHELLEXECUTEINFOW sei = { sizeof(sei) };
        sei.fMask = SEE_MASK_INVOKEIDLIST;
        sei.lpVerb = L"properties";
        std::wstring wpath = path.toStdWString();
        sei.lpFile = wpath.c_str();
        sei.nShow = SW_SHOW;
        ShellExecuteExW(&sei);
    } else if (actionText == "打开" || actionText == "用系统默认程序打开") {
        onDoubleClicked(currentIndex);
    }
}

void ContentPanel::onSelectionChanged() {
    QItemSelectionModel* selectionModel = (m_viewStack->currentWidget() == m_gridView) ? m_gridView->selectionModel() : m_treeView->selectionModel();
    if (!selectionModel) return;

    QStringList selectedPaths;
    QModelIndexList indices = selectionModel->selectedIndexes();
    for (const QModelIndex& index : indices) {
        if (index.column() == 0) {
            QString path = index.data(PathRole).toString();
            if (!path.isEmpty()) selectedPaths.append(path);
        }
    }
    emit selectionChanged(selectedPaths);
}

void ContentPanel::onDoubleClicked(const QModelIndex& index) {
    if (!index.isValid()) return;
    QString path = index.data(PathRole).toString();
    if (path.isEmpty()) return;

    QFileInfo info(path);
    if (info.isDir()) {
        emit directorySelected(path); 
    } else {
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    }
}

void ContentPanel::loadDirectory(const QString& path, bool recursive) {
    if (m_viewStack) m_viewStack->show();
    if (m_textPreview) m_textPreview->hide();
    if (m_imagePreview) m_imagePreview->hide();

    m_isRecursive = recursive;
    if (m_btnLayers) m_btnLayers->setChecked(recursive);

    m_model->clear();
    m_model->setHorizontalHeaderLabels({"名称", "大小", "类型", "修改时间"});

    if (path.isEmpty() || path == "computer://") {
        m_currentPath = "computer://";
        updateLayersButtonState();
        QFileIconProvider iconProvider;
        const auto drives = QDir::drives();
        
        QMap<int, int>     ratingCounts;
        QMap<QString, int> colorCounts;
        QMap<QString, int> tagCounts;
        QMap<QString, int> typeCounts;
        QMap<QString, int> createDateCounts;
        QMap<QString, int> modifyDateCounts;

        for (const QFileInfo& drive : drives) {
            QString drivePath = drive.absolutePath();
            auto* item = new QStandardItem(iconProvider.icon(drive), drivePath);
            item->setData(drivePath, PathRole);
            item->setData("folder", TypeRole);
            item->setData(0, RatingRole);
            item->setData("", ColorRole);
            item->setData(false, IsLockedRole);

            QList<QStandardItem*> row;
            row << item;
            row << new QStandardItem("-");
            row << new QStandardItem("磁盘分区");
            row << new QStandardItem("-");
            m_model->appendRow(row);

            typeCounts["folder"]++;
        }
        emit directoryStatsReady(ratingCounts, colorCounts, tagCounts, typeCounts,
                                  createDateCounts, modifyDateCounts);
        return;
    }

    m_currentPath = path;
    updateLayersButtonState();
    
    QMap<int, int>     ratingCounts;
    QMap<QString, int> colorCounts;
    QMap<QString, int> tagCounts;
    QMap<QString, int> typeCounts;
    QMap<QString, int> createDateCounts;
    QMap<QString, int> modifyDateCounts;
    int noTagCount = 0;

    addItemsFromDirectory(path, recursive, ratingCounts, colorCounts, tagCounts, typeCounts, createDateCounts, modifyDateCounts, noTagCount);

    applyFilters();

    if (noTagCount > 0) tagCounts["__none__"] = noTagCount;
    emit directoryStatsReady(ratingCounts, colorCounts, tagCounts, typeCounts,
                              createDateCounts, modifyDateCounts);
}

void ContentPanel::addItemsFromDirectory(const QString& path, bool recursive,
                                       QMap<int, int>& ratingCounts,
                                       QMap<QString, int>& colorCounts,
                                       QMap<QString, int>& tagCounts,
                                       QMap<QString, int>& typeCounts,
                                       QMap<QString, int>& createDateCounts,
                                       QMap<QString, int>& modifyDateCounts,
                                       int& noTagCount) 
{
    QDir dir(path);
    if (!dir.exists()) return;

    QFileInfoList entries = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::DirsFirst | QDir::Name);
    QFileIconProvider iconProvider;

    QDate today    = QDate::currentDate();
    QDate yesterday = today.addDays(-1);

    auto dateKey = [&](const QDate& d) -> QString {
        if (d == today)     return "today";
        if (d == yesterday) return "yesterday";
        return d.toString("yyyy-MM-dd");
    };

    for (const QFileInfo& info : entries) {
        QString fileName = info.fileName();
        if (fileName == ".am_meta.json" || fileName == ".am_meta.json.tmp") continue;

        QString fullPath = info.absoluteFilePath();
        QList<QStandardItem*> row;
        auto* nameItem = new QStandardItem(iconProvider.icon(info), fileName);
        nameItem->setData(fullPath, PathRole);
        nameItem->setData(info.isDir() ? "folder" : "file", TypeRole);

        RuntimeMeta runtimeMeta = MetadataManager::instance().getMeta(fullPath.toStdWString());
        
        int     itemRating = runtimeMeta.rating;
        QString itemColor  = QString::fromStdWString(runtimeMeta.color);
        QStringList itemTags = runtimeMeta.tags;
        bool    hasTags = !itemTags.isEmpty();

        nameItem->setData(itemRating, RatingRole);
        nameItem->setData(itemColor, ColorRole);
        nameItem->setData(runtimeMeta.pinned, IsLockedRole);
        nameItem->setData(runtimeMeta.encrypted, EncryptedRole);
        nameItem->setData(itemTags, TagsRole);

        for (const auto& t : itemTags) tagCounts[t]++;

        ratingCounts[itemRating]++;
        colorCounts[itemColor]++;
        if (!hasTags) noTagCount++;
        typeCounts[info.isDir() ? "folder" : info.suffix().toUpper()]++;
        createDateCounts[dateKey(info.birthTime().date())]++;
        modifyDateCounts[dateKey(info.lastModified().date())]++;

        row << nameItem;
        row << new QStandardItem(info.isDir() ? "-" : QString::number(info.size() / 1024) + " KB");
        row << new QStandardItem(info.isDir() ? "文件夹" : info.suffix().toUpper() + " 文件");
        row << new QStandardItem(info.lastModified().toString("yyyy-MM-dd HH:mm"));
        m_model->appendRow(row);

        if (recursive && info.isDir()) {
            addItemsFromDirectory(fullPath, true, ratingCounts, colorCounts, tagCounts, typeCounts, createDateCounts, modifyDateCounts, noTagCount);
        }
    }
}



void ContentPanel::search(const QString& query) {
    // 2026-03-xx 按照用户最新要求：物理移除 MFT 引擎及其相关搜索逻辑。
    // 此处暂不实现替代方案，仅保留函数接口以防编译报错，后续可按需实现基于 QDirIterator 的简单过滤。
    if (m_viewStack) m_viewStack->show();
    if (m_textPreview) m_textPreview->hide();
    if (m_imagePreview) m_imagePreview->hide();

    Q_UNUSED(query);
    m_model->clear();
    m_model->setHorizontalHeaderLabels({"名称", "路径", "类型", "修改时间"});
}

void ContentPanel::applyFilters(const FilterState& state) {
    m_currentFilter = state;
    applyFilters();
}

void ContentPanel::applyFilters() {
    auto* proxy = static_cast<FilterProxyModel*>(m_proxyModel);
    proxy->currentFilter = m_currentFilter;
    proxy->updateFilter();
}

void ContentPanel::previewFile(const QString& path) {
    // 2026-03-xx 按照用户要求：全能预览实现，支持图片与多种文本格式，破除 .md 局限
    QFileInfo info(path);
    QString ext = info.suffix().toLower();

    // 1. 图片格式识别
    static const QStringList imageExts = {"jpg", "jpeg", "png", "bmp", "webp", "gif", "ico"};
    if (imageExts.contains(ext)) {
        QPixmap pix(path);
        if (!pix.isNull()) {
            m_viewStack->hide();
            m_textPreview->hide();
            
            // 保持比例缩放显示
            m_imagePreview->setPixmap(pix.scaled(m_imagePreview->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
            m_imagePreview->show();
            return;
        }
    }

    // 2. 文本格式识别 (参考版本A 扩展识别)
    // 此处可根据需要进一步细化，目前先处理常规文本
    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        m_viewStack->hide();
        m_imagePreview->hide();

        // 针对 Markdown 特殊渲染
        if (ext == "md" || ext == "markdown") {
             m_textPreview->setMarkdown(file.readAll());
        } else {
             // 针对其他代码或文本，直接显示原文
             // 限制读取前 1MB 以防大文件卡死
             m_textPreview->setPlainText(QString::fromUtf8(file.read(1024 * 1024)));
        }
        m_textPreview->show();
        file.close();
    }
}

void ContentPanel::loadPaths(const QStringList& paths) {
    m_viewStack->show();
    if (m_textPreview) m_textPreview->hide();
    if (m_imagePreview) m_imagePreview->hide();
    
    m_model->clear();
    m_model->setHorizontalHeaderLabels({"名称", "大小", "类型", "修改时间"});
    
    QFileIconProvider iconProvider;
    for (const QString& path : paths) {
        QFileInfo info(path);
        if (!info.exists()) continue;

        QList<QStandardItem*> row;
        auto* nameItem = new QStandardItem(iconProvider.icon(info), info.fileName());
        nameItem->setData(path, PathRole);
        nameItem->setData(info.isDir() ? "folder" : "file", TypeRole);
        
        RuntimeMeta rm = MetadataManager::instance().getMeta(path.toStdWString());
        nameItem->setData(rm.rating, RatingRole);
        nameItem->setData(QString::fromStdWString(rm.color), ColorRole);
        nameItem->setData(rm.pinned, IsLockedRole);
        nameItem->setData(rm.tags, TagsRole);

        row << nameItem;
        row << new QStandardItem(info.isDir() ? "-" : QString::number(info.size() / 1024) + " KB");
        row << new QStandardItem(info.isDir() ? "文件夹" : info.suffix().toUpper() + " 文件");
        row << new QStandardItem(info.lastModified().toString("yyyy-MM-dd HH:mm"));
        m_model->appendRow(row);
    }
    applyFilters();
}

void ContentPanel::createNewItem(const QString& type) {
    if (m_currentPath.isEmpty() || m_currentPath == "computer://") return;

    QString baseName = (type == "folder") ? "新建文件夹" : "未命名";
    QString ext = (type == "md") ? ".md" : ((type == "txt") ? ".txt" : "");
    QString finalName = baseName + ext;
    QString fullPath = m_currentPath + "/" + finalName;

    int counter = 1;
    while (QFileInfo::exists(fullPath)) {
        finalName = baseName + QString(" (%1)").arg(counter++) + ext;
        fullPath = m_currentPath + "/" + finalName;
    }

    bool success = false;
    if (type == "folder") {
        success = QDir(m_currentPath).mkdir(finalName);
    } else {
        QFile file(fullPath);
        if (file.open(QIODevice::WriteOnly)) {
            file.close();
            success = true;
        }
    }

    if (success) {
        loadDirectory(m_currentPath, m_isRecursive);
        auto results = m_model->findItems(finalName, Qt::MatchExactly, 0);
        if (!results.isEmpty()) {
            QModelIndex srcIdx = results.first()->index();
            QModelIndex proxyIdx = m_proxyModel->mapFromSource(srcIdx);
            if (proxyIdx.isValid()) {
                m_gridView->setCurrentIndex(proxyIdx);
                m_gridView->edit(proxyIdx);
            }
        }
    }
}

void ContentPanel::updateLayersButtonState() {
    if (!m_btnLayers) return;

    if (m_currentPath.isEmpty() || m_currentPath == "computer://") {
        m_btnLayers->setEnabled(false);
        m_btnLayers->setChecked(false);
        m_btnLayers->setToolTip("“此电脑”不支持递归显示");
        return;
    }

    m_btnLayers->setEnabled(true);
    m_btnLayers->setToolTip("递归显示子目录所有文件");
}

// --- Delegate ---

void GridItemDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    QRect cardRect = option.rect.adjusted(4, 4, -4, -4);
    bool isSelected = (option.state & QStyle::State_Selected);
    bool isHovered = (option.state & QStyle::State_MouseOver);
    
    QColor cardBg = isSelected ? QColor("#282828") : (isHovered ? QColor("#2A2A2A") : QColor("#2D2D2D"));
    painter->setPen(isSelected ? QPen(QColor("#3498db"), 2) : QPen(QColor("#333333"), 1));
    painter->setBrush(cardBg);
    painter->drawRoundedRect(cardRect, 8, 8);

    QString path = index.data(PathRole).toString();
    QFileInfo info(path);
    QString ext = info.isDir() ? "DIR" : info.suffix().toUpper();
    if (ext.isEmpty()) ext = "FILE";
    QColor badgeColor = UiHelper::getExtensionColor(ext);

    QRect extRect(cardRect.left() + 8, cardRect.top() + 8, 36, 18);
    painter->setPen(Qt::NoPen);
    painter->setBrush(badgeColor);
    painter->drawRoundedRect(extRect, 2, 2);
    painter->setPen(QColor("#FFFFFF"));
    QFont extFont = painter->font();
    extFont.setPointSize(8);
    extFont.setBold(true);
    painter->setFont(extFont);
    painter->drawText(extRect, Qt::AlignCenter, ext);

    int baseIconSize = option.decorationSize.width();
    if (baseIconSize <= 0) baseIconSize = 64; 
    int iconDrawSize = static_cast<int>(baseIconSize * 0.65); 
    
    int ratingH = 12;
    int nameH = 18;
    int gap1 = 6;
    int gap2 = 4;
    
    int totalH = iconDrawSize + gap1 + ratingH + gap2 + nameH;
    int startY = cardRect.top() + (cardRect.height() - totalH) / 2 + 13;

    QRect iconRect(cardRect.left() + (cardRect.width() - iconDrawSize) / 2, startY, iconDrawSize, iconDrawSize);
    QIcon icon = index.data(Qt::DecorationRole).value<QIcon>();
    icon.paint(painter, iconRect);

    int ratingY = iconRect.bottom() + gap1;
    int rating = index.data(RatingRole).toInt();
    
    int starSize = 12; 
    int starSpacing = 1; 
    int banW = 12;
    int banGap = 4;
    int infoTotalW = banW + banGap + (5 * starSize) + (4 * starSpacing);
    int infoStartX = cardRect.left() + (cardRect.width() - infoTotalW) / 2;

    QRect banRect(infoStartX, ratingY + (ratingH - banW) / 2, banW, banW);
    QIcon banIcon = UiHelper::getIcon("no_color", QColor("#555555"), banW);
    banIcon.paint(painter, banRect);

    int starsStartX = infoStartX + banW + banGap;
    QPixmap filledStar = UiHelper::getPixmap("star_filled", QSize(starSize, starSize), QColor("#EF9F27"));
    QPixmap emptyStar = UiHelper::getPixmap("star", QSize(starSize, starSize), QColor("#444444"));

    for (int i = 0; i < 5; ++i) {
        QRect starRect(starsStartX + i * (starSize + starSpacing), ratingY + (ratingH - starSize) / 2, starSize, starSize);
        painter->drawPixmap(starRect, (i < rating) ? filledStar : emptyStar);
    }

    int nameY = ratingY + ratingH + gap2;
    QRect nameRect(cardRect.left() + 6, nameY, cardRect.width() - 12, nameH);
    
    QString colorName = index.data(ColorRole).toString();
    if (!colorName.isEmpty()) {
        QColor dotC(colorName);
        if (!dotC.isValid()) {
            if (colorName.contains("red") || colorName.contains("红")) dotC = QColor("#E24B4A");
            else if (colorName.contains("orange") || colorName.contains("橙")) dotC = QColor("#EF9F27");
            else if (colorName.contains("green") || colorName.contains("绿")) dotC = QColor("#639922");
            else if (colorName.contains("blue") || colorName.contains("蓝")) dotC = QColor("#378ADD");
        }
        if (dotC.isValid()) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(dotC);
            painter->drawRoundedRect(nameRect, 2, 2);
            painter->setPen(dotC.lightness() > 180 ? Qt::black : Qt::white);
        } else {
            painter->setPen(QColor("#CCCCCC"));
        }
    } else {
        painter->setPen(QColor("#CCCCCC"));
    }

    QString name = index.data(Qt::DisplayRole).toString();
    QFont textFont = painter->font();
    textFont.setPointSize(8);
    textFont.setBold(false);
    painter->setFont(textFont);
    painter->drawText(nameRect.adjusted(4, 0, -4, 0), Qt::AlignCenter | Qt::ElideRight, name);

    painter->restore();
}

QSize GridItemDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex&) const {
    auto* view = qobject_cast<const QListView*>(option.widget);
    if (view && view->gridSize().isValid()) return view->gridSize();
    return QSize(96, 112);
}

bool GridItemDelegate::eventFilter(QObject* obj, QEvent* event) {
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        QLineEdit* editor = qobject_cast<QLineEdit*>(obj);
        if (editor) {
            switch (keyEvent->key()) {
                case Qt::Key_Left:
                case Qt::Key_Right:
                case Qt::Key_Up:
                case Qt::Key_Down:
                case Qt::Key_Home:
                case Qt::Key_End:
                    keyEvent->accept();
                    return false;
                default:
                    break;
            }
        }
    }
    return QStyledItemDelegate::eventFilter(obj, event);
}

bool GridItemDelegate::editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option, const QModelIndex& index) {
    if (event->type() == QEvent::MouseButtonPress) {
        QMouseEvent* mEvent = static_cast<QMouseEvent*>(event);
        if (mEvent->button() == Qt::LeftButton) {
            int baseIconSize = option.decorationSize.width();
            if (baseIconSize <= 0) baseIconSize = 64; 
            int iconDrawSize = static_cast<int>(baseIconSize * 0.65); 
            int ratingH = 12;
            int nameH = 18;
            int gap1 = 6;
            int gap2 = 4;
            int totalH = iconDrawSize + gap1 + ratingH + gap2 + nameH;
            int startY = option.rect.top() + (option.rect.height() - totalH) / 2 + 13;
            int ratingY = startY + iconDrawSize + gap1;
            int starSize = 12; 
            int starSpacing = 1; 
            int banW = 12;
            int banGap = 4;
            int infoTotalW = banW + banGap + (5 * starSize + 4 * starSpacing);
            int infoStartX = option.rect.left() + (option.rect.width() - infoTotalW) / 2;

            QRect banRect(infoStartX, ratingY + (ratingH - 14) / 2, 14, 14);
            if (banRect.contains(mEvent->pos())) {
                QString path = index.data(PathRole).toString();
                if (!path.isEmpty()) {
                    MetadataManager::instance().setRating(path.toStdWString(), 0);
                    model->setData(index, 0, RatingRole);
                }
                return true;
            }

            int starsStartX = infoStartX + banW + banGap;
            for (int i = 0; i < 5; ++i) {
                QRect starRect(starsStartX + i * (starSize + starSpacing), ratingY + (ratingH - starSize) / 2, starSize, starSize);
                if (starRect.contains(mEvent->pos())) {
                    int r = i + 1;
                    QString path = index.data(PathRole).toString();
                    if (!path.isEmpty()) {
                        MetadataManager::instance().setRating(path.toStdWString(), r);
                        model->setData(index, r, RatingRole);
                    }
                    return true;
                }
            }
        }
    }
    return QStyledItemDelegate::editorEvent(event, model, option, index);
}

QWidget* GridItemDelegate::createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const {
    Q_UNUSED(option);
    QLineEdit* editor = new QLineEdit(parent);
    editor->installEventFilter(const_cast<GridItemDelegate*>(this));
    editor->setAlignment(Qt::AlignCenter);
    editor->setFrame(false);
    
    QString tagColorStr = index.data(ColorRole).toString();
    QString bgColor = tagColorStr.isEmpty() ? "#3E3E42" : tagColorStr;
    QString textColor = tagColorStr.isEmpty() ? "#FFFFFF" : "#000000";

    editor->setStyleSheet(
        QString("QLineEdit { background-color: %1; color: %2; border-radius: 2px; "
                "border: 2px solid #3498db; font-weight: bold; font-size: 8pt; padding: 0px; }")
        .arg(bgColor, textColor)
    );
    return editor;
}

void GridItemDelegate::setEditorData(QWidget* editor, const QModelIndex& index) const {
    QString value = index.model()->data(index, Qt::EditRole).toString();
    QLineEdit* lineEdit = static_cast<QLineEdit*>(editor);
    lineEdit->setText(value);
    
    int lastDot = value.lastIndexOf('.');
    if (lastDot > 0) {
        lineEdit->setSelection(0, lastDot);
    } else {
        lineEdit->selectAll();
    }
}

void GridItemDelegate::setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const {
    QLineEdit* lineEdit = static_cast<QLineEdit*>(editor);
    QString value = lineEdit->text();
    if(value.isEmpty() || value == index.data(Qt::DisplayRole).toString()) return;

    QString oldPath = index.data(PathRole).toString();
    QFileInfo info(oldPath);
    QString newPath = info.absolutePath() + "/" + value;
    
    if (QFile::rename(oldPath, newPath)) {
        model->setData(index, value, Qt::EditRole);
        model->setData(index, newPath, PathRole);
        AmMetaJson::renameItem(info.absolutePath(), info.fileName(), value);
    } 
}

void GridItemDelegate::updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const {
    Q_UNUSED(index);
    QRect cardRect = option.rect.adjusted(4, 4, -4, -4);
    int baseIconSize = option.decorationSize.width();
    if (baseIconSize <= 0) baseIconSize = 64; 
    int iconDrawSize = static_cast<int>(baseIconSize * 0.65); 
    
    int ratingH = 12;
    int nameH = 18;
    int gap1 = 6;
    int gap2 = 4;
    
    int totalH = iconDrawSize + gap1 + ratingH + gap2 + nameH;
    int startY = cardRect.top() + (cardRect.height() - totalH) / 2 + 13;
    int ratingY = startY + iconDrawSize + gap1;
    int nameY = ratingY + ratingH + gap2;
    
    QRect nameBoxRect(cardRect.left() + 6, nameY, cardRect.width() - 12, nameH);
    editor->setGeometry(nameBoxRect);
}

} // namespace ArcMeta
