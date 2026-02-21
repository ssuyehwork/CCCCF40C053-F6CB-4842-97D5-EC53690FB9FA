#ifndef FILESTORAGEHELPER_H
#define FILESTORAGEHELPER_H

#include <QString>
#include <QStringList>
#include <QObject>

class FramelessProgressDialog;

class FileStorageHelper {
public:
    /**
     * @brief 处理导入逻辑 (支持拖拽和剪贴板触发)
     * @param paths 物理路径列表
     * @param targetCategoryId 目标父分类ID，默认为 -1 (根分类)
     * @return 成功导入的项目总数 (文件+分类)
     */
    static int processImport(const QStringList& paths, int targetCategoryId = -1, bool fromClipboard = false);

    /**
     * @brief 递归导出分类结构及笔记到物理路径
     * @param categoryId 要导出的分类ID
     * @param targetPath 目标物理文件夹路径
     * @return 导出的项目总数
     */
    static int exportCategory(int categoryId, const QString& targetPath);

    static QString getStorageRoot();
    static QString getUniqueFilePath(const QString& dirPath, const QString& fileName);
    
    /**
     * @brief 计算路径列表的总大小 (字节)
     */
    static qint64 calculateTotalSize(const QStringList& paths);

private:
    /**
     * @brief 递归导入文件夹为分类结构
     */
    static int importFolderRecursive(const QString& folderPath, int parentCategoryId, 
                                   QList<int>& createdNoteIds, QList<int>& createdCatIds,
                                   FramelessProgressDialog* progress = nullptr, qint64* processedSize = nullptr, bool fromClipboard = false);
    
    /**
     * @brief 导入单个文件到指定分类
     */
    static bool storeFile(const QString& path, int categoryId, 
                         QList<int>& createdNoteIds,
                         FramelessProgressDialog* progress = nullptr, qint64* processedSize = nullptr, bool fromClipboard = false);

    /**
     * @brief 解析 CSV 文件并导入为笔记
     */
    static int parseCsvFile(const QString& csvPath, int catId, QList<int>* createdNoteIds = nullptr);

    /**
     * @brief 辅助：递归拷贝文件夹
     */
    static bool copyRecursively(const QString& srcPath, const QString& dstPath);
};

#endif // FILESTORAGEHELPER_H
