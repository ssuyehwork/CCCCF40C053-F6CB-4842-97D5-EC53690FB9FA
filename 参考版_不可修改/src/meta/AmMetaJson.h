#pragma once

#include <string>
#include <vector>
#include <map>
#include <QString>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>

namespace ArcMeta {

/**
 * @brief 文件夹级别的元数据
 */
struct FolderMeta {
    std::wstring sortBy = L"name";
    std::wstring sortOrder = L"asc";
    int rating = 0;
    std::wstring color = L"";
    std::vector<std::wstring> tags;
    bool pinned = false;
    std::wstring note = L"";

    // 判断是否为空（即均为默认值，无需写入 items 列表时使用，此处用于 folder 节点总是存在）
    bool isDefault() const {
        return sortBy == L"name" && sortOrder == L"asc" && rating == 0 &&
               color.empty() && tags.empty() && !pinned && note.empty();
    }
};

/**
 * @brief 单个条目（文件或子文件夹）的元数据
 */
struct ItemMeta {
    std::wstring type = L"file"; // "file" | "folder"
    int rating = 0;
    std::wstring color = L"";
    std::vector<std::wstring> tags;
    bool pinned = false;
    std::wstring note = L"";
    bool encrypted = false;
    std::string encryptSalt;      // 存储为字符串
    std::string encryptIv;        // Base64 字符串
    std::string encryptVerifyHash;
    std::wstring originalName;
    std::wstring volume;
    std::wstring frn;             // 十六进制字符串存储，避免溢出

    /**
     * @brief 判断该条目是否有过用户操作。
     * 只有满足该条件的条目才会被序列化到 JSON 的 items 节点中。
     */
    bool hasUserOperations() const {
        return rating > 0 || !color.empty() || !tags.empty() || pinned ||
               !note.empty() || encrypted;
    }
};

/**
 * @brief 处理 .am_meta.json 的读写类，包含安全写入逻辑
 */
class AmMetaJson {
public:
    /**
     * @param folderPath 目标文件夹的完整路径（不含文件名）
     */
    explicit AmMetaJson(const std::wstring& folderPath);

    /**
     * @brief 加载 .am_meta.json 文件
     * @return 加载成功返回 true，文件不存在返回 true（视为初始化），解析错误返回 false
     */
    bool load();

    /**
     * @brief 安全保存到 .am_meta.json 文件
     * 遵循：写临时文件 -> 校验 -> 原子替换 -> 设置隐藏属性 流程
     */
    bool save() const;

    // 数据访问接口
    FolderMeta& folder() { return m_folder; }
    const FolderMeta& folder() const { return m_folder; }

    std::map<std::wstring, ItemMeta>& items() { return m_items; }
    const std::map<std::wstring, ItemMeta>& items() const { return m_items; }

    /**
     * @brief 获取元数据文件的完整路径
     */
    std::wstring getMetaFilePath() const { return m_filePath; }

    /**
     * @brief 静态辅助方法：物理重命名元数据条目
     * @param folderPath 所在目录
     * @param oldName 旧文件名
     * @param newName 新文件名
     */
    static bool renameItem(const QString& folderPath, const QString& oldName, const QString& newName);

private:
    std::wstring m_folderPath;
    std::wstring m_filePath;
    
    FolderMeta m_folder;
    std::map<std::wstring, ItemMeta> m_items;

    // 内部转换辅助
    static QJsonObject folderToEntry(const FolderMeta& meta);
    static FolderMeta entryToFolder(const QJsonObject& obj);
    static QJsonObject itemToEntry(const ItemMeta& meta);
    static ItemMeta entryToItem(const QJsonObject& obj);

    static QString toQString(const std::wstring& ws) { return QString::fromStdWString(ws); }
    static std::wstring toStdWString(const QString& qs) { return qs.toStdWString(); }
};

} // namespace ArcMeta
