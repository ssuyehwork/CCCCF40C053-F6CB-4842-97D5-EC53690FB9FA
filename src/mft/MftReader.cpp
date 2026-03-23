#include "MftReader.h"
#include <iostream>
#include <vector>
#include <execution>
#include <algorithm>
#include <cwctype>

namespace mft {

MftReader& MftReader::instance() {
    static MftReader inst;
    return inst;
}

bool MftReader::loadVolumeIndex(const std::wstring& volumePath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    // 预分配以提高性能
    if (m_index.empty()) m_index.reserve(1000000);

    // 打开卷句柄 (需要管理员权限)
    HANDLE hVolume = CreateFileW(volumePath.c_str(),
                                GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                NULL,
                                OPEN_EXISTING,
                                0,
                                NULL);

    if (hVolume == INVALID_HANDLE_VALUE) {
        return false;
    }

    bool success = scanMft(hVolume);
    CloseHandle(hVolume);
    return success;
}

bool MftReader::scanMft(HANDLE hVolume) {
    USN_JOURNAL_DATA journalData;
    DWORD bytesReturned;

    // 获取 USN 日志元数据
    if (!DeviceIoControl(hVolume,
                         FSCTL_QUERY_USN_JOURNAL,
                         NULL,
                         0,
                         &journalData,
                         sizeof(journalData),
                         &bytesReturned,
                         NULL)) {
        return false;
    }

    // 设置 MFT 枚举范围 (从 0 开始)
    MFT_ENUM_DATA mftEnumData;
    mftEnumData.StartFileReferenceNumber = 0;
    mftEnumData.LowUsn = 0;
    mftEnumData.HighUsn = journalData.NextUsn;

    // 枚举缓冲区大小: 64KB (文档推荐大小)
    const DWORD BUFFER_SIZE = 64 * 1024;
    std::vector<BYTE> buffer(BUFFER_SIZE);

    while (DeviceIoControl(hVolume,
                           FSCTL_ENUM_USN_DATA,
                           &mftEnumData,
                           sizeof(mftEnumData),
                           buffer.data(),
                           BUFFER_SIZE,
                           &bytesReturned,
                           NULL)) {

        if (bytesReturned < sizeof(DWORDLONG)) break;

        // 枚举缓冲区中的记录
        // 缓冲区开头是一个 DWORDLONG (下一条 FRN)
        BYTE* current = buffer.data() + sizeof(DWORDLONG);
        BYTE* end = buffer.data() + bytesReturned;

        while (current < end) {
            USN_RECORD_V2* record = reinterpret_cast<USN_RECORD_V2*>(current);

            // 提取字段并存入索引
            FileEntry entry;
            entry.frn = record->FileReferenceNumber;
            entry.parentFrn = record->ParentFileReferenceNumber;
            entry.attributes = record->FileAttributes;
            entry.name = std::wstring(record->FileName, record->FileNameLength / sizeof(WCHAR));

            m_index[entry.frn] = std::move(entry);

            // 指向下一条记录 (按 8 字节对齐)
            current += record->RecordLength;
        }

        // 更新起始 FRN 为下一批枚举的起点
        mftEnumData.StartFileReferenceNumber = *reinterpret_cast<DWORDLONG*>(buffer.data());
    }

    return true;
}

std::vector<const FileEntry*> MftReader::search(const std::wstring& keyword) {
    std::vector<const FileEntry*> results;
    std::wstring lowerKeyword = keyword;
    std::transform(lowerKeyword.begin(), lowerKeyword.end(), lowerKeyword.begin(), ::towlower);

    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<const FileEntry*> allEntries;
    allEntries.reserve(m_index.size());
    for (const auto& pair : m_index) {
        allEntries.push_back(&pair.second);
    }

    std::mutex resultsMutex;
    std::for_each(std::execution::par, allEntries.begin(), allEntries.end(), [&](const FileEntry* entry) {
        std::wstring lowerName = entry->name;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::towlower);

        if (lowerName.find(lowerKeyword) != std::wstring::npos) {
            std::lock_guard<std::mutex> resLock(resultsMutex);
            results.push_back(entry);
        }
    });

    return results;
}

} // namespace mft
