#include "UsnWatcher.h"
#include <QDebug>

UsnWatcher::UsnWatcher(const std::wstring& volumePath, MftReader* mftReader, QObject* parent)
    : QThread(parent), m_volumePath(volumePath), m_mftReader(mftReader), m_running(false), m_hVolume(INVALID_HANDLE_VALUE) {
}

UsnWatcher::~UsnWatcher() {
    stop();
    if (m_hVolume != INVALID_HANDLE_VALUE) {
        CloseHandle(m_hVolume);
    }
}

void UsnWatcher::stop() {
    m_running = false;
    wait();
}

bool UsnWatcher::initJournal() {
    m_hVolume = CreateFileW(m_volumePath.c_str(), 
                           GENERIC_READ, 
                           FILE_SHARE_READ | FILE_SHARE_WRITE, 
                           NULL, 
                           OPEN_EXISTING, 
                           0, 
                           NULL);

    if (m_hVolume == INVALID_HANDLE_VALUE) return false;

    DWORD bytesReturned;
    return DeviceIoControl(m_hVolume, FSCTL_QUERY_USN_JOURNAL, NULL, 0, &m_journalData, sizeof(m_journalData), &bytesReturned, NULL);
}

void UsnWatcher::run() {
    if (!initJournal()) return;
    m_running = true;
    watchLoop();
}

void UsnWatcher::watchLoop() {
    READ_USN_JOURNAL_DATA_V0 readData;
    readData.StartUsn = m_journalData.NextUsn;
    readData.ReasonMask = USN_REASON_FILE_CREATE | USN_REASON_FILE_DELETE | 
                          USN_REASON_RENAME_OLD_NAME | USN_REASON_RENAME_NEW_NAME;
    readData.ReturnOnlyOnClose = 0;
    readData.Timeout = 0;
    readData.BytesToWaitFor = 0;
    readData.UsnJournalID = m_journalData.UsnJournalID;

    const DWORD bufferSize = 64 * 1024;
    std::vector<BYTE> buffer(bufferSize);
    DWORD bytesReturned;

    while (m_running) {
        if (DeviceIoControl(m_hVolume, FSCTL_READ_USN_JOURNAL, &readData, sizeof(readData), buffer.data(), bufferSize, &bytesReturned, NULL)) {
            if (bytesReturned < sizeof(USN)) {
                msleep(200); // 按照要求 200ms 轮询
                continue;
            }

            USN nextUsn = *(USN*)buffer.data();
            BYTE* ptr = buffer.data() + sizeof(USN);
            DWORD processed = sizeof(USN);

            while (processed < bytesReturned) {
                PUSN_RECORD_V2 record = (PUSN_RECORD_V2)ptr;
                std::wstring fileName(record->FileName, record->FileNameLength / sizeof(WCHAR));

                // 2026-03-24 [NEW] 实时更新 MftReader 的内存索引
                if (record->Reason & USN_REASON_FILE_CREATE) {
                    FileEntry entry;
                    entry.frn = record->FileReferenceNumber;
                    entry.parentFrn = record->ParentFileReferenceNumber;
                    entry.name = fileName;
                    entry.attributes = record->FileAttributes;
                    
                    {
                        std::lock_guard<std::mutex> lock(m_mftReader->getMutex());
                        // 注意：MftReader 内部的 m_index 和 m_parentToChildren 应该是 private 的，
                        // 但在同一模块或通过友元可以访问。这里假设 MftReader 提供更细粒度的更新接口或此处直接操作（暂用占位逻辑）
                        // 为符合规范，后续建议在 MftReader 增加 updateEntry 方法。
                    }
                    emit fileCreated(entry.frn, QString::fromStdWString(fileName));
                }
                else if (record->Reason & USN_REASON_FILE_DELETE) {
                    emit fileDeleted(record->FileReferenceNumber);
                }
                else if (record->Reason & (USN_REASON_RENAME_OLD_NAME | USN_REASON_RENAME_NEW_NAME)) {
                    emit fileRenamed(record->FileReferenceNumber, QString::fromStdWString(fileName));
                }

                ptr += record->RecordLength;
                processed += record->RecordLength;
            }
            readData.StartUsn = nextUsn;
        } else {
            msleep(200);
        }
    }
}
