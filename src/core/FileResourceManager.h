#ifndef FILERESOURCEMANAGER_H
#define FILERESOURCEMANAGER_H

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <string>
#include "../meta/AmMetaJson.h"

namespace ArcMeta {

/**
 * @brief 超级资源管理器核心层
 * 负责物理文件与元数据的统合管理
 */
class FileResourceManager : public QObject {
    Q_OBJECT
public:
    static FileResourceManager& instance();

    /**
     * @brief 获取指定路径的完整元数据（优先从 JSON 读取）
     */
    ItemMeta getItemMeta(const QString& fullPath);

    /**
     * @brief 更新文件元数据并持久化
     */
    bool setItemMeta(const QString& fullPath, const ItemMeta& meta);

    /**
     * @brief 物理操作同步接口
     */
    bool renameFile(const QString& oldPath, const QString& newPath);
    bool moveFile(const QString& srcPath, const QString& dstDir);

private:
    FileResourceManager(QObject* parent = nullptr);
};

} // namespace ArcMeta

#endif // FILERESOURCEMANAGER_H
