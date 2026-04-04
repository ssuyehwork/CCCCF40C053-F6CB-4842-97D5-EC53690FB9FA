#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <windows.h>

namespace ArcMeta {

/**
 * @brief 文件条目基础结构
 */
struct FileEntry {
    std::wstring volume;
    DWORDLONG frn = 0;
    DWORDLONG parentFrn = 0;
    std::wstring name;
    DWORD attributes = 0;

    bool isDir() const { return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0; }
};

/**
 * @brief 文件索引管理器
 * 实现 MFT 枚举及无权限时的文件系统降级扫描
 */
class MftReader {
public:
    static MftReader& instance();

    /**
     * @brief 构建全盘索引
     * 自动尝试 MFT 模式，失败或无权限时降级为 std::filesystem 模式
     */
    void buildIndex();

    /**
     * @brief 获取指定目录下的子项
     */
    std::vector<FileEntry> getChildren(const std::wstring& folderPath);

    /**
     * @brief 并行全局搜索文件名
     * @param query 搜索关键词
     * @param volume 限制在特定卷搜索，为空则全盘搜索
     */
    std::vector<FileEntry> search(const std::wstring& query, const std::wstring& volume = L"");

private:
    MftReader() = default;
    
    // 2026-03-xx 按照重构方案：改为私有内部方法，支持局部变量填充以实现无锁扫描
    bool loadMftForVolumeInternal(const std::wstring& volumeName,
        std::unordered_map<std::wstring, std::unordered_map<DWORDLONG, FileEntry>>& outIndex,
        std::unordered_map<std::wstring, std::unordered_map<DWORDLONG, std::vector<DWORDLONG>>>& outParentToChildren);
    
    void scanDirectoryFallbackInternal(const std::wstring& volumeName, 
        std::unordered_map<std::wstring, FileEntry>& outPathIndex);

    // volume -> (frn -> Entry)
    std::unordered_map<std::wstring, std::unordered_map<DWORDLONG, FileEntry>> m_index;
    
    // 关键优化：父子关系反向索引 volume -> (parentFrn -> vector of childFrns)
    std::unordered_map<std::wstring, std::unordered_map<DWORDLONG, std::vector<DWORDLONG>>> m_parentToChildren;

    // 性能优化：volume -> (fullPath -> frn)
    std::unordered_map<std::wstring, std::unordered_map<std::wstring, DWORDLONG>> m_pathToFrn;

    /**
     * @brief 根据路径获取对应的 FRN（用于 MFT 模式下的钻取）
     */
    DWORDLONG getFrnFromPath(const std::wstring& folderPath);

    // 降级模式下的路径索引：fullPath -> Entry
    std::unordered_map<std::wstring, FileEntry> m_pathIndex;
    
    bool m_isUsingMft = false;

public:
    const std::unordered_map<std::wstring, std::unordered_map<DWORDLONG, FileEntry>>& getIndex() const { return m_index; }
    
    /**
     * @brief USN 监听器更新内存索引，并同步维护反向索引
     */
    void updateEntry(const FileEntry& entry);

    /**
     * @brief USN 监听器移除记录
     */
    void removeEntry(const std::wstring& volume, DWORDLONG frn);

private:
    mutable std::recursive_mutex m_mutex; // 保护所有索引数据
};

} // namespace ArcMeta
