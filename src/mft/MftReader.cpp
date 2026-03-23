#include "MftReader.h"
#include <iostream>
#include <thread>

MftReader& MftReader::instance() {
    static MftReader inst;
    return inst;
}

bool MftReader::init(const std::wstring& drive) {
    m_drive = drive;
    std::wstring path = L"\\\\.\\" + drive;
    HANDLE hVolume = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 NULL, OPEN_EXISTING, 0, NULL);
    if (hVolume == INVALID_HANDLE_VALUE) return false;
    CloseHandle(hVolume);
    return true;
}

void MftReader::scanAsync(std::function<void()> callback) {
    if (m_isScanning) return;
    m_isScanning = true;

    std::thread([this, callback]() {
        std::unique_lock lock(m_mutex);
        m_index.clear();
        m_children.clear();
        m_index.reserve(1000000);

        std::wstring path = L"\\\\.\\" + m_drive;
        HANDLE hVolume = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                     NULL, OPEN_EXISTING, 0, NULL);
        if (hVolume != INVALID_HANDLE_VALUE) {
            USN_JOURNAL_DATA journalData;
            DWORD bytesReturned;
            if (DeviceIoControl(hVolume, FSCTL_QUERY_USN_JOURNAL, NULL, 0, &journalData, sizeof(journalData), &bytesReturned, NULL)) {
                MFT_ENUM_DATA_V0 enumData;
                enumData.StartFileReferenceNumber = 0;
                enumData.LowUsn = 0;
                enumData.HighUsn = journalData.NextUsn;

                uint8_t buffer[65536];
                while (DeviceIoControl(hVolume, FSCTL_ENUM_USN_DATA, &enumData, sizeof(enumData), buffer, sizeof(buffer), &bytesReturned, NULL)) {
                    PUSN_RECORD record = (PUSN_RECORD)&buffer[sizeof(USN)];
                    while ((uint8_t*)record < buffer + bytesReturned) {
                        FileEntry entry;
                        entry.frn = record->FileReferenceNumber;
                        entry.parentFrn = record->ParentFileReferenceNumber;
                        entry.name = std::wstring(record->FileName, record->FileNameLength / sizeof(WCHAR));
                        entry.attributes = record->FileAttributes;
                        m_index[entry.frn] = entry;
                        m_children[entry.parentFrn].push_back(entry.frn);

                        record = (PUSN_RECORD)((uint8_t*)record + record->RecordLength);
                    }
                    enumData.StartFileReferenceNumber = *(DWORDLONG*)buffer;
                }
            }
            CloseHandle(hVolume);
        }
        m_isScanning = false;
        // 2026-03-24 按照用户要求：在扫描完成后执行回调，确保 UI 线程安全
        if (callback) callback();
    }).detach();
}

FileEntry MftReader::getEntry(DWORDLONG frn) {
    std::shared_lock lock(m_mutex);
    auto it = m_index.find(frn);
    if (it != m_index.end()) return it->second;
    return {};
}

std::vector<FileEntry> MftReader::getChildren(DWORDLONG parentFrn) {
    std::shared_lock lock(m_mutex);
    std::vector<FileEntry> result;
    auto it = m_children.find(parentFrn);
    if (it != m_children.end()) {
        for (DWORDLONG childFrn : it->second) {
            result.push_back(m_index[childFrn]);
        }
    }
    return result;
}
