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
    add("qw_search", "搜索灵感", "Ctrl+F", "懒人笔记窗口");
    add("qw_delete_soft", "移至回收站", "Delete", "懒人笔记窗口");
    add("qw_delete_hard", "彻底删除", "Shift+Delete", "懒人笔记窗口");
    add("qw_favorite", "切换收藏状态", "Ctrl+E", "懒人笔记窗口");
    add("qw_preview", "快速预览内容", "Space", "懒人笔记窗口");
    add("qw_pin", "置顶/取消置顶项目", "Alt+D", "懒人笔记窗口");
    add("qw_close", "关闭窗口", "Ctrl+W", "懒人笔记窗口");
    add("qw_move_up", "项目上移", "Alt+Up", "懒人笔记窗口");
    add("qw_move_down", "项目下移", "Alt+Down", "懒人笔记窗口");
    add("qw_screenshot", "截图", "Alt+S", "懒人笔记窗口"); // [USER_REQUEST] 找回缺失快捷键
    add("qw_pure_paste", "纯净粘贴", "Ctrl+Shift+V", "懒人笔记窗口"); // [USER_REQUEST] 找回缺失快捷键
    add("qw_new_idea", "新建灵感", "Ctrl+N", "懒人笔记窗口");
    add("qw_select_all", "全选列表", "Ctrl+A", "懒人笔记窗口");
    add("qw_extract", "提取内容到剪贴板", "Ctrl+C", "懒人笔记窗口");
    add("qw_lock_cat", "立即锁定当前分类", "Ctrl+S", "懒人笔记窗口");
    add("qw_lock_all_cats", "闪速锁定所有分类", "Ctrl+Shift+S", "懒人笔记窗口");
    add("qw_toggle_locked_visibility", "显示/隐藏加锁分类", "Ctrl+Alt+S", "懒人笔记窗口");
    add("qw_stay_on_top", "切换窗口置顶", "Alt+Q", "懒人笔记窗口");
    add("qw_edit", "编辑选中项", "Ctrl+B", "懒人笔记窗口");
    add("qw_lock_app", "锁定应用", "Ctrl+Shift+Alt+S", "懒人笔记窗口");
    add("qw_sidebar", "显示/隐藏侧边栏", "Alt+W", "懒人笔记窗口");
    add("qw_filter", "开启高级筛选", "Ctrl+E", "懒人笔记窗口");
    add("qw_filter_toggle_groups", "折叠/展开筛选器组", "Ctrl+G", "懒人笔记窗口");
    // 用户要求：将列表翻页快捷键由 Alt+S/X 修改为 PgUp/PgDn
    add("qw_prev_page", "上一页", "PgUp", "懒人笔记窗口");
    add("qw_next_page", "下一页", "PgDown", "懒人笔记窗口");
    // 用户要求：为刷新按钮添加 F5 快捷键定义
    add("qw_refresh", "刷新列表", "F5", "懒人笔记窗口");
    add("qw_show_all", "显示全部数据", "Ctrl+Shift+A", "懒人笔记窗口");
    add("qw_copy_tags", "复制标签", "Ctrl+Alt+C", "懒人笔记窗口");
    add("qw_paste_tags", "粘贴标签", "Ctrl+Alt+V", "懒人笔记窗口");
    for (int i = 0; i <= 5; ++i) {
        add(QString("qw_rating_%1").arg(i), QString("标记星级 %1").arg(i), QString("Ctrl+%1").arg(i), "懒人笔记窗口");
    }
    // [USER_REQUEST] 2026-03-14 F4重复上一次操作
    add("qw_repeat_action", "重复上一次操作", "F4", "懒人笔记窗口");



    // NoteEditWindow
    add("ed_save", "保存修改", "Ctrl+S", "编辑器");
    add("ed_close", "关闭编辑器", "Ctrl+W", "编辑器");
    add("ed_search", "内容内查找", "Ctrl+F", "编辑器");

    // QuickPreview
    add("pv_prev", "上一个项目", "Alt+Up", "预览窗");
    add("pv_next", "下一个项目", "Alt+Down", "预览窗");
    add("pv_back", "历史后退", "Alt+Left", "预览窗");
    add("pv_forward", "历史前进", "Alt+Right", "预览窗");
    add("pv_copy", "复制", "Ctrl+C", "预览窗");
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
        QKeySequence seq(settings.value(key).toString());
        
        // [FORCE_UPDATE] 强制升级过时的快捷键配置，防止本地缓存导致 UI 显示错误
        // 用户要求：qw_toggle_main 由 Alt+W 升级为 Alt+E
        if (key == "qw_toggle_main" && seq == QKeySequence("Alt+W")) {
            seq = QKeySequence("Alt+E");
        }
        // [MODIFIED] 2026-03-xx 按照用户要求：仅保留 Alt+W 控制侧边栏，清除旧有的 Alt+Q/Ctrl+Q 残留
        if (key == "qw_sidebar" && (seq == QKeySequence("Alt+Q") || seq == QKeySequence("Ctrl+Q"))) {
            seq = QKeySequence("Alt+W");
        }
        // 用户要求：翻页快捷键由 Alt+S/X 升级为 PgUp/PgDn
        if (key == "qw_prev_page" && seq == QKeySequence("Alt+S")) {
            seq = QKeySequence("PgUp");
        }
        if (key == "qw_next_page" && seq == QKeySequence("Alt+X")) {
            seq = QKeySequence("PgDown");
        }
        // 用户要求：解决与原生纯本文粘贴的冲突
        if ((key == "qw_paste_tags" || key == "mw_paste_tags") && seq == QKeySequence("Ctrl+Shift+V")) {
            seq = QKeySequence("Ctrl+Alt+V");
        }
        if ((key == "qw_copy_tags" || key == "mw_copy_tags") && seq == QKeySequence("Ctrl+Shift+C")) {
            seq = QKeySequence("Ctrl+Alt+C");
        }
        // 用户要求：由 Ctrl+Shift+Alt+S 升级为 Ctrl+Alt+S
        if ((key == "qw_toggle_locked_visibility" || key == "mw_toggle_locked_visibility") && seq == QKeySequence("Ctrl+Shift+Alt+S")) {
            seq = QKeySequence("Ctrl+Alt+S");
        }
        
        if (key == "qw_toggle_locked_visibility" || key == "mw_toggle_locked_visibility") {
            qDebug() << "[TRACE-SC] 加载快捷键:" << key << " -> " << seq.toString();
        }

        // 用户要求：锁定应用快捷键由 Ctrl+Shift+L 升级为 Ctrl+Shift+Alt+S
        if (key == "qw_lock_app" && seq == QKeySequence("Ctrl+Shift+L")) {
            seq = QKeySequence("Ctrl+Shift+Alt+S");
        }

        // [USER_REQUEST] 强制统一快捷键标准：窗口置顶 Alt+Q，数据/分类置顶 Alt+D
        // 无论当前值是什么，均强制覆盖为新标准
        if (key == "qw_stay_on_top" || key == "mw_stay_on_top") {
            seq = QKeySequence("Alt+Q");
        }
        if (key == "qw_pin" || key == "mw_pin") {
            seq = QKeySequence("Alt+D");
        }

        // [FORCE_UPDATE] 2026-04-xx 按照用户要求：高级筛选 Ctrl+G 升级为 Ctrl+E，Ctrl+G 用于折叠
        if (key == "qw_filter" && seq == QKeySequence("Ctrl+G")) {
            seq = QKeySequence("Ctrl+E");
        }
        
        m_customKeys[key] = seq;
    }
    settings.endGroup();
}

void ShortcutManager::resetToDefaults() {
    m_customKeys.clear();
    QSettings settings("RapidNotes", "InternalHotkeys");
    settings.remove("Custom");
    emit shortcutsChanged();
}
