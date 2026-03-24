#ifndef PATHBUILDER_H
#define PATHBUILDER_H

#include "MftReader.h"
#include <string>
#include <vector>

class PathBuilder {
public:
    // 2026-03-24 [NEW] 从 FRN 重建路径
    static std::wstring buildPath(DWORDLONG frn, MftReader* reader, const std::wstring& driveRoot) {
        if (!reader) return L"";

        std::wstring path;
        DWORDLONG currentFrn = frn;
        int depth = 0;
        const int MAX_DEPTH = 64; // 按照要求限制递归深度

        const FileIndex& index = reader->getIndex();
        std::mutex& mtx = reader->getMutex();

        while (depth < MAX_DEPTH) {
            std::lock_guard<std::mutex> lock(mtx);
            auto it = index.find(currentFrn);
            if (it == index.end()) break;

            const FileEntry& entry = it->second;
            if (path.empty()) path = entry.name;
            else path = entry.name + L"\\" + path;

            // 根目录判断逻辑：(parentFrn & 0x0000FFFFFFFFFFFF) == 5
            if ((entry.parentFrn & 0x0000FFFFFFFFFFFF) == 5) {
                break;
            }

            currentFrn = entry.parentFrn;
            depth++;
        }

        if (driveRoot.empty()) return path;

        std::wstring root = driveRoot;
        if (root.back() == L'\\') root.pop_back();
        return root + L"\\" + path;
    }
};

#endif // PATHBUILDER_H
