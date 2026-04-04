#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <unordered_map>
#include <shared_mutex>
#include <string>

namespace ArcMeta {

/**
 * @brief 内存元数据镜像结构
 * 仅保留 UI 高频展现和筛选所需的字段，极致压缩内存占用。
 */
struct RuntimeMeta {
    int rating = 0;
    std::wstring color;
    QStringList tags;
    bool pinned = false;
    bool encrypted = false;
};

/**
 * @brief 元数据管理器 (架构核心层)
 * 1. 维护元数据内存 L1 缓存，消除 UI 刷新的磁盘 IO。
 * 2. 统一数据更新入口，确保 Memory, DB, JSON 三方一致性。
 */
class MetadataManager : public QObject {
    Q_OBJECT
public:
    static MetadataManager& instance();

    /**
     * @brief 从数据库初始化内存镜像
     */
    void initFromDatabase();

    /**
     * @brief 高效查询接口 (O(1))
     */
    RuntimeMeta getMeta(const std::wstring& path);

    /**
     * @brief 统一更新接口
     */
    void setRating(const std::wstring& path, int rating);
    void setColor(const std::wstring& path, const std::wstring& color);
    void setPinned(const std::wstring& path, bool pinned);
    void setTags(const std::wstring& path, const QStringList& tags);

signals:
    /**
     * @brief 元数据变更信号，UI 订阅此信号以实现局部刷新
     */
    void metaChanged(const std::wstring& path);

private:
    MetadataManager(QObject* parent = nullptr);
    ~MetadataManager() override = default;

    // 路径到元数据的映射
    std::unordered_map<std::wstring, RuntimeMeta> m_cache;
    mutable std::shared_mutex m_mutex;

    /**
     * @brief 异步持久化：刷入数据库和 JSON
     */
    void persistAsync(const std::wstring& path);
};

} // namespace ArcMeta
