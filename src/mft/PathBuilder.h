#ifndef PATHBUILDER_H
#define PATHBUILDER_H

#include "MftReader.h"
#include <string>
#include <vector>
#include <mutex>

class PathBuilder {
public:
    // 2026-03-24 [NEW] 从 FRN 重建完整路径
    static std::wstring getFullPath(DWORDLONG frn, const FileIndex& index, const std::wstring& driveRoot) {
        std::wstring path;
        DWORDLONG currentFrn = frn;
        int depth = 0;
        const int MAX_DEPTH = 64;

        while (depth < MAX_DEPTH) {
            auto it = index.find(currentFrn);
            if (it == index.end()) break;

            const FileEntry& entry = it->second;
            if (path.empty()) path = entry.name;
            else path = entry.name + L"\\" + path;

            // 根目录判断
            if ((entry.parentFrn & 0x0000FFFFFFFFFFFF) == 5) {
                break;
            }

            currentFrn = entry.parentFrn;
            depth++;
        }

        std::wstring root = driveRoot;
        if (!root.empty()) {
            if (root.back() == L'\\') root.pop_back();
            return root + L"\\" + path;
        }
        return path;
    }

    // 兼容旧接口
    static std::wstring buildPath(DWORDLONG frn, MftReader* reader, const std::wstring& driveRoot) {
        if (!reader) return L"";
        std::lock_guard<std::mutex> lock(reader->getMutex());
        return getFullPath(frn, reader->getIndex(), driveRoot);
    }
};

#endif // PATHBUILDER_H
