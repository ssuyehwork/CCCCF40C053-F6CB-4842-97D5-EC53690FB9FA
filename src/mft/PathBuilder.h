#ifndef PATHBUILDER_H
#define PATHBUILDER_H

#include "MftReader.h"
#include <string>

class PathBuilder {
public:
    // 2026-03-24 [NEW] 从 FRN 回溯完整物理路径
    static std::wstring getFullPath(DWORDLONG frn, const FileIndex& index, const std::wstring& volumeRoot) {
        std::wstring path;
        DWORDLONG currentFrn = frn;
        int depth = 0;

        while (depth < 64) { // 限制深度防死循环
            auto it = index.find(currentFrn);
            if (it == index.end()) break;

            if (!path.empty()) path = L"\\" + path;
            path = it->second.name + path;

            // 根目录判断条件：(parentFrn & 0x0000FFFFFFFFFFFF) == 5
            if ((it->second.parentFrn & 0x0000FFFFFFFFFFFF) == 5) {
                return volumeRoot + L"\\" + path;
            }

            currentFrn = it->second.parentFrn;
            depth++;
        }

        return volumeRoot + L"\\" + path;
    }
};

#endif // PATHBUILDER_H
