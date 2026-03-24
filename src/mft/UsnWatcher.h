#ifndef USNWATCHER_H
#define USNWATCHER_H

#include <QThread>
#include <windows.h>
#include <string>
#include "MftReader.h"

class UsnWatcher : public QThread {
    Q_OBJECT
public:
    explicit UsnWatcher(const std::wstring& volumePath, MftReader* mftReader, QObject* parent = nullptr);
    ~UsnWatcher();

    void stop();

signals:
    void fileCreated(DWORDLONG frn, const QString& name);
    void fileDeleted(DWORDLONG frn);
    void fileRenamed(DWORDLONG frn, const QString& newName);

protected:
    void run() override;

private:
    std::wstring m_volumePath;
    MftReader* m_mftReader;
    bool m_running;
    HANDLE m_hVolume;
    USN_JOURNAL_DATA_V0 m_journalData;

    bool initJournal();
    void watchLoop();
};

#endif // USNWATCHER_H
