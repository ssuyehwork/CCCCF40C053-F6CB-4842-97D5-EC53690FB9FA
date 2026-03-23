#include "AmMetaJson.h"

namespace meta {

const QString AmMetaJson::META_FILENAME = ".am_meta.json";

QString AmMetaJson::getMetaPath(const QString& folderPath) {
    return QDir(folderPath).filePath(META_FILENAME);
}

bool AmMetaJson::read(const QString& folderPath, FolderMeta& folderMeta, QMap<QString, ItemMeta>& items) {
    QString metaPath = getMetaPath(folderPath);
    if (!QFile::exists(metaPath)) {
        return false;
    }

    QFile file(metaPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    QByteArray data = file.readAll();
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError) {
        qWarning() << "Failed to parse .am_meta.json in" << folderPath << ":" << error.errorString();
        return false;
    }

    QJsonObject root = doc.object();
    if (root.contains("folder")) {
        folderMeta = FolderMeta::fromJson(root["folder"].toObject());
    }

    if (root.contains("items")) {
        QJsonObject itemsObj = root["items"].toObject();
        for (auto it = itemsObj.begin(); it != itemsObj.end(); ++it) {
            items.insert(it.key(), ItemMeta::fromJson(it.value().toObject()));
        }
    }

    return true;
}

bool AmMetaJson::write(const QString& folderPath, const FolderMeta& folderMeta, const QMap<QString, ItemMeta>& items) {
    QString metaPath = getMetaPath(folderPath);
    QString tmpPath = metaPath + ".tmp";

    QJsonObject root;
    root["version"] = "1";
    root["folder"] = folderMeta.toJson();

    QJsonObject itemsObj;
    for (auto it = items.begin(); it != items.end(); ++it) {
        if (!it.value().isDefault()) {
            itemsObj.insert(it.key(), it.value().toJson());
        }
    }
    root["items"] = itemsObj;

    QJsonDocument doc(root);
    QByteArray data = doc.toJson(QJsonDocument::Indented);

    // 1. 写入临时文件
    QFile tmpFile(tmpPath);
    if (!tmpFile.open(QIODevice::WriteOnly)) {
        qWarning() << "Failed to open temporary file for writing:" << tmpPath;
        return false;
    }

    if (tmpFile.write(data) == -1) {
        tmpFile.close();
        tmpFile.remove();
        return false;
    }
    tmpFile.close();

    // 2. 重新解析验证
    QJsonParseError error;
    QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError) {
        qWarning() << "Verification failed for .am_meta.json.tmp:" << error.errorString();
        tmpFile.remove();
        return false;
    }

    // 3. 原子替换 (Windows 下 rename 行为在文件已存在时可能不同，Qt 的 QFile::rename 处理得当)
    if (QFile::exists(metaPath)) {
        if (!QFile::remove(metaPath)) {
            qWarning() << "Failed to remove existing .am_meta.json";
            return false;
        }
    }

    if (!QFile::rename(tmpPath, metaPath)) {
        qWarning() << "Failed to rename .tmp to .am_meta.json";
        return false;
    }

    return true;
}

} // namespace meta
