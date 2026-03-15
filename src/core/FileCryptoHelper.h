#ifndef FILECRYPTOHELPER_H
#define FILECRYPTOHELPER_H

#include <QString>
#include <QByteArray>
#include <QFile>

class FileCryptoHelper {
public:
    // 三层架构专用：带魔数的壳加密/解密
    static bool encryptFileWithShell(const QString& sourcePath, const QString& destPath, const QString& password);
    static bool decryptFileWithShell(const QString& sourcePath, const QString& destPath, const QString& password);
    
    // 旧版解密 (Legacy): 不检查魔数
    static bool decryptFileLegacy(const QString& sourcePath, const QString& destPath, const QString& password);
    
    // 获取设备指纹与内置 Hardcode 结合的密钥
    static QString getCombinedKey();

    // 安全删除文件（覆盖后再删除）
    static bool secureDelete(const QString& filePath);

private:
    static QByteArray deriveKey(const QString& password, const QByteArray& salt);
};

#endif // FILECRYPTOHELPER_H
