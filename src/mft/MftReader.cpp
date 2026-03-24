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
        // [USER_REQUEST] 2026-03-24 按照需求：无权限时降级为 walkdir 递归遍历
        qDebug() << "[MftReader] 无法打开卷句柄，权限不足，降级至 walkdir 扫描...";
        return walkdir(volumePath);
    }

    bool success = readMft(hVolume);
    CloseHandle(hVolume);

    if (success) {
        emit scanFinished(static_cast<int>(m_index.size()));
    }
    return success;
}

#include <QDirIterator>
bool MftReader::walkdir(const std::wstring& volumePath) {
    QString root = QString::fromStdWString(volumePath);
    if (root.startsWith(L"\\\\.\\")) {
        root = root.mid(4) + "/";
    }

    QDirIterator it(root, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
    int count = 0;

    // FRN 模拟逻辑：对于 walkdir，由于无法直接获取 FRN，我们使用路径哈希模拟 (仅供展示，生产环境建议调用 GetFileInformationByHandle)
    // 但为保持 FileIndex 结构一致，我们尽量获取真实 FRN
    while (it.hasNext()) {
        QString path = it.next();
        QFileInfo info = it.fileInfo();

        FileEntry entry;
        entry.name = info.fileName().toStdWString();
        entry.attributes = (DWORD)info.permissions(); // 简化属性
        if (info.isDir()) entry.attributes |= FILE_ATTRIBUTE_DIRECTORY;

        // 尝试获取真实 FRN
        HANDLE hFile = CreateFileW(path.toStdWString().c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
        if (hFile != INVALID_HANDLE_VALUE) {
            BY_HANDLE_FILE_INFORMATION fileInfo;
            if (GetFileInformationByHandle(hFile, &fileInfo)) {
                entry.frn = ((DWORDLONG)fileInfo.nFileIndexHigh << 32) | fileInfo.nFileIndexLow;
                // 注意：parentFrn 在 walkdir 模式下较难高效获取，此处暂存 path 并在内存中维护映射
            }
            CloseHandle(hFile);
        } else {
            entry.frn = qHash(path); // 保底方案
        }

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_index[entry.frn] = entry;
        }

        count++;
        if (count % 1000 == 0) emit scanProgress(count);
    }

    emit scanFinished(count);
    return true;
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
