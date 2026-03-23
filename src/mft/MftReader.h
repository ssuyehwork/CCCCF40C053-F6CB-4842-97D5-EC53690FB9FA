#ifndef MFTREADER_H
#define MFTREADER_H

#include <windows.h>
#include <winioctl.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>

namespace mft {

struct FileEntry {
    DWORDLONG frn;         // File Reference Number
    DWORDLONG parentFrn;   // Parent FRN
    std::wstring name;     // Filename
    DWORD attributes;      // Attributes
    bool isDir() const { return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0; }
};

using FileIndex = std::unordered_map<DWORDLONG, FileEntry>;

class MftReader {
public:
    static MftReader& instance();

    // 加载卷全量 MFT 索引
    bool loadVolumeIndex(const std::wstring& volumePath);

    // 清空现有索引
    void clear() { std::lock_guard<std::mutex> lock(m_mutex); m_index.clear(); }

    // 获取当前内存中的文件索引
    FileIndex& getIndex() { return m_index; }
    std::mutex& getMutex() { return m_mutex; }

    // 并行搜索支持
    std::vector<const FileEntry*> search(const std::wstring& keyword);

private:
    MftReader() = default;
    ~MftReader() = default;
    MftReader(const MftReader&) = delete;
    MftReader& operator=(const MftReader&) = delete;

    // 批量枚举 MFT 记录的底层方法
    bool scanMft(HANDLE hVolume);

    FileIndex m_index;
    std::mutex m_mutex;
};

} // namespace mft

#endif // MFTREADER_H
