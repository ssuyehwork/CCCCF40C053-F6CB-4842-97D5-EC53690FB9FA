#pragma once

#include "MftReader.h"
#include <string>
#include <unordered_set>

namespace ArcMeta {

/**
 * @brief 路径重建工具
 * 负责从 FRN 二元组递归向上重构物理路径
 */
class PathBuilder {
public:
    /**
     * @brief 获取 FRN 对应的完整物理路径
     * @param volume 卷标 (如 L"C:")
     * @param frn 目标 FRN
     * @return 完整路径字符串，失败返回空
     */
    static std::wstring getPath(const std::wstring& volume, DWORDLONG frn);

private:
    /**
     * @brief 递归核心逻辑
     * @param volume 卷标
     * @param frn 当前 FRN
     * @param visited 环路检测集合
     * @param depth 递归深度
     */
    static std::wstring resolveRecursive(const std::wstring& volume, DWORDLONG frn, 
                                          std::unordered_set<DWORDLONG>& visited, int depth);
};

} // namespace ArcMeta
