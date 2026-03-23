#ifndef AMMETAJSON_H
#define AMMETAJSON_H

#include <QString>
#include <QVariantMap>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QSaveFile>
#include <QDebug>

namespace meta {

struct ItemMeta {
    QString type; // "file" | "folder"
    int rating = 0;
    QString color;
    QStringList tags;
    bool pinned = false;

    QJsonObject toJson() const {
        QJsonObject obj;
        obj["type"] = type;
        obj["rating"] = rating;
        obj["color"] = color;
        QJsonArray tagsArray;
        for (const auto& tag : tags) tagsArray.append(tag);
        obj["tags"] = tagsArray;
        obj["pinned"] = pinned;
        return obj;
    }

    static ItemMeta fromJson(const QJsonObject& obj) {
        ItemMeta meta;
        meta.type = obj["type"].toString();
        meta.rating = obj["rating"].toInt();
        meta.color = obj["color"].toString();
        QJsonArray tagsArray = obj["tags"].toArray();
        for (auto tagValue : tagsArray) meta.tags.append(tagValue.toString());
        meta.pinned = obj["pinned"].toBool();
        return meta;
    }

    bool isDefault() const {
        return rating == 0 && color.isEmpty() && tags.isEmpty() && !pinned;
    }
};

struct FolderMeta {
    QString sortBy = "name";
    QString sortOrder = "asc";
    int rating = 0;
    QString color;
    QStringList tags;
    bool pinned = false;

    QJsonObject toJson() const {
        QJsonObject obj;
        obj["sort_by"] = sortBy;
        obj["sort_order"] = sortOrder;
        obj["rating"] = rating;
        obj["color"] = color;
        QJsonArray tagsArray;
        for (const auto& tag : tags) tagsArray.append(tag);
        obj["tags"] = tagsArray;
        obj["pinned"] = pinned;
        return obj;
    }

    static FolderMeta fromJson(const QJsonObject& obj) {
        FolderMeta meta;
        meta.sortBy = obj["sort_by"].toString("name");
        meta.sortOrder = obj["sort_order"].toString("asc");
        meta.rating = obj["rating"].toInt();
        meta.color = obj["color"].toString();
        QJsonArray tagsArray = obj["tags"].toArray();
        for (auto tagValue : tagsArray) meta.tags.append(tagValue.toString());
        meta.pinned = obj["pinned"].toBool();
        return meta;
    }
};

class AmMetaJson {
public:
    static const QString META_FILENAME;

    // 安全读取：解析 .am_meta.json
    static bool read(const QString& folderPath, FolderMeta& folderMeta, QMap<QString, ItemMeta>& items);

    // 安全写入逻辑：临时文件 -> 验证 -> 原子替换
    static bool write(const QString& folderPath, const FolderMeta& folderMeta, const QMap<QString, ItemMeta>& items);

    // 获取 .am_meta.json 的完整路径
    static QString getMetaPath(const QString& folderPath);
};

} // namespace meta

#endif // AMMETAJSON_H
