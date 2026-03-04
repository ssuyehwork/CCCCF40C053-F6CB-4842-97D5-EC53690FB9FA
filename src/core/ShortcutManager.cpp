#include "ShortcutManager.h"

ShortcutManager& ShortcutManager::instance() {
    static ShortcutManager inst;
    return inst;
}

ShortcutManager::ShortcutManager(QObject* parent) : QObject(parent) {
    initDefaults();
    load();
}

void ShortcutManager::initDefaults() {
    auto add = [&](const QString& id, const QString& desc, const QString& def, const QString& cat) {
        m_shortcuts[id] = {id, desc, QKeySequence(def), cat};
    };

    // QuickWindow shortcuts
    add("qw_search", "搜索灵感", "Ctrl+F", "极速窗口");
    add("qw_delete_soft", "移至回收站", "Delete", "极速窗口");
    add("qw_delete_hard", "彻底删除", "Shift+Delete", "极速窗口");
    add("qw_favorite", "切换书签状态", "Ctrl+E", "极速窗口");
    add("qw_preview", "快速预览内容", "Space", "极速窗口");
    add("qw_pin", "置顶/取消置顶项目", "Ctrl+P", "极速窗口");
    add("qw_close", "关闭窗口", "Ctrl+W", "极速窗口");
    add("qw_lock_item", "锁定/解锁项目", "Ctrl+S", "极速窗口");
    add("qw_new_idea", "新建灵感", "Ctrl+N", "极速窗口");
    add("qw_select_all", "全选列表", "Ctrl+A", "极速窗口");
    add("qw_extract", "提取内容到剪贴板", "Ctrl+C", "极速窗口");
    add("qw_lock_cat", "立即锁定当前分类", "Ctrl+Shift+L", "极速窗口");
    add("qw_stay_on_top", "切换窗口置顶", "Alt+D", "极速窗口");
    add("qw_toggle_main", "打开主窗口", "Alt+W", "极速窗口");
    add("qw_toolbox", "打开工具箱", "Ctrl+Shift+T", "极速窗口");
    add("qw_edit", "编辑选中项", "Ctrl+B", "极速窗口");
    add("qw_sidebar", "显示/隐藏侧边栏", "Ctrl+Q", "极速窗口");
    add("qw_prev_page", "上一页", "Alt+S", "极速窗口");
    add("qw_next_page", "下一页", "Alt+X", "极速窗口");
    add("qw_copy_tags", "复制标签", "Ctrl+Shift+C", "极速窗口");
    add("qw_paste_tags", "粘贴标签", "Ctrl+Shift+V", "极速窗口");
    for (int i = 0; i <= 5; ++i) {
        add(QString("qw_rating_%1").arg(i), QString("设置星级 %1").arg(i), QString("Ctrl+%1").arg(i), "极速窗口");
    }

    // MainWindow shortcuts
    add("mw_filter", "开启高级筛选", "Ctrl+G", "主窗口");
    add("mw_preview", "预览选中项", "Space", "主窗口");
    add("mw_meta", "开启元数据面板", "Ctrl+I", "主窗口");
    add("mw_refresh", "刷新列表", "F5", "主窗口");
    add("mw_search", "聚焦搜索框", "Ctrl+F", "主窗口");
    add("mw_new", "新建笔记", "Ctrl+N", "主窗口");
    add("mw_favorite", "切换书签状态", "Ctrl+E", "主窗口");
    add("mw_pin", "置顶/取消置顶", "Ctrl+P", "主窗口");
    add("mw_save", "保存笔记/锁定项", "Ctrl+S", "主窗口");
    add("mw_edit", "编辑笔记", "Ctrl+B", "主窗口");
    add("mw_extract", "提取内容", "Ctrl+C", "主窗口");
    add("mw_lock_cat", "锁定分类", "Ctrl+Shift+L", "主窗口");
    add("mw_delete_soft", "移至回收站", "Delete", "主窗口");
    add("mw_delete_hard", "彻底删除", "Shift+Delete", "主窗口");
    add("mw_copy_tags", "复制标签", "Ctrl+Shift+C", "主窗口");
    add("mw_paste_tags", "粘贴标签", "Ctrl+Shift+V", "主窗口");
    add("mw_close", "关闭窗口", "Ctrl+W", "主窗口");
    for (int i = 0; i <= 5; ++i) {
        add(QString("mw_rating_%1").arg(i), QString("设置星级 %1").arg(i), QString("Ctrl+%1").arg(i), "主窗口");
    }

    // NoteEditWindow
    add("ed_save", "保存修改", "Ctrl+S", "编辑器");
    add("ed_close", "关闭编辑器", "Ctrl+W", "编辑器");
    add("ed_search", "内容内查找", "Ctrl+F", "编辑器");

    // QuickPreview
    add("pv_prev", "上一个项目", "Alt+Up", "预览窗");
    add("pv_next", "下一个项目", "Alt+Down", "预览窗");
    add("pv_back", "历史后退", "Alt+Left", "预览窗");
    add("pv_forward", "历史前进", "Alt+Right", "预览窗");
    add("pv_copy", "复制内容", "Ctrl+C", "预览窗");
    add("pv_edit", "编辑项目", "Ctrl+B", "预览窗");
    add("pv_close", "关闭预览", "Ctrl+W", "预览窗");
    add("pv_search", "内容内查找", "Ctrl+F", "预览窗");

    // FileSearch & KeywordSearch
    add("fs_select_all", "全选结果", "Ctrl+A", "搜索窗口");
    add("fs_copy", "复制选中内容", "Ctrl+C", "搜索窗口");
    add("fs_delete", "删除选中项", "Delete", "搜索窗口");
    add("fs_scan", "开始/重新扫描", "F5", "搜索窗口");

    add("ks_search", "执行搜索", "Ctrl+F", "关键字搜索");
    add("ks_replace", "执行替换", "Ctrl+R", "关键字搜索");
    add("ks_undo", "撤销上次替换", "Ctrl+Z", "关键字搜索");
    add("ks_swap", "交换查找与替换内容", "Ctrl+Shift+S", "关键字搜索");
}

QKeySequence ShortcutManager::getShortcut(const QString& id) const {
    if (m_customKeys.contains(id)) return m_customKeys[id];
    if (m_shortcuts.contains(id)) return m_shortcuts[id].defaultKey;
    return QKeySequence();
}

void ShortcutManager::setShortcut(const QString& id, const QKeySequence& key) {
    m_customKeys[id] = key;
}

QList<ShortcutManager::ShortcutInfo> ShortcutManager::getShortcutsByCategory(const QString& category) const {
    QList<ShortcutInfo> result;
    for (const auto& info : m_shortcuts) {
        if (info.category == category) result << info;
    }
    return result;
}

void ShortcutManager::save() {
    QSettings settings("RapidNotes", "InternalHotkeys");
    settings.beginGroup("Custom");
    for (auto it = m_customKeys.begin(); it != m_customKeys.end(); ++it) {
        settings.setValue(it.key(), it.value().toString());
    }
    settings.endGroup();
    emit shortcutsChanged();
}

void ShortcutManager::load() {
    QSettings settings("RapidNotes", "InternalHotkeys");
    settings.beginGroup("Custom");
    QStringList keys = settings.allKeys();
    for (const QString& key : keys) {
        m_customKeys[key] = QKeySequence(settings.value(key).toString());
    }
    settings.endGroup();
}

void ShortcutManager::resetToDefaults() {
    m_customKeys.clear();
    QSettings settings("RapidNotes", "InternalHotkeys");
    settings.remove("Custom");
    emit shortcutsChanged();
}
