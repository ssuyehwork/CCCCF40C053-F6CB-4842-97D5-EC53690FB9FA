#include "SyncQueue.h"
#include <QDebug>
#include <QCoreApplication>
#include <QTimer>
#include <QThreadPool>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include "AmMetaJson.h"
#include "../db/Database.h"

namespace meta {

SyncQueue& SyncQueue::instance() {
    static SyncQueue inst;
    return inst;
}

SyncQueue::SyncQueue(QObject* parent) : QObject(parent) {
    m_syncTimer = new QTimer(this);
    m_syncTimer->setInterval(2000); // 2秒同步一次
    connect(m_syncTimer, &QTimer::timeout, this, &SyncQueue::processQueue);
    m_syncTimer->start();
}

SyncQueue::~SyncQueue() {
    flush();
}

void SyncQueue::enqueue(const QString& folderPath) {
    QMutexLocker locker(&m_mutex);
    m_pendingPaths.insert(folderPath);
}

void SyncQueue::flush() {
    processQueue();
}

void SyncQueue::processQueue() {
    QMutexLocker locker(&m_mutex);
    if (m_isProcessing || m_pendingPaths.isEmpty()) return;
    m_isProcessing = true;

    QSet<QString> pathsToProcess = m_pendingPaths;
    m_pendingPaths.clear();
    locker.unlock(); // 释放锁，允许在处理时继续添加新路径

    // [PERF] 将耗时的数据库批量写入移至线程池执行
    QThreadPool::globalInstance()->start([this, pathsToProcess]() {
        db::Database& database = db::Database::instance();
    if (!database.beginTransaction()) {
        qWarning() << "Failed to start transaction for SyncQueue";
        m_isProcessing = false;
        return;
    }

    QSqlQuery folderQuery(database.getDb());
    folderQuery.prepare(R"(
        REPLACE INTO folders (path, rating, color, tags, pinned, sort_by, sort_order, last_sync)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?)
    )");

    QSqlQuery itemQuery(database.getDb());
    itemQuery.prepare(R"(
        REPLACE INTO items (path, type, rating, color, tags, pinned, parent_path)
        VALUES (?, ?, ?, ?, ?, ?, ?)
    )");

    for (const QString& folderPath : pathsToProcess) {
        FolderMeta folderMeta;
        QMap<QString, ItemMeta> items;
        if (AmMetaJson::read(folderPath, folderMeta, items)) {
            // 更新文件夹自身元数据到 folders 表
            QFileInfo folderInfo(AmMetaJson::getMetaPath(folderPath));
            double mtime = folderInfo.lastModified().toMSecsSinceEpoch() / 1000.0;

            folderQuery.addBindValue(folderPath);
            folderQuery.addBindValue(folderMeta.rating);
            folderQuery.addBindValue(folderMeta.color);
            folderQuery.addBindValue(QJsonDocument::fromVariant(folderMeta.tags).toJson(QJsonDocument::Compact));
            folderQuery.addBindValue(folderMeta.pinned ? 1 : 0);
            folderQuery.addBindValue(folderMeta.sortBy);
            folderQuery.addBindValue(folderMeta.sortOrder);
            folderQuery.addBindValue(mtime);
            if (!folderQuery.exec()) {
                qWarning() << "Failed to replace folder meta for" << folderPath << folderQuery.lastError().text();
            }

            // 更新子项目元数据到 items 表
            for (auto it = items.begin(); it != items.end(); ++it) {
                QString itemPath = QDir(folderPath).filePath(it.key());
                itemQuery.addBindValue(itemPath);
                itemQuery.addBindValue(it.value().type);
                itemQuery.addBindValue(it.value().rating);
                itemQuery.addBindValue(it.value().color);
                itemQuery.addBindValue(QJsonDocument::fromVariant(it.value().tags).toJson(QJsonDocument::Compact));
                itemQuery.addBindValue(it.value().pinned ? 1 : 0);
                itemQuery.addBindValue(folderPath);
                if (!itemQuery.exec()) {
                    qWarning() << "Failed to replace item meta for" << itemPath << itemQuery.lastError().text();
                }
            }
            emit syncFinished(folderPath);
        }
    }

        if (!database.commit()) {
            qWarning() << "Failed to commit transaction for SyncQueue";
        }

        QMutexLocker locker(&m_mutex);
        m_isProcessing = false;
    });
}

} // namespace meta
