#ifndef PATHBUILDER_H
#define PATHBUILDER_H

#include <windows.h>
#include <string>
#include <unordered_map>
#include "MftReader.h"

class PathBuilder {
public:
    static std::wstring getFullPath(DWORDLONG frn, const FileIndex& index);
};

#endif // PATHBUILDER_H
