#ifndef MFTREADER_H
#define MFTREADER_H

#include <windows.h>
#include <winioctl.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <shared_mutex>

struct FileEntry {
    DWORDLONG frn;         // File Reference Number
    DWORDLONG parentFrn;   // 父目录 FRN
    std::wstring name;     // 文件名（宽字符）
    DWORD attributes;      // 文件属性
    bool isDir() const { return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0; }
};

using FileIndex = std::unordered_map<DWORDLONG, FileEntry>;
using ChildrenMap = std::unordered_map<DWORDLONG, std::vector<DWORDLONG>>;

class MftReader {
public:
    static MftReader& instance();

    bool init(const std::wstring& drive);
    void scanAsync(std::function<void()> callback = nullptr);

    FileEntry getEntry(DWORDLONG frn);
    std::vector<FileEntry> getChildren(DWORDLONG parentFrn);

private:
    MftReader() = default;
    std::wstring m_drive;
    FileIndex m_index;
    ChildrenMap m_children; // parentFrn -> list of children FRNs
    std::shared_mutex m_mutex;
    bool m_isScanning = false;
};

#endif // MFTREADER_H
