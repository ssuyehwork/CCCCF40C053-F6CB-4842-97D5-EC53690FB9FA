#ifndef FILESTORAGEHELPER_H
#define FILESTORAGEHELPER_H

#include <QString>
#include <QStringList>
#include <QObject>

class FileStorageHelper {
public:
    /**
     * @brief 处理导入逻辑
     * @param paths 物理路径列表
     * @param targetCategoryId 目标分类ID，如果为-1且导入的是单文件夹，则自动创建分类
     * @return 成功导入的项目数量
     */
    static int processImport(const QStringList& paths, int targetCategoryId = -1);

    static QString getStorageRoot();
    static QString getUniqueFilePath(const QString& dirPath, const QString& fileName);
    static bool copyRecursively(const QString& srcStr, const QString& dstStr);

private:
    static void storeFile(const QString& path, int categoryId);
    static void storeFolderAsCategory(const QString& path, int parentCategoryId);
    static void storeItemsAsBatch(const QStringList& paths, int categoryId);
};

#endif // FILESTORAGEHELPER_H
