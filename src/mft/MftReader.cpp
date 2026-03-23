#include "MftReader.h"
#include <iostream>
#include <vector>
#include <algorithm>
#include <cwctype>

namespace mft {

MftReader& MftReader::instance() {
    static MftReader inst;
    return inst;
}

bool MftReader::loadVolumeIndex(const std::wstring& volumePath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_index.empty()) m_index.reserve(1000000);

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

    MFT_ENUM_DATA mftEnumData;
    mftEnumData.StartFileReferenceNumber = 0;
    mftEnumData.LowUsn = 0;
    mftEnumData.HighUsn = journalData.NextUsn;

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

        BYTE* current = buffer.data() + sizeof(DWORDLONG);
        BYTE* end = buffer.data() + bytesReturned;

        while (current < end) {
            // 2026-03-22 🟢 [MinGW 适配]：彻底废除 MSVC 专属的 USN_RECORD_V2，统一使用 USN_RECORD
            USN_RECORD* record = reinterpret_cast<USN_RECORD*>(current);

            FileEntry entry;
            entry.frn = record->FileReferenceNumber;
            entry.parentFrn = record->ParentFileReferenceNumber;
            entry.attributes = record->FileAttributes;
            entry.name = std::wstring(record->FileName, record->FileNameLength / sizeof(WCHAR));

            m_index[entry.frn] = std::move(entry);

            current += record->RecordLength;
        }

        mftEnumData.StartFileReferenceNumber = *reinterpret_cast<DWORDLONG*>(buffer.data());
    }

    return true;
}

std::vector<const FileEntry*> MftReader::search(const std::wstring& keyword) {
    std::vector<const FileEntry*> results;
    std::wstring lowerKeyword = keyword;
    std::transform(lowerKeyword.begin(), lowerKeyword.end(), lowerKeyword.begin(), ::towlower);

    std::lock_guard<std::mutex> lock(m_mutex);
    // 2026-03-22 🟢 [MinGW 适配]：彻底废除 std::execution::par 以避免链接失败 (MinGW 无自带 TBB)
    for (const auto& pair : m_index) {
        const FileEntry& entry = pair.second;
        std::wstring lowerName = entry.name;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::towlower);

        if (lowerName.find(lowerKeyword) != std::wstring::npos) {
            results.push_back(&entry);
        }
    }

    return results;
}

} // namespace mft
