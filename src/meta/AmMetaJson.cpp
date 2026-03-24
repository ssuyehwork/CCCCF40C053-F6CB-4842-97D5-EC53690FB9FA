#include "AmMetaJson.h"
#include <QFile>
#include <QJsonArray>
#include <QFileInfo>
#include <QDir>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

std::wstring AmMetaJson::getMetaPath(const std::wstring& folderPath) {
    return folderPath + L"\\.am_meta.json";
}

bool AmMetaJson::load(const std::wstring& folderPath, FolderMeta& folder, std::map<std::string, ItemMeta>& items) {
    QString path = QString::fromStdWString(getMetaPath(folderPath));
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (doc.isNull()) return false;

    QJsonObject root = doc.object();

    // 解析 folder
    QJsonObject fObj = root["folder"].toObject();
    folder.sortBy = fObj["sort_by"].toString("name").toStdString();
    folder.sortOrder = fObj["sort_order"].toString("asc").toStdString();
    folder.rating = fObj["rating"].toInt();
    folder.color = fObj["color"].toString().toStdString();
    folder.remark = fObj["remark"].toString().toStdString();
    folder.pinned = fObj["pinned"].toBool();

    QJsonArray fTags = fObj["tags"].toArray();
    for (auto t : fTags) folder.tags.push_back(t.toString().toStdString());

    // 解析 items
    QJsonObject iObj = root["items"].toObject();
    for (auto it = iObj.begin(); it != iObj.end(); ++it) {
        QJsonObject item = it.value().toObject();
        ItemMeta meta;
        meta.type = item["type"].toString().toStdString();
        meta.rating = item["rating"].toInt();
        meta.color = item["color"].toString().toStdString();
        meta.pinned = item["pinned"].toBool();
        meta.remark = item["remark"].toString().toStdString();

        QJsonArray iTags = item["tags"].toArray();
        for (auto t : iTags) meta.tags.push_back(t.toString().toStdString());

        items[it.key().toStdString()] = meta;
    }

    return true;
}

bool AmMetaJson::save(const std::wstring& folderPath, const FolderMeta& folder, const std::map<std::string, ItemMeta>& items) {
    QString finalPath = QString::fromStdWString(getMetaPath(folderPath));
    QString tmpPath = finalPath + ".tmp";

    QJsonObject root;
    root["version"] = "1";

    QJsonObject fObj;
    fObj["sort_by"] = QString::fromStdString(folder.sortBy);
    fObj["sort_order"] = QString::fromStdString(folder.sortOrder);
    fObj["rating"] = folder.rating;
    fObj["color"] = QString::fromStdString(folder.color);
    fObj["pinned"] = folder.pinned;
    fObj["remark"] = QString::fromStdString(folder.remark);

    QJsonArray fTags;
    for (const auto& t : folder.tags) fTags.append(QString::fromStdString(t));
    fObj["tags"] = fTags;
    root["folder"] = fObj;

    QJsonObject iObj;
    for (const auto& [name, meta] : items) {
        // items 只记录有过用户操作的条目
        if (meta.rating == 0 && meta.color.empty() && meta.tags.empty() && !meta.pinned && meta.remark.empty()) continue;

        QJsonObject item;
        item["type"] = QString::fromStdString(meta.type);
        item["rating"] = meta.rating;
        item["color"] = QString::fromStdString(meta.color);
        item["pinned"] = meta.pinned;
        item["remark"] = QString::fromStdString(meta.remark);

        QJsonArray iTags;
        for (const auto& t : meta.tags) iTags.append(QString::fromStdString(t));
        item["tags"] = iTags;

        iObj[QString::fromStdString(name)] = item;
    }
    root["items"] = iObj;

    // 1. 写入临时文件
    QFile file(tmpPath);
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.write(QJsonDocument(root).toJson());
    file.close();

    // 2. 校验临时文件
    QFile checkFile(tmpPath);
    if (!checkFile.open(QIODevice::ReadOnly)) return false;
    QJsonDocument checkDoc = QJsonDocument::fromJson(checkFile.readAll());
    if (checkDoc.isNull()) {
        checkFile.close();
        QFile::remove(tmpPath);
        return false;
    }
    checkFile.close();

    // 3. 原子重命名
    if (QFile::exists(finalPath)) QFile::remove(finalPath);
    bool ok = QFile::rename(tmpPath, finalPath);

    if (ok) {
        // [USER_REQUEST] 2026-03-24 按照要求：将 .am_meta.json 设为隐藏文件
#ifdef Q_OS_WIN
        std::wstring metaPath = getMetaPath(folderPath);
        SetFileAttributesW(metaPath.c_str(), FILE_ATTRIBUTE_HIDDEN);
#endif
    }
    return ok;
}
