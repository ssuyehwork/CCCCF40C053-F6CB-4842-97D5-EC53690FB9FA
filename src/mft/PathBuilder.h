#ifndef PATHBUILDER_H
#define PATHBUILDER_H

#include <string>
#include <vector>
#include "MftReader.h"

namespace mft {

class PathBuilder {
public:
    static std::wstring buildPath(DWORDLONG frn, const FileIndex& index, const std::wstring& driveRoot = L"C:");

private:
    static const int MAX_DEPTH = 64; // 最大递归深度
};

} // namespace mft

#endif // PATHBUILDER_H
