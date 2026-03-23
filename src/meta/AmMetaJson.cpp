#include "AmMetaJson.h"
#include <QFileInfo>
#include <QDir>
#include <QDebug>

bool AmMetaJson::writeMetadataSafe(const std::wstring& folderPath, const QJsonObject& root) {
    QString path = QString::fromStdWString(folderPath) + "/.am_meta.json";
    QString tmpPath = path + ".tmp";

    QJsonDocument doc(root);
    QFile tmpFile(tmpPath);
    if (!tmpFile.open(QIODevice::WriteOnly)) return false;

    tmpFile.write(doc.toJson());
    tmpFile.close();

    // 重新解析验证
    QFile verifyFile(tmpPath);
    if (!verifyFile.open(QIODevice::ReadOnly)) return false;
    QJsonDocument verifyDoc = QJsonDocument::fromJson(verifyFile.readAll());
    verifyFile.close();

    if (verifyDoc.isNull()) {
        QFile::remove(tmpPath);
        return false;
    }

    // 原子替换
    if (QFile::exists(path)) QFile::remove(path);
    return QFile::rename(tmpPath, path);
}

QJsonObject AmMetaJson::readMetadata(const std::wstring& folderPath) {
    QString path = QString::fromStdWString(folderPath) + "/.am_meta.json";
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return QJsonObject();

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    return doc.object();
}
