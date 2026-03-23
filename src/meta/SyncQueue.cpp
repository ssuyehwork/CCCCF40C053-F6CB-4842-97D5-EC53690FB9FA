#include "SyncQueue.h"
#include "AmMetaJson.h"
#include "../db/Database.h"
#include <QDebug>
#include <QDateTime>
#include <chrono>
#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

SyncQueue::SyncQueue() {
    m_running = true;
    m_thread = std::thread(&SyncQueue::processLoop, this);
}

SyncQueue::~SyncQueue() {
    stop();
}

SyncQueue& SyncQueue::instance() {
    static SyncQueue inst;
    return inst;
}

void SyncQueue::enqueue(const std::wstring& folderPath) {
    std::unique_lock<std::mutex> lock(m_mutex);
    if (m_pendingPaths.find(folderPath) == m_pendingPaths.end()) {
        m_queue.push_back(folderPath);
        m_pendingPaths.insert(folderPath);
        m_cv.notify_one();
    }
}

void SyncQueue::stop() {
    m_running = false;
    m_cv.notify_all();
    if (m_thread.joinable()) m_thread.join();
}

// 2026-03-24 按照用户要求：实现带防抖合并的元数据懒更新队列
void SyncQueue::processLoop() {
    // 2026-03-24 按照用户要求：确保程序关闭前刷空队列
    while (m_running || !m_queue.empty()) {
        std::wstring path;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this] { return !m_queue.empty() || !m_running; });
            if (!m_running && m_queue.empty()) break;

            // 2026-03-24 按照用户要求：简单的防抖合并处理
            // 取出并处理
            path = m_queue.back();
            m_queue.pop_back();
            m_pendingPaths.erase(path);
        }

        if (path.empty()) continue;

        // 解析 JSON 并更新数据库
        QJsonObject root = AmMetaJson::read(path);
        if (root.isEmpty()) continue;

        QSqlDatabase db = QSqlDatabase::database("file_index_db");
        if (!db.isOpen()) continue;

        db.transaction();
        QSqlQuery q(db);

        // 更新 folders 表
        QJsonObject folderMeta = root["folder"].toObject();
        q.prepare("INSERT OR REPLACE INTO folders (path, rating, color, tags, pinned, sort_by, sort_order, last_sync) "
                  "VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
        q.addBindValue(QString::fromStdWString(path));
        q.addBindValue(folderMeta["rating"].toInt());
        q.addBindValue(folderMeta["color"].toString());
        q.addBindValue(QJsonDocument(folderMeta["tags"].toArray()).toJson(QJsonDocument::Compact));
        q.addBindValue(folderMeta["pinned"].toBool() ? 1 : 0);
        q.addBindValue(folderMeta["sort_by"].toString());
        q.addBindValue(folderMeta["sort_order"].toString());
        q.addBindValue(QDateTime::currentDateTime().toMSecsSinceEpoch() / 1000.0);
        q.exec();

        // 更新 items 表
        QJsonObject items = root["items"].toObject();
        for (auto it = items.begin(); it != items.end(); ++it) {
            QString name = it.key();
            QJsonObject itemMeta = it.value().toObject();
            QString itemPath = QString::fromStdWString(path) + "/" + name;

            q.prepare("INSERT OR REPLACE INTO items (path, type, rating, color, tags, pinned, parent_path) "
                      "VALUES (?, ?, ?, ?, ?, ?, ?)");
            q.addBindValue(itemPath);
            q.addBindValue(itemMeta["type"].toString());
            q.addBindValue(itemMeta["rating"].toInt());
            q.addBindValue(itemMeta["color"].toString());
            q.addBindValue(QJsonDocument(itemMeta["tags"].toArray()).toJson(QJsonDocument::Compact));
            q.addBindValue(itemMeta["pinned"].toBool() ? 1 : 0);
            q.addBindValue(QString::fromStdWString(path));
            q.exec();
        }

        if (!db.commit()) {
            qWarning() << "[SyncQueue] Commit failed:" << db.lastError().text();
        }

        // 留出 CPU 空档
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}
