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

    // 1. 获取当前日志状态，设置起始监听位置
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
    readData.ReasonMask = 0xFFFFFFFF; // 监听所有事件
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

            // 更新下一次读取的起始位置 (缓冲区前 8 字节)
            readData.StartUsn = *reinterpret_cast<USN*>(buffer.data());

            // [THREAD-SAFETY] 为监听线程创建独立数据库连接
            QString connectionName = QString("UsnWatcher_Thread_%1").arg(reinterpret_cast<quintptr>(QThread::currentThreadId()));
            {
                QSqlDatabase threadDb = QSqlDatabase::addDatabase("QSQLITE", connectionName);
                threadDb.setDatabaseName(db::Database::instance().getDbPath());
                if (!threadDb.open()) {
                    // qWarning() << "UsnWatcher thread failed to open database";
                }

                BYTE* current = buffer.data() + sizeof(USN);
                BYTE* end = buffer.data() + bytesReturned;

                while (current < end) {
                    USN_RECORD_V2* record = reinterpret_cast<USN_RECORD_V2*>(current);

                    // 处理文件系统事件
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
                            // 构建被删除项目的完整路径
                            std::wstring wpath = PathBuilder::buildPath(record->FileReferenceNumber, index);
                            QString path = QString::fromStdWString(wpath);

                            index.erase(record->FileReferenceNumber);

                            // 级联从数据库删除 (按路径清理)
                            if (!path.isEmpty() && threadDb.isOpen()) {
                                QSqlQuery q(threadDb);
                                q.prepare("DELETE FROM folders WHERE path = ?");
                                q.addBindValue(path);
                                q.exec();

                                q.prepare("DELETE FROM items WHERE path = ?");
                                q.addBindValue(path);
                                q.exec();

                                // 级联清理 items 表中 parent_path 等于该路径的所有记录
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
            } // close threadDb
            QSqlDatabase::removeDatabase(connectionName);

        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }

    CloseHandle(hVolume);
}

} // namespace mft
