#ifndef AMMETAJSON_H
#define AMMETAJSON_H

#include <string>
#include <vector>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFile>
#include <QSaveFile>

struct AmItemMeta {
    std::string type; // "file" | "folder"
    int rating = 0;
    std::string color;
    std::vector<std::string> tags;
    bool pinned = false;
};

class AmMetaJson {
public:
    static bool writeMetadataSafe(const std::wstring& folderPath, const QJsonObject& root);
    static QJsonObject readMetadata(const std::wstring& folderPath);
};

#endif // AMMETAJSON_H
