#include "PathBuilder.h"
#include <algorithm>

namespace mft {

std::wstring PathBuilder::buildPath(DWORDLONG frn, const FileIndex& index, const std::wstring& driveRoot) {
    std::vector<std::wstring> pathParts;
    DWORDLONG currentFrn = frn;
    int depth = 0;

    while (depth < MAX_DEPTH) {
        auto it = index.find(currentFrn);
        if (it == index.end()) break;

        const FileEntry& entry = it->second;
        pathParts.push_back(entry.name);

        // 根目录判断条件：(parentFrn & 0x0000FFFFFFFFFFFF) == 5 (通常是 C: 等分区的根)
        if ((entry.parentFrn & 0x0000FFFFFFFFFFFFULL) == 5ULL) {
            pathParts.push_back(driveRoot);
            break;
        }

        currentFrn = entry.parentFrn;
        depth++;
    }

    if (pathParts.empty()) return L"";

    // 反转并拼接路径
    std::reverse(pathParts.begin(), pathParts.end());
    std::wstring fullPath;
    for (size_t i = 0; i < pathParts.size(); ++i) {
        fullPath += pathParts[i];
        if (i < pathParts.size() - 1) {
            fullPath += L"\\";
        }
    }

    return fullPath;
}

} // namespace mft
