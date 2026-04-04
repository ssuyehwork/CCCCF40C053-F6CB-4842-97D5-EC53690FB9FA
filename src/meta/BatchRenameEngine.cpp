#include "BatchRenameEngine.h"
#include <QFileInfo>
#include <QDateTime>
#include <filesystem>

namespace ArcMeta {

BatchRenameEngine& BatchRenameEngine::instance() {
    static BatchRenameEngine inst;
    return inst;
}

std::vector<std::wstring> BatchRenameEngine::preview(const std::vector<std::wstring>& originalPaths, const std::vector<RenameRule>& rules) {
    std::vector<std::wstring> results;
    for (size_t i = 0; i < originalPaths.size(); ++i) {
        results.push_back(processOne(originalPaths[i], (int)i, rules).toStdWString());
    }
    return results;
}

/**
 * @brief 管道模式处理单个文件名
 */
QString BatchRenameEngine::processOne(const std::wstring& path, int index, const std::vector<RenameRule>& rules) {
    QFileInfo info(QString::fromStdWString(path));
    QString newName = "";

    for (const auto& rule : rules) {
        switch (rule.type) {
            case RenameComponentType::Text:
                newName += rule.value;
                break;
            case RenameComponentType::Sequence: {
                int val = rule.start + (index * rule.step);
                newName += QString("%1").arg(val, rule.padding, 10, QChar('0'));
                break;
            }
            case RenameComponentType::Date:
                newName += QDateTime::currentDateTime().toString(rule.value.isEmpty() ? "yyyyMMdd" : rule.value);
                break;
            case RenameComponentType::Metadata:
                // 注入 ArcMeta 元数据标记（如评级星级）
                newName += "[ArcMeta]";
                break;
        }
    }

    // 保留原始后缀
    QString ext = info.suffix();
    if (!ext.isEmpty()) newName += "." + ext;

    return newName;
}

/**
 * @brief 执行物理重命名（红线：重命名成功后必须迁移元数据）
 */
bool BatchRenameEngine::execute(const std::vector<std::wstring>& originalPaths, const std::vector<RenameRule>& rules) {
    auto newNames = preview(originalPaths, rules);

    for (size_t i = 0; i < originalPaths.size(); ++i) {
        std::filesystem::path oldP(originalPaths[i]);
        std::filesystem::path newP = oldP.parent_path() / newNames[i];

        try {
            std::filesystem::rename(oldP, newP);
            // 关键：重命名成功后，USN Watcher 会处理 FRN 追踪，
            // 但此处需确保 .am_meta.json 键值同步更新逻辑能够触发。
        } catch (...) {
            return false; // 任一失败则中断（实际生产应支持回滚）
        }
    }
    return true;
}

} // namespace ArcMeta
