#include "AmMetaJson.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QDir>
#include <string>
#include <map>
#include <vector>

namespace ArcMeta {

/**
 * @brief 构造函数，确定目标元数据文件路径
 */
AmMetaJson::AmMetaJson(const std::wstring& folderPath)
    : m_folderPath(folderPath) {
    std::wstring path = folderPath;
    if (!path.empty() && path.back() != L'\\' && path.back() != L'/') {
        path += L'\\';
    }
    m_filePath = path + L".am_meta.json";
}

/**
 * @brief 加载元数据文件
 */
bool AmMetaJson::load() {
    QString qPath = toQString(m_filePath);
    QFile file(qPath);

    if (!file.exists()) {
        m_folder = FolderMeta();
        m_items.clear();
        return true;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    QByteArray fileData = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(fileData, &parseError);

    if (doc.isNull() || !doc.isObject()) {
        return false;
    }

    QJsonObject root = doc.object();

    if (root.contains("folder") && root["folder"].isObject()) {
        m_folder = entryToFolder(root["folder"].toObject());
    } else {
        m_folder = FolderMeta();
    }

    m_items.clear();
    if (root.contains("items") && root["items"].isObject()) {
        QJsonObject itemsObj = root["items"].toObject();
        for (auto it = itemsObj.begin(); it != itemsObj.end(); ++it) {
            if (it.value().isObject()) {
                m_items[toStdWString(it.key())] = entryToItem(it.value().toObject());
            }
        }
    }

    return true;
}

/**
 * @brief 安全写入元数据文件
 */
bool AmMetaJson::save() const {
    QJsonObject root;
    root["version"] = "1";
    root["folder"] = folderToEntry(m_folder);

    QJsonObject itemsObj;
    for (const auto& [name, meta] : m_items) {
        if (meta.hasUserOperations()) {
            itemsObj[toQString(name)] = itemToEntry(meta);
        }
    }
    root["items"] = itemsObj;

    QJsonDocument doc(root);
    QByteArray jsonData = doc.toJson(QJsonDocument::Indented);

    QString tmpPath = toQString(m_filePath) + ".tmp";
    QFile tmpFile(tmpPath);
    if (!tmpFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    if (tmpFile.write(jsonData) != jsonData.size()) {
        tmpFile.close();
        tmpFile.remove();
        return false;
    }
    tmpFile.close();

    QFile checkFile(tmpPath);
    if (!checkFile.open(QIODevice::ReadOnly)) {
        checkFile.remove();
        return false;
    }
    QByteArray checkData = checkFile.readAll();
    checkFile.close();

    QJsonParseError checkError;
    QJsonDocument checkDoc = QJsonDocument::fromJson(checkData, &checkError);
    if (checkDoc.isNull() || !checkDoc.isObject()) {
        QFile::remove(tmpPath);
        return false;
    }

#ifdef Q_OS_WIN
    if (!MoveFileExW(tmpPath.toStdWString().c_str(), m_filePath.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        QFile::remove(tmpPath);
        return false;
    }

    SetFileAttributesW(m_filePath.c_str(), FILE_ATTRIBUTE_HIDDEN);
#else
    if (QFile::exists(toQString(m_filePath))) {
        QFile::remove(toQString(m_filePath));
    }
    if (!QFile::rename(tmpPath, toQString(m_filePath))) {
        QFile::remove(tmpPath);
        return false;
    }
    // 非 Windows 平台使用 . 前缀表示隐藏（通常在路径逻辑中已处理）
#endif

    return true;
}

/**
 * @brief 物理重命名元数据条目
 */
bool AmMetaJson::renameItem(const QString& folderPath, const QString& oldName, const QString& newName) {
    if (oldName == newName) return true;

    AmMetaJson meta(folderPath.toStdWString());
    if (!meta.load()) return false;

    auto& items = meta.items();
    std::wstring wOld = oldName.toStdWString();
    std::wstring wNew = newName.toStdWString();

    auto it = items.find(wOld);
    if (it != items.end()) {
        items[wNew] = it->second;
        items.erase(it);
        return meta.save();
    }

    return true;
}

// --- 内部辅助函数：转换 ---

QJsonObject AmMetaJson::folderToEntry(const FolderMeta& meta) {
    QJsonObject obj;
    obj["sort_by"] = toQString(meta.sortBy);
    obj["sort_order"] = toQString(meta.sortOrder);
    obj["rating"] = meta.rating;
    obj["color"] = toQString(meta.color);
    obj["pinned"] = meta.pinned;
    obj["note"] = toQString(meta.note);

    QJsonArray tagsArr;
    for (const auto& tag : meta.tags) {
        tagsArr.append(toQString(tag));
    }
    obj["tags"] = tagsArr;

    return obj;
}

FolderMeta AmMetaJson::entryToFolder(const QJsonObject& obj) {
    FolderMeta meta;
    if (obj.contains("sort_by")) meta.sortBy = toStdWString(obj["sort_by"].toString());
    if (obj.contains("sort_order")) meta.sortOrder = toStdWString(obj["sort_order"].toString());
    if (obj.contains("rating")) meta.rating = obj["rating"].toInt();
    if (obj.contains("color")) meta.color = toStdWString(obj["color"].toString());
    if (obj.contains("pinned")) meta.pinned = obj["pinned"].toBool();
    if (obj.contains("note")) meta.note = toStdWString(obj["note"].toString());

    if (obj.contains("tags") && obj["tags"].isArray()) {
        QJsonArray tagsArr = obj["tags"].toArray();
        for (const auto& tag : tagsArr) {
            meta.tags.push_back(toStdWString(tag.toString()));
        }
    }
    return meta;
}

QJsonObject AmMetaJson::itemToEntry(const ItemMeta& meta) {
    QJsonObject obj;
    obj["type"] = toQString(meta.type);
    obj["rating"] = meta.rating;
    obj["color"] = toQString(meta.color);
    obj["pinned"] = meta.pinned;
    obj["note"] = toQString(meta.note);
    obj["encrypted"] = meta.encrypted;
    obj["encrypt_salt"] = QString::fromStdString(meta.encryptSalt);

    QByteArray ivData = QByteArray::fromStdString(meta.encryptIv);
    obj["encrypt_iv"] = QString::fromLatin1(ivData.toBase64());

    obj["encrypt_verify_hash"] = QString::fromStdString(meta.encryptVerifyHash);
    obj["original_name"] = toQString(meta.originalName);
    obj["volume"] = toQString(meta.volume);
    obj["frn"] = toQString(meta.frn);

    QJsonArray tagsArr;
    for (const auto& tag : meta.tags) {
        tagsArr.append(toQString(tag));
    }
    obj["tags"] = tagsArr;

    return obj;
}

ItemMeta AmMetaJson::entryToItem(const QJsonObject& obj) {
    ItemMeta meta;
    if (obj.contains("type")) meta.type = toStdWString(obj["type"].toString());
    if (obj.contains("rating")) meta.rating = obj["rating"].toInt();
    if (obj.contains("color")) meta.color = toStdWString(obj["color"].toString());
    if (obj.contains("pinned")) meta.pinned = obj["pinned"].toBool();
    if (obj.contains("note")) meta.note = toStdWString(obj["note"].toString());
    if (obj.contains("encrypted")) meta.encrypted = obj["encrypted"].toBool();
    if (obj.contains("encrypt_salt")) meta.encryptSalt = obj["encrypt_salt"].toString().toStdString();

    if (obj.contains("encrypt_iv")) {
        QByteArray base64Iv = obj["encrypt_iv"].toString().toLatin1();
        meta.encryptIv = QByteArray::fromBase64(base64Iv).toStdString();
    }

    if (obj.contains("encrypt_verify_hash")) meta.encryptVerifyHash = obj["encrypt_verify_hash"].toString().toStdString();
    if (obj.contains("original_name")) meta.originalName = toStdWString(obj["original_name"].toString());
    if (obj.contains("volume")) meta.volume = toStdWString(obj["volume"].toString());
    if (obj.contains("frn")) meta.frn = toStdWString(obj["frn"].toString());

    if (obj.contains("tags") && obj["tags"].isArray()) {
        QJsonArray tagsArr = obj["tags"].toArray();
        for (const auto& tag : tagsArr) {
            meta.tags.push_back(toStdWString(tag.toString()));
        }
    }
    return meta;
}

} // namespace ArcMeta
