#include "UsnWatcher.h"
#include "MftReader.h"
#include "PathBuilder.h"
#include "../db/Database.h"
#include <winioctl.h>
#include <QDebug>
#include <QSqlQuery>

UsnWatcher::UsnWatcher() : QObject(nullptr) {}

UsnWatcher::~UsnWatcher() {
    stop();
}

UsnWatcher& UsnWatcher::instance() {
    static UsnWatcher inst;
    return inst;
}

bool UsnWatcher::start(const std::wstring& drive) {
    if (m_running) return true;

    m_drive = drive;
    std::wstring path = L"\\\\.\\" + drive;
    m_hVolume = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            NULL, OPEN_EXISTING, 0, NULL);
    if (m_hVolume == INVALID_HANDLE_VALUE) return false;

    USN_JOURNAL_DATA journalData;
    DWORD bytesReturned;
    if (!DeviceIoControl(m_hVolume, FSCTL_QUERY_USN_JOURNAL, NULL, 0, &journalData, sizeof(journalData), &bytesReturned, NULL)) {
        CloseHandle(m_hVolume);
        return false;
    }
    m_nextUsn = journalData.NextUsn;
    m_running = true;
    m_thread = std::thread(&UsnWatcher::watchLoop, this);
    return true;
}

void UsnWatcher::stop() {
    m_running = false;
    if (m_thread.joinable()) m_thread.join();
    if (m_hVolume != INVALID_HANDLE_VALUE) {
        CloseHandle(m_hVolume);
        m_hVolume = INVALID_HANDLE_VALUE;
    }
}

void UsnWatcher::watchLoop() {
    uint8_t buffer[65536];
    READ_USN_JOURNAL_DATA readData;
    readData.StartUsn = m_nextUsn;
    readData.ReasonMask = 0xFFFFFFFF;
    readData.ReturnOnlyOnClose = FALSE;
    readData.Timeout = 0;
    readData.BytesToWaitFor = 0;
    readData.UsnJournalID = 0; // Will be set later if needed

    USN_JOURNAL_DATA journalData;
    DWORD bytesReturned;
    if (DeviceIoControl(m_hVolume, FSCTL_QUERY_USN_JOURNAL, NULL, 0, &journalData, sizeof(journalData), &bytesReturned, NULL)) {
        readData.UsnJournalID = journalData.UsnJournalID;
    }

    while (m_running) {
        if (DeviceIoControl(m_hVolume, FSCTL_READ_USN_JOURNAL, &readData, sizeof(readData), buffer, sizeof(buffer), &bytesReturned, NULL)) {
            if (bytesReturned < sizeof(USN)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                continue;
            }

            PUSN_RECORD record = (PUSN_RECORD)&buffer[sizeof(USN)];
            while ((uint8_t*)record < buffer + bytesReturned) {
                // 处理事件
                DWORD reason = record->Reason;
                if (reason & USN_REASON_FILE_CREATE) {
                    FileEntry entry;
                    entry.frn = record->FileReferenceNumber;
                    entry.parentFrn = record->ParentFileReferenceNumber;
                    entry.name = std::wstring(record->FileName, record->FileNameLength / sizeof(WCHAR));
                    entry.attributes = record->FileAttributes;
                    MftReader::instance().addEntry(entry);
                } else if (reason & (USN_REASON_FILE_DELETE | USN_REASON_RENAME_OLD_NAME)) {
                    // 获取路径以便清理数据库
                    std::wstring path = PathBuilder::getFullPath(record->FileReferenceNumber, {});
                    MftReader::instance().removeEntry(record->FileReferenceNumber);

                    if (!path.empty()) {
                        QString qPath = QString::fromStdWString(path);
                        QSqlDatabase db = QSqlDatabase::database("file_index_db");
                        if (db.isOpen()) {
                            QSqlQuery q(db);
                            // 1. 删除 folders 表中对应路径
                            q.prepare("DELETE FROM folders WHERE path = ?");
                            q.addBindValue(qPath);
                            q.exec();

                            // 2. 删除 items 表中对应路径
                            q.prepare("DELETE FROM items WHERE path = ?");
                            q.addBindValue(qPath);
                            q.exec();

                            // 3. 级联删除 items 中子项 (如果是目录)
                            q.prepare("DELETE FROM items WHERE parent_path = ? OR parent_path LIKE ?");
                            q.addBindValue(qPath);
                            q.addBindValue(qPath + "/%");
                            q.exec();
                        }
                    }
                } else if (reason & USN_REASON_RENAME_NEW_NAME) {
                    FileEntry entry;
                    entry.frn = record->FileReferenceNumber;
                    entry.parentFrn = record->ParentFileReferenceNumber;
                    entry.name = std::wstring(record->FileName, record->FileNameLength / sizeof(WCHAR));
                    entry.attributes = record->FileAttributes;
                    MftReader::instance().addEntry(entry);
                }

                record = (PUSN_RECORD)((uint8_t*)record + record->RecordLength);
            }
            readData.StartUsn = *(USN*)buffer;
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }
}
