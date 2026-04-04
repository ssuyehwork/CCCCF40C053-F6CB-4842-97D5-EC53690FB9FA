#pragma once

#include <QFrame>
#include <QTreeView>
#include <QVBoxLayout>

namespace ArcMeta {

class CategoryModel;
class DropTreeView;

/**
 * @brief 分类面板（面板一）
 * 还原旧版双树架构
 */
class CategoryPanel : public QFrame {
    Q_OBJECT

public:
    explicit CategoryPanel(QWidget* parent = nullptr);
    ~CategoryPanel() override = default;

    /**
     * @brief 物理还原：设置 1px 翠绿高亮线的显隐状态
     */
    void setFocusHighlight(bool visible);

    /**
     * @brief 暴露模型以便外部（如 MainWindow）触发刷新
     */
    CategoryModel* model() const { return m_categoryModel; }

signals:
    void categorySelected(int id, const QString& name);
    void fileSelected(const QString& path);

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private slots:
    void onCreateCategory();
    void onCreateSubCategory();
    void onRenameCategory();
    void onDeleteCategory();
    void onClassifyToCategory();
    void onSetColor();
    void onRandomColor();
    void onSetPresetTags();
    void onTogglePin();
    void onSetPassword();
    void onClearPassword();
    void onSortByNameAsc();
    void onSortByNameDesc();
    void onSortAllByNameAsc();
    void onSortAllByNameDesc();

private:
    void initUi();
    void setupContextMenu();
    
    /**
     * @brief 2026-03-xx 物理寻址辅助：递归向上查找有效的分类 ID
     */
    int getTargetCategoryId(const QModelIndex& index);

    /**
     * @brief 2026-03-xx 持久化记忆：保存与加载展开状态到 QSettings
     */
    void saveExpandedStateToSettings();
    void loadExpandedStateFromSettings();

    /**
     * @brief 2026-03-xx 安全逻辑：尝试解锁分类
     * @return 是否解锁成功
     */
    bool tryUnlockCategory(const QModelIndex& index);

    QVBoxLayout* m_mainLayout = nullptr;
    QWidget* m_focusLine = nullptr;
    
    DropTreeView* m_categoryTree = nullptr;
    CategoryModel* m_categoryModel = nullptr;

    // 2026-03-xx 会话级解锁列表：存储当前已验证通过的加密分类 ID
    QSet<int> m_unlockedIds;
};

} // namespace ArcMeta
