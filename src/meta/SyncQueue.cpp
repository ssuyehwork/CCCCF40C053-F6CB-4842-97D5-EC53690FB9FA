#include "SyncQueue.h"
#include <QCoreApplication>
#include <QDebug>

SyncQueue& SyncQueue::instance() {
    static SyncQueue inst;
    return inst;
}

SyncQueue::SyncQueue(QObject* parent) : QObject(parent) {
    m_debounceTimer = new QTimer(this);
    m_debounceTimer->setSingleShot(true);
    m_debounceTimer->setInterval(2000); // 2秒防抖

    connect(m_debounceTimer, &QTimer::timeout, this, [this](){
        flush();
    });
}

SyncQueue::~SyncQueue() {
    flush();
}

void SyncQueue::enqueue(const QString& folderPath) {
    QMutexLocker locker(&m_mutex);
    m_pendingPaths.insert(folderPath);
    
    // 每次加入都重置定时器，实现防抖
    if (QThread::currentThread() == qApp->thread()) {
        m_debounceTimer->start();
    } else {
        QMetaObject::invokeMethod(m_debounceTimer, "start", Qt::QueuedConnection);
    }
}

void SyncQueue::flush() {
    QStringList toSync;
    {
        QMutexLocker locker(&m_mutex);
        if (m_pendingPaths.isEmpty()) return;
        toSync = m_pendingPaths.values();
        m_pendingPaths.clear();
    }

    qDebug() << "[SyncQueue] 触发批量同步，路径总数:" << toSync.size();
    emit syncRequired(toSync);
}
