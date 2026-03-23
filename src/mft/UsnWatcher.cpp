#include "UsnWatcher.h"
#include <iostream>
#include <vector>
#include <chrono>
#include <QSqlQuery>
#include <QSqlError>
#include <QDir>
#include <QThread>
#include <QDebug>
#include "../db/Database.h"
#include "PathBuilder.h"

namespace mft {

UsnWatcher& UsnWatcher::instance() {
    static UsnWatcher inst;
    return inst;
}

bool UsnWatcher::start(const std::wstring& volumePath) {
    if (m_running) return true;
    m_running = true;
    m_thread = std::thread(&UsnWatcher::watchThread, this, volumePath);
    return true;
}

void UsnWatcher::stop() {
    m_running = false;
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void UsnWatcher::watchThread(std::wstring volumePath) {
    HANDLE hVolume = CreateFileW(volumePath.c_str(),
                                GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                NULL,
                                OPEN_EXISTING,
                                0,
                                NULL);

    if (hVolume == INVALID_HANDLE_VALUE) {
        m_running = false;
        return;
    }

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
        CloseHandle(hVolume);
        m_running = false;
        return;
    }

    READ_USN_JOURNAL_DATA readData;
    readData.StartUsn = journalData.NextUsn;
    readData.ReasonMask = 0xFFFFFFFF;
    readData.ReturnOnlyOnClose = FALSE;
    readData.Timeout = 0;
    readData.BytesToWaitFor = 0;
    readData.UsnJournalID = journalData.UsnJournalID;

    const DWORD BUFFER_SIZE = 4096;
    std::vector<BYTE> buffer(BUFFER_SIZE);

    while (m_running) {
        if (DeviceIoControl(hVolume,
                             FSCTL_READ_USN_JOURNAL,
                             &readData,
                             sizeof(readData),
                             buffer.data(),
                             BUFFER_SIZE,
                             &bytesReturned,
                             NULL)) {

            if (bytesReturned < sizeof(USN)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                continue;
            }

            readData.StartUsn = *reinterpret_cast<USN*>(buffer.data());

            QString connectionName = QString("UsnWatcher_Thread_%1").arg(reinterpret_cast<quintptr>(QThread::currentThreadId()));
            {
                QSqlDatabase threadDb = QSqlDatabase::addDatabase("QSQLITE", connectionName);
                threadDb.setDatabaseName(db::Database::instance().getDbPath());
                if (!threadDb.open()) {
                }

                BYTE* current = buffer.data() + sizeof(USN);
                BYTE* end = buffer.data() + bytesReturned;

                while (current < end) {
                    // 2026-03-22 🟢 [MinGW 适配]：彻底废除 MSVC 专属的 USN_RECORD_V2，统一使用 USN_RECORD
                    USN_RECORD* record = reinterpret_cast<USN_RECORD*>(current);

                    {
                        std::lock_guard<std::mutex> lock(MftReader::instance().getMutex());
                        auto& index = MftReader::instance().getIndex();

                        if (record->Reason & USN_REASON_FILE_CREATE) {
                            FileEntry entry;
                            entry.frn = record->FileReferenceNumber;
                            entry.parentFrn = record->ParentFileReferenceNumber;
                            entry.attributes = record->FileAttributes;
                            entry.name = std::wstring(record->FileName, record->FileNameLength / sizeof(WCHAR));
                            index[entry.frn] = std::move(entry);
                        } else if (record->Reason & (USN_REASON_FILE_DELETE | USN_REASON_RENAME_OLD_NAME)) {
                            std::wstring wpath = PathBuilder::buildPath(record->FileReferenceNumber, index);
                            QString path = QString::fromStdWString(wpath);

                            index.erase(record->FileReferenceNumber);

                            if (!path.isEmpty() && threadDb.isOpen()) {
                                QSqlQuery q(threadDb);
                                q.prepare("DELETE FROM folders WHERE path = ?");
                                q.addBindValue(path);
                                q.exec();

                                q.prepare("DELETE FROM items WHERE path = ?");
                                q.addBindValue(path);
                                q.exec();

                                q.prepare("DELETE FROM items WHERE parent_path = ?");
                                q.addBindValue(path);
                                q.exec();
                            }
                        } else if (record->Reason & USN_REASON_RENAME_NEW_NAME) {
                            FileEntry entry;
                            entry.frn = record->FileReferenceNumber;
                            entry.parentFrn = record->ParentFileReferenceNumber;
                            entry.attributes = record->FileAttributes;
                            entry.name = std::wstring(record->FileName, record->FileNameLength / sizeof(WCHAR));
                            index[entry.frn] = std::move(entry);
                        }
                    }

                    current += record->RecordLength;
                }
            }
            QSqlDatabase::removeDatabase(connectionName);

        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }

    CloseHandle(hVolume);
}

} // namespace mft
