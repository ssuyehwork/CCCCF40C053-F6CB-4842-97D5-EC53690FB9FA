#pragma once

#include <QWidget>
#include <QListView>
#include <QTreeView>
#include <QStackedWidget>
#include <QPushButton>
#include <QTextBrowser>
#include <QStandardItemModel>
#include <QSortFilterProxyModel>
#include <QVBoxLayout>
#include <QStyledItemDelegate>
#include <QMap>
#include "FilterPanel.h"

namespace ArcMeta {

/**
 * @brief 自定义 Role 枚举，用于 QStandardItemModel 数据存取
 */
enum ItemRole {
    RatingRole = Qt::UserRole + 1,
    ColorRole,
    PinnedRole,
    EncryptedRole,
    PathRole,
    IsLockedRole,
    TagsRole,
    TypeRole
};

/**
 * @brief 内容面板（面板四）：核心业务展示区
 * 支持网格视图（QListView）与列表视图（QTreeView）切换
 */
class ContentPanel : public QFrame {
    Q_OBJECT

public:
    enum ViewMode {
        GridView,
        ListView
    };

    explicit ContentPanel(QWidget* parent = nullptr);
    ~ContentPanel() override = default;

    /**
     * @brief 物理还原：设置 1px 翠绿高亮线的显隐状态
     */
    void setFocusHighlight(bool visible);

    /**
     * @brief 切换视图模式
     */
    void setViewMode(ViewMode mode);

    /**
     * @brief 拦截空格键（红线：物理拦截 QEvent::KeyPress 且为 Key_Space）
     */
    bool eventFilter(QObject* obj, QEvent* event) override;

    // --- 业务接口 ---
    QAbstractItemModel* model() const { return m_model; }
    QSortFilterProxyModel* getProxyModel() const { return m_proxyModel; }
    QModelIndexList getSelectedIndexes() const {
        return (m_viewStack->currentWidget() == m_gridView) ? 
                m_gridView->selectionModel()->selectedIndexes() : 
                m_treeView->selectionModel()->selectedIndexes();
    }

signals:
    /**
     * @brief 请求 QuickLook 预览信号
     * @param path 物理路径
     */
    void requestQuickLook(const QString& path);

    /**
     * @brief 选中项发生变化时通知元数据面板刷新
     * @param paths 选中条目的物理路径列表
     */
    void selectionChanged(const QStringList& paths);
    void directorySelected(const QString& path);

    /**
     * @brief 目录装载完成后发出，携带统计数据供 FilterPanel 填充
     */
    void directoryStatsReady(
        const QMap<int, int>&     ratingCounts,
        const QMap<QString, int>& colorCounts,
        const QMap<QString, int>& tagCounts,
        const QMap<QString, int>& typeCounts,
        const QMap<QString, int>& createDateCounts,
        const QMap<QString, int>& modifyDateCounts);

private:
    void initUi();
    void initGridView();
    void initListView();
    void setupContextMenu();
    void updateLayersButtonState();

    QVBoxLayout* m_mainLayout = nullptr;
    QWidget* m_focusLine = nullptr;
    QStackedWidget* m_viewStack = nullptr;
    QPushButton* m_btnLayers = nullptr;
    QTextBrowser* m_textPreview = nullptr;
    QLabel* m_imagePreview = nullptr;

    // 视图组件
    QListView* m_gridView = nullptr;
    QTreeView* m_treeView = nullptr;
    QStandardItemModel* m_model = nullptr;
    QSortFilterProxyModel* m_proxyModel = nullptr;

    FilterState m_currentFilter;

    int m_zoomLevel = 64;
    QString m_currentPath;
    bool m_isRecursive = false;
    void updateGridSize();

    void addItemsFromDirectory(const QString& path, bool recursive,
                               QMap<int, int>& ratingCounts,
                               QMap<QString, int>& colorCounts,
                               QMap<QString, int>& tagCounts,
                               QMap<QString, int>& typeCounts,
                               QMap<QString, int>& createDateCounts,
                               QMap<QString, int>& modifyDateCounts,
                               int& noTagCount);

public slots:
    void onSelectionChanged();
    void onCustomContextMenuRequested(const QPoint& pos);
    void onDoubleClicked(const QModelIndex& index);

    /**
     * @brief 加载并显示目录内容
     */
    void loadDirectory(const QString& path, bool recursive = false);

    /**
     * @brief 全局/本地搜索
     */
    void search(const QString& query);

    /**
     * @brief 应用当前筛选器
     */
    void applyFilters(const FilterState& state);
    void applyFilters(); // 使用保存的状态重新应用

    /**
     * @brief 创建新条目（文件夹/Markdown/Txt）
     */
    void createNewItem(const QString& type);

    /**
     * @brief 预览文件内容 (支持文本、Markdown、图片等)
     */
    void previewFile(const QString& path);

    /**
     * @brief 加载指定路径列表 (分类联动使用)
     */
    void loadPaths(const QStringList& paths);

protected:
    void wheelEvent(QWheelEvent* event) override;
};

/**
 * @brief 自定义 Delegate：处理网格视图下的图标、星级、颜色圆点及角标叠加
 */
class GridItemDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    bool eventFilter(QObject* obj, QEvent* event) override;
    bool editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option, const QModelIndex& index) override;

    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    void setEditorData(QWidget* editor, const QModelIndex& index) const override;
    void setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const override;
    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
};

} // namespace ArcMeta
