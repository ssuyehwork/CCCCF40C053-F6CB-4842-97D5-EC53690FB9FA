#include "PathBuilder.h"
#include "MftReader.h"
#include <algorithm>

std::wstring PathBuilder::getFullPath(DWORDLONG frn, const FileIndex& unused) {
    std::wstring path;
    DWORDLONG currentFrn = frn;
    int depth = 0;

    while (depth < 64) {
        FileEntry entry = MftReader::instance().getEntry(currentFrn);
        if (entry.frn == 0) break;

        if (!path.empty()) path = L"\\" + path;
        path = entry.name + path;

        // 根目录判断条件：(parentFrn & 0x0000FFFFFFFFFFFF) == 5
        if ((entry.parentFrn & 0x0000FFFFFFFFFFFF) == 5) {
            break;
        }

        currentFrn = entry.parentFrn;
        depth++;
    }

    return path;
}
