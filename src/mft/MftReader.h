#ifndef MFTREADER_H
#define MFTREADER_H

#include <windows.h>
#include <winternl.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <QObject>

struct FileEntry {
    DWORDLONG frn;         // File Reference Number
    DWORDLONG parentFrn;   // 父目录 FRN
    std::wstring name;     // 文件名（宽字符）
    DWORD attributes;      // 文件属性
    bool isDir() const { return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0; }
};

using FileIndex = std::unordered_map<DWORDLONG, FileEntry>;

class MftReader : public QObject {
    Q_OBJECT
public:
    explicit MftReader(QObject* parent = nullptr);
    ~MftReader();

    // 2026-03-24 [NEW] 核心扫描接口，支持多卷扫描
    bool scanVolume(const std::wstring& volumePath);
    bool walkdir(const std::wstring& volumePath);
    
    // 获取 FileIndex 引用（需加锁访问）
    const FileIndex& getIndex() const { return m_index; }
    std::mutex& getMutex() { return m_mutex; }

    // 获取特定 FRN 的子节点列表 (O(1) 检索)
    std::vector<DWORDLONG> getChildren(DWORDLONG parentFrn);

    // 2026-03-24 [NEW] 增量更新接口
    void addEntry(const FileEntry& entry);
    void removeEntry(DWORDLONG frn);

signals:
    void scanStarted(const QString& volume);
    void scanProgress(int count);
    void scanFinished(int totalCount);
    void errorOccurred(const QString& error);

private:
    FileIndex m_index;
    std::unordered_map<DWORDLONG, std::vector<DWORDLONG>> m_parentToChildren; // 层级映射
    std::mutex m_mutex;

    // 内部帮助函数
    bool readMft(HANDLE volumeHandle);
};

#endif // MFTREADER_H
