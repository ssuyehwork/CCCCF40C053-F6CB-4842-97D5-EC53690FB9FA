#include "MftReader.h"
#include "PathBuilder.h"
#include <winioctl.h>
#include <filesystem>
#include <iostream>
#include <algorithm>
#include <execution>
#include <mutex>

namespace ArcMeta {

MftReader& MftReader::instance() {
    static MftReader inst;
    return inst;
}

/**
 * @brief 构建全盘索引 (2026-03-xx 重构：双缓冲无锁扫描，解决 UI 假死)
 * 扫描过程不持有全局锁，仅在最终交换数据时进行极短时间锁定。
 */
void MftReader::buildIndex() {
    // 1. 创建局部临时容器 (局部 Buffer)
    std::unordered_map<std::wstring, std::unordered_map<DWORDLONG, FileEntry>> localIndex;
    std::unordered_map<std::wstring, std::unordered_map<DWORDLONG, std::vector<DWORDLONG>>> localParentToChildren;
    std::unordered_map<std::wstring, FileEntry> localPathIndex;
    bool usingMft = false;

    DWORD drives = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if (drives & (1 << i)) {
            wchar_t driveLetter = (wchar_t)(L'A' + i);
            std::wstring volumeName = std::wstring(1, driveLetter) + L":";
            
            wchar_t driveRoot[] = { driveLetter, L':', L'\\', L'\0' };
            if (GetDriveTypeW(driveRoot) == DRIVE_FIXED) {
                // 尝试 MFT 读取，传入局部容器引用
                if (loadMftForVolumeInternal(volumeName, localIndex, localParentToChildren)) {
                    usingMft = true;
                } else {
                    // 如果 MFT 失败，执行降级扫描，存入局部路径索引
                    scanDirectoryFallbackInternal(volumeName, localPathIndex);
                }
            }
        }
    }

    // 2. 极速原子化数据交换 (仅锁定几微秒)
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        m_index = std::move(localIndex);
        m_parentToChildren = std::move(localParentToChildren);
        m_pathIndex = std::move(localPathIndex);
        m_isUsingMft = usingMft;
        // 清理旧的路径缓存，因为数据已更新
        m_pathToFrn.clear();
    }
}

/**
 * @brief 内部方法：读取 MFT 并填充指定容器
 */
bool MftReader::loadMftForVolumeInternal(const std::wstring& volumeName,
    std::unordered_map<std::wstring, std::unordered_map<DWORDLONG, FileEntry>>& outIndex,
    std::unordered_map<std::wstring, std::unordered_map<DWORDLONG, std::vector<DWORDLONG>>>& outParentToChildren) {
    
    std::wstring path = L"\\\\.\\" + volumeName;
    HANDLE hVol = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    
    if (hVol == INVALID_HANDLE_VALUE) return false;

    USN_JOURNAL_DATA journalData;
    DWORD cb;
    if (!DeviceIoControl(hVol, FSCTL_QUERY_USN_JOURNAL, NULL, 0, &journalData, sizeof(journalData), &cb, NULL)) {
        CloseHandle(hVol);
        return false;
    }

    MFT_ENUM_DATA enumData;
    enumData.StartFileReferenceNumber = 0;
    enumData.LowUsn = 0;
    enumData.HighUsn = journalData.NextUsn;

    const int BUF_SIZE = 64 * 1024;
    std::vector<BYTE> buffer(BUF_SIZE);
    
    auto& volumeIndex = outIndex[volumeName];
    volumeIndex.reserve(1000000); 
    auto& childrenMap = outParentToChildren[volumeName];

    while (DeviceIoControl(hVol, FSCTL_ENUM_USN_DATA, &enumData, sizeof(enumData), buffer.data(), BUF_SIZE, &cb, NULL)) {
        BYTE* pData = buffer.data() + sizeof(USN);
        while (pData < buffer.data() + cb) {
            USN_RECORD_V2* pRecord = (USN_RECORD_V2*)pData;
            
            FileEntry entry;
            entry.volume = volumeName;
            entry.frn = pRecord->FileReferenceNumber;
            entry.parentFrn = pRecord->ParentFileReferenceNumber;
            entry.attributes = pRecord->FileAttributes;
            entry.name = std::wstring(pRecord->FileName, pRecord->FileNameLength / sizeof(wchar_t));
            
            volumeIndex[entry.frn] = entry;
            childrenMap[entry.parentFrn].push_back(entry.frn);

            pData += pRecord->RecordLength;
        }
        enumData.StartFileReferenceNumber = ((USN_RECORD_V2*)buffer.data())->FileReferenceNumber;
    }

    CloseHandle(hVol);
    return true;
}

/**
 * @brief 内部方法：降级扫描并填充路径索引
 */
void MftReader::scanDirectoryFallbackInternal(const std::wstring& volumeName, 
    std::unordered_map<std::wstring, FileEntry>& outPathIndex) {
    try {
        std::wstring rootPath = volumeName + L"\\";
        for (const auto& entry : std::filesystem::directory_iterator(rootPath, std::filesystem::directory_options::skip_permission_denied)) {
            FileEntry fe;
            fe.volume = volumeName;
            fe.name = entry.path().filename().wstring();
            fe.attributes = entry.is_directory() ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
            outPathIndex[entry.path().wstring()] = fe;
        }
    } catch (...) {}
}

/**
 * @brief 获取指定目录下的子项列表 (2026-03-xx 优化：实时降级逻辑，防止索引未就绪时白屏)
 */
std::vector<FileEntry> MftReader::getChildren(const std::wstring& folderPath) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::vector<FileEntry> results;
    std::wstring vol = folderPath.length() >= 2 ? folderPath.substr(0, 2) : L"";
    if (vol.empty()) return results;

    // 1. 如果 MFT 索引已就绪，使用极速内存查询
    if (m_isUsingMft) {
        DWORDLONG parentFrn = getFrnFromPath(folderPath);
        if (parentFrn != 0) {
            auto itVol = m_parentToChildren.find(vol);
            if (itVol != m_parentToChildren.end()) {
                auto itParent = itVol->second.find(parentFrn);
                if (itParent != itVol->second.end()) {
                    auto& entries = m_index[vol];
                    for (DWORDLONG childFrn : itParent->second) {
                        auto itEntry = entries.find(childFrn);
                        if (itEntry != entries.end()) results.push_back(itEntry->second);
                    }
                    return results;
                }
            }
        }
    }

    // 2. 降级/兜底逻辑：如果索引未就绪或未找到，直接通过操作系统实时读取
    // 这保证了即便后台正在构建全量索引，用户点击目录依然能“秒开”看到内容
    try {
        std::filesystem::path p(folderPath);
        if (std::filesystem::exists(p) && std::filesystem::is_directory(p)) {
            for (const auto& entry : std::filesystem::directory_iterator(p, std::filesystem::directory_options::skip_permission_denied)) {
                FileEntry fe;
                fe.volume = vol;
                fe.name = entry.path().filename().wstring();
                fe.attributes = entry.is_directory() ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
                results.push_back(fe);
            }
        }
    } catch (...) {}

    return results;
}

/**
 * @brief 根据路径获取对应的 FRN
 */
DWORDLONG MftReader::getFrnFromPath(const std::wstring& folderPath) {
    // getChildren 已经加锁了，但 getFrnFromPath 是公有的或被其他公有方法调用，需自保
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::wstring vol = folderPath.length() >= 2 ? folderPath.substr(0, 2) : L"";
    if (vol.empty()) return 0;
    
    // 处理根目录特例 (一般根目录 FRN 为 5，但通过路径匹配更稳健)
    if (folderPath.length() <= 3) { // e.g., "C:\" 或 "C:"
        return 5; // NTFS 规范根目录 FRN 恒为 5
    }

    auto& volPathMap = m_pathToFrn[vol];
    auto it = volPathMap.find(folderPath);
    if (it != volPathMap.end()) {
        return it->second;
    }
    
    // 如果缓存没有，全量反向构建一次（仅对目录）
    auto& entries = m_index[vol];
    for (const auto& [frn, entry] : entries) {
        if (entry.isDir()) {
            std::wstring fullPath = PathBuilder::getPath(vol, frn);
            m_pathToFrn[vol][fullPath] = frn;
            if (fullPath == folderPath) return frn;
        }
    }
    return 0; 
}

/**
 * @brief 实现并行文件名搜索 (std::execution::par)
 */
std::vector<FileEntry> MftReader::search(const std::wstring& query, const std::wstring& volume) {
    if (query.empty()) return {};

    std::vector<const FileEntry*> pool;
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (!volume.empty()) {
            auto it = m_index.find(volume);
            if (it != m_index.end()) {
                for (auto& pair : it->second) pool.push_back(&pair.second);
            }
        } else {
            for (auto& volPair : m_index) {
                for (auto& pair : volPair.second) pool.push_back(&pair.second);
            }
        }
    }

    // 如果索引为空（构建中），搜索直接返回空，后续可以扩展实时流式搜索
    if (pool.empty()) return {};

    std::wstring lQuery = query;
    std::transform(lQuery.begin(), lQuery.end(), lQuery.begin(), ::towlower);

    std::vector<FileEntry> results;
    std::mutex resultsMutex;

    std::for_each(std::execution::par, pool.begin(), pool.end(), [&](const FileEntry* entry) {
        std::wstring lName = entry->name;
        std::transform(lName.begin(), lName.end(), lName.begin(), ::towlower);
        
        if (lName.find(lQuery) != std::wstring::npos) {
            std::lock_guard<std::mutex> lock(resultsMutex);
            results.push_back(*entry);
        }
    });

    return results;
}

/**
 * @brief USN 监听器更新内存索引
 */
void MftReader::updateEntry(const FileEntry& entry) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    
    auto& volIndex = m_index[entry.volume];
    bool isNew = (volIndex.find(entry.frn) == volIndex.end());
    DWORDLONG oldParentFrn = isNew ? 0 : volIndex[entry.frn].parentFrn;
    
    volIndex[entry.frn] = entry;

    if (!isNew && oldParentFrn != entry.parentFrn) {
        auto& oldChildren = m_parentToChildren[entry.volume][oldParentFrn];
        oldChildren.erase(std::remove(oldChildren.begin(), oldChildren.end(), entry.frn), oldChildren.end());
    }
    
    if (isNew || oldParentFrn != entry.parentFrn) {
        m_parentToChildren[entry.volume][entry.parentFrn].push_back(entry.frn);
    }

    m_pathToFrn[entry.volume].clear();
}

/**
 * @brief USN 监听器移除记录
 */
void MftReader::removeEntry(const std::wstring& volume, DWORDLONG frn) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto& volIndex = m_index[volume];
    auto it = volIndex.find(frn);
    if (it != volIndex.end()) {
        DWORDLONG parentFrn = it->second.parentFrn;
        volIndex.erase(it);
        auto& children = m_parentToChildren[volume][parentFrn];
        children.erase(std::remove(children.begin(), children.end(), frn), children.end());
        m_pathToFrn[volume].clear();
    }
}

} // namespace ArcMeta
