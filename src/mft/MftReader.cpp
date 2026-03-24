#include "MftReader.h"
#include <iostream>
#include <chrono>

MftReader::MftReader(QObject* parent) : QObject(parent) {
    m_index.reserve(1000000); // 按照要求预分配 1,000,000 容量
}

MftReader::~MftReader() {
}

bool MftReader::scanVolume(const std::wstring& volumePath) {
    emit scanStarted(QString::fromStdWString(volumePath));

    HANDLE hVolume = CreateFileW(volumePath.c_str(), 
                                GENERIC_READ, 
                                FILE_SHARE_READ | FILE_SHARE_WRITE, 
                                NULL, 
                                OPEN_EXISTING, 
                                FILE_ATTRIBUTE_NORMAL, 
                                NULL);

    if (hVolume == INVALID_HANDLE_VALUE) {
        emit errorOccurred("无法打开卷句柄 (需要管理员权限)");
        return false;
    }

    bool success = readMft(hVolume);
    CloseHandle(hVolume);

    if (success) {
        emit scanFinished(static_cast<int>(m_index.size()));
    }
    return success;
}

bool MftReader::readMft(HANDLE volumeHandle) {
    USN_JOURNAL_DATA_V0 journalData;
    DWORD bytesReturned;

    // 获取 USN Journal 信息
    if (!DeviceIoControl(volumeHandle, FSCTL_QUERY_USN_JOURNAL, NULL, 0, &journalData, sizeof(journalData), &bytesReturned, NULL)) {
        emit errorOccurred("查询 USN Journal 失败");
        return false;
    }

    MFT_ENUM_DATA_V0 enumData;
    enumData.StartFileReferenceNumber = 0;
    enumData.LowUsn = 0;
    enumData.HighUsn = journalData.NextUsn;

    const DWORD bufferSize = 64 * 1024; // 64KB 缓冲区
    std::vector<BYTE> buffer(bufferSize);

    int count = 0;
    while (DeviceIoControl(volumeHandle, FSCTL_ENUM_USN_DATA, &enumData, sizeof(enumData), buffer.data(), bufferSize, &bytesReturned, NULL)) {
        if (bytesReturned < sizeof(USN_RECORD_V2)) break;

        BYTE* ptr = buffer.data() + sizeof(USN_RECORD_V2); // 指向第一条记录（跳过返回的大小头）
        // 注意：FSCTL_ENUM_USN_DATA 返回的是 USN_RECORD 列表，前面会有个 USN 的首部
        // 正确处理方式是跳过前面的 USN 列表头
        
        ptr = buffer.data() + sizeof(USN); // 真正的起始
        DWORD processed = sizeof(USN);

        while (processed < bytesReturned) {
            PUSN_RECORD_V2 record = (PUSN_RECORD_V2)ptr;
            
            FileEntry entry;
            entry.frn = record->FileReferenceNumber;
            entry.parentFrn = record->ParentFileReferenceNumber;
            entry.name = std::wstring(record->FileName, record->FileNameLength / sizeof(WCHAR));
            entry.attributes = record->FileAttributes;

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_index[entry.frn] = entry;
                m_parentToChildren[entry.parentFrn].push_back(entry.frn);
            }

            ptr += record->RecordLength;
            processed += record->RecordLength;
            count++;

            if (count % 10000 == 0) {
                emit scanProgress(count);
            }
        }
        enumData.StartFileReferenceNumber = *(DWORDLONG*)buffer.data();
    }

    return true;
}

std::vector<DWORDLONG> MftReader::getChildren(DWORDLONG parentFrn) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_parentToChildren.count(parentFrn)) {
        return m_parentToChildren.at(parentFrn);
    }
    return {};
}
