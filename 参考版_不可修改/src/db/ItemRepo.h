#pragma once

#include "Database.h"
#include "../meta/AmMetaJson.h"
#include <string>
#include <vector>

namespace ArcMeta {

/**
 * @brief 文件条目持久层
 */
class ItemRepo {
public:
    static bool save(const std::wstring& parentPath, const std::wstring& name, const ItemMeta& meta);
    static bool removeByFrn(const std::wstring& volume, const std::wstring& frn);
    static bool markAsDeleted(const std::wstring& volume, const std::wstring& frn);
    
    /**
     * @brief 通过 FRN 获取当前数据库记录的路径
     */
    static std::wstring getPathByFrn(const std::wstring& volume, const std::wstring& frn);

    /**
     * @brief 物理更新路径
     */
    static bool updatePath(const std::wstring& volume, const std::wstring& frn, const std::wstring& newPath, const std::wstring& newParentPath);
};

} // namespace ArcMeta
