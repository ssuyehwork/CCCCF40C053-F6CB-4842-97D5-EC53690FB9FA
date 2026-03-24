#ifndef AMMETAJSON_H
#define AMMETAJSON_H

#include <string>
#include <vector>
#include <map>
#include <QJsonObject>
#include <QJsonDocument>

class AmMetaJson {
public:
    struct ItemMeta {
        std::string type;
        int rating = 0;
        std::string color;
        std::vector<std::string> tags;
        bool pinned = false;
        std::string remark;
    };

    struct FolderMeta {
        std::string sortBy = "name";
        std::string sortOrder = "asc";
        int rating = 0;
        std::string color;
        std::vector<std::string> tags;
        bool pinned = false;
        std::string remark;
    };

    // 2026-03-24 [NEW] 安全读写接口
    static bool load(const std::wstring& folderPath, FolderMeta& folder, std::map<std::string, ItemMeta>& items);
    static bool save(const std::wstring& folderPath, const FolderMeta& folder, const std::map<std::string, ItemMeta>& items);

private:
    static std::wstring getMetaPath(const std::wstring& folderPath);
};

#endif // AMMETAJSON_H
