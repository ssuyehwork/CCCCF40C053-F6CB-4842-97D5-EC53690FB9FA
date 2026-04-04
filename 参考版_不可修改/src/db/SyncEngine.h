#pragma once

#include <string>
#include <vector>
#include <functional>
#include <filesystem>

namespace ArcMeta {

/**
 * @brief 同步引擎
 * 负责离线变更追平（增量同步）与系统全量扫描逻辑
 */
class SyncEngine {
public:
    static SyncEngine& instance();

    /**
     * @brief 启动增量同步（程序启动时自动调用）
     */
    void runIncrementalSync();

    /**
     * @brief 启动全量扫描（由用户手动触发）
     * @param onProgress 进度回调
     */
    void runFullScan(std::function<void(int current, int total)> onProgress);

    /**
     * @brief 维护标签聚合表 (tags 表)
     */
    void rebuildTagStats();

private:
    SyncEngine() = default;
    ~SyncEngine() = default;

    void scanDirectory(const std::filesystem::path& root, std::vector<std::wstring>& metaFiles);
};

} // namespace ArcMeta
