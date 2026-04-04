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
 */
struct RuntimeMeta {
    int rating = 0;
    std::wstring color;
    QStringList tags;
    std::wstring note;
    bool pinned = false;
    bool encrypted = false;
    bool isLoaded = false; // 用于判定是否已从磁盘同步过现有数据
};

/**
 * @brief 元数据管理器
 */
class MetadataManager : public QObject {
    Q_OBJECT
public:
    static MetadataManager& instance();

    /**
     * @brief 高效查询接口
     */
    RuntimeMeta getMeta(const std::wstring& path);

    /**
     * @brief 统一更新接口
     */
    void setRating(const std::wstring& path, int rating);
    void setColor(const std::wstring& path, const std::wstring& color);
    void setPinned(const std::wstring& path, bool pinned);
    void setTags(const std::wstring& path, const QStringList& tags);
    void setNote(const std::wstring& path, const std::wstring& note);

signals:
    void metaChanged(const std::wstring& path);

private:
    MetadataManager(QObject* parent = nullptr);
    ~MetadataManager() override = default;

    std::unordered_map<std::wstring, RuntimeMeta> m_cache;
    mutable std::shared_mutex m_mutex;

    void persistAsync(const std::wstring& path);
};

} // namespace ArcMeta
