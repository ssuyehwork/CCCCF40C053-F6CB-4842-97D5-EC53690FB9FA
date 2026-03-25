# 文件管理器核心模块开发需求

## 重要声明
- 本项目只在 Windows 系统环境下运行，不考虑任何跨平台兼容性
- 编译器只使用 MSVC 2022，不使用 MinGW，不使用 GCC，不使用 Clang
- 所有代码均面向 MSVC 编译器编写，可自由使用 MSVC 专有特性
- 不需要任何跨平台宏、条件编译、平台抽象层

---

## 技术栈
- 语言：C++17
- IDE：Qt Creator 19.0.0 (Community)
- 编译器：MSVC 2022（唯一编译器，通过 Qt Creator 工具链配置）
- 构建系统：CMake（使用 CMakeLists.txt，Qt Creator 原生支持）
- 平台：Windows only（不考虑 Linux、macOS 任何其他平台）
- 数据库：SQLite（使用 SQLiteCpp 封装库）
- 并行：std::execution::par（C++17 并行算法，MSVC 下开箱即用，
  不需要额外链接 TBB）
- UI 层：暂不实现，保留接口供后续接入

## CMakeLists.txt 要求
- cmake_minimum_required(VERSION 3.20)
- set(CMAKE_CXX_STANDARD 17)
- 链接库：ntdll、ole32
- 编译选项：/W4 /O2（MSVC 专用，不使用 -Wall 等 GCC 风格选项）
- 可执行文件需通过 .manifest 文件声明
  requestedExecutionLevel: requireAdministrator
  以确保 MFT 读取所需的管理员权限

---

## 代码组织结构

src/
├── mft/
│   ├── MftReader.h / .cpp       // MFT 读取，构建 FileIndex
│   ├── UsnWatcher.h / .cpp      // USN Journal 实时监听
│   └── PathBuilder.h / .cpp     // FRN → 完整路径重建
├── meta/
│   ├── AmMetaJson.h / .cpp      // .am_meta.json 读写（含安全写入）
│   └── SyncQueue.h / .cpp       // 懒更新队列
├── db/
│   ├── Database.h / .cpp        // SQLite 连接与 schema 初始化
│   ├── FolderRepo.h / .cpp      // folders 表的 CRUD
│   ├── ItemRepo.h / .cpp        // items 表的 CRUD
│   └── SyncEngine.h / .cpp      // 增量同步 + 全量扫描逻辑
└── main.cpp

---

## 模块一：MFT 文件索引

### 数据结构
struct FileEntry {
    DWORDLONG frn;         // File Reference Number
    DWORDLONG parentFrn;   // 父目录 FRN
    std::wstring name;     // 文件名（宽字符）
    DWORD attributes;      // 文件属性
    bool isDir() const { return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0; }
};

using FileIndex = std::unordered_map<DWORDLONG, FileEntry>;

### MFT 读取（MftReader）
- 使用 CreateFileW 打开卷句柄，路径格式如 \\.\C:
- 需要管理员权限，无权限时降级为递归目录遍历
- 使用 DeviceIoControl + FSCTL_ENUM_USN_DATA 批量枚举 MFT 记录
- 缓冲区大小：64KB，循环读取直至枚举完毕
- index.reserve(1,000,000) 预分配，避免频繁 rehash
- 每条 USN_RECORD_V2 提取以下字段：
  FileReferenceNumber、ParentFileReferenceNumber、
  FileName、FileAttributes
- 所有字符串使用宽字符 std::wstring

### 路径重建（PathBuilder）
- 通过 parentFrn 递归向上查找，拼接完整路径
- 根目录判断条件：(parentFrn & 0x0000FFFFFFFFFFFF) == 5
- 最大递归深度 64 层，防止循环引用导致死循环

### 并行搜索
- 将 FileIndex 中所有 FileEntry 指针收集到 std::vector
- 使用 std::execution::par + std::for_each 并行过滤
- 支持大小写不敏感匹配
- 使用 std::mutex 保护搜索结果的收集过程

### USN Journal 实时监听（UsnWatcher，独立后台线程）
- 启动时使用 FSCTL_QUERY_USN_JOURNAL 获取当前 Journal 状态
- 使用 FSCTL_READ_USN_JOURNAL 持续监听变更事件
- 监听以下四种事件并执行对应操作：

  USN_REASON_FILE_CREATE
  → 插入新 FileEntry 到 FileIndex

  USN_REASON_FILE_DELETE
  → 从 FileIndex 删除对应 FRN
  → 从数据库 items 表删除对应 path 记录
  → 从数据库 folders 表删除对应 path 记录
  → 级联删除 items 表中 parent_path 等于该路径的所有记录

  USN_REASON_RENAME_OLD_NAME
  → 从 FileIndex 删除旧 FRN
  → 从数据库删除旧路径的所有相关记录（同上级联清理）
  → 旧路径的标签、星级、颜色等元数据随旧路径一起丢弃，不迁移
  → 路径即身份，路径变了就是新的对象

  USN_REASON_RENAME_NEW_NAME
  → 插入新 FileEntry 到 FileIndex
  → 不自动创建新的 .am_meta.json，等用户下次操作时再生成

- 轮询间隔：200ms
- 使用 std::mutex 保护 FileIndex 的读写

---

## 模块二：.am_meta.json 文件格式

每个文件夹下按需创建 .am_meta.json，记录该文件夹及其内部
文件和子文件夹的元数据。.am_meta.json 文件本身不计入
items 列表。

### 完整 JSON 结构
{
  "version": "1",
  "folder": {
    "sort_by": "name",
    "sort_order": "asc",
    "rating": 0,
    "color": "",
    "tags": [],
    "pinned": false
  },
  "items": {
    "文件名或子文件夹名": {
      "type": "file | folder",
      "rating": 0,
      "color": "",
      "tags": [],
      "pinned": false
    }
  }
}

### 字段说明
- folder：描述当前文件夹自身的元数据与视图配置
- folder.sort_by 可选值：
  name | size | ctime | mtime | type | rating | color | custom
- folder.sort_order 可选值：asc | desc
- color 字段：颜色标记字符串，例如 "red"、"cyan"、"blue"，
  空字符串表示无标记
- tags 字段：直接存储标签文字的字符串数组，不存 ID，
  例如 ["工作", "待整理", "重要"]
- items 只记录有过用户操作的条目，即至少满足以下之一：
  rating > 0，或 color 非空，或 tags 非空，或 pinned 为 true
  无任何操作的文件不写入 items

### 安全写入流程（AmMetaJson，必须严格遵守）
1. 将新内容序列化后写入 .am_meta.json.tmp 临时文件
2. 尝试重新解析 .tmp 文件，验证是合法完整的 JSON
3. 验证成功：
   调用 rename 将 .tmp 原子替换为 .am_meta.json
4. 验证失败：
   删除 .tmp 文件
   记录错误日志
   保留原 .am_meta.json 不做任何修改
   向调用方返回写入失败的错误码

---

## 模块三：数据库 Schema（SQLite）

数据库仅作为全局搜索和聚合查询的索引。
.am_meta.json 是主数据源，数据库是从。
数据库损坏时可通过全量扫描从 JSON 完整重建。

### 建表语句

-- 文件夹元数据表
CREATE TABLE IF NOT EXISTS folders (
    path        TEXT PRIMARY KEY,
    rating      INTEGER DEFAULT 0,
    color       TEXT    DEFAULT '',
    tags        TEXT    DEFAULT '',  -- JSON 数组字符串
    pinned      INTEGER DEFAULT 0,
    sort_by     TEXT    DEFAULT 'name',
    sort_order  TEXT    DEFAULT 'asc',
    last_sync   REAL                 -- 对应 .am_meta.json 的 mtime
);

-- 文件与子文件夹元数据表
CREATE TABLE IF NOT EXISTS items (
    path        TEXT PRIMARY KEY,
    type        TEXT,                -- 'file' | 'folder'
    rating      INTEGER DEFAULT 0,
    color       TEXT    DEFAULT '',
    tags        TEXT    DEFAULT '',  -- JSON 数组字符串
    pinned      INTEGER DEFAULT 0,
    parent_path TEXT
);

-- 标签聚合索引表
CREATE TABLE IF NOT EXISTS tags (
    tag         TEXT PRIMARY KEY,
    item_count  INTEGER DEFAULT 0
);

-- 同步状态表
CREATE TABLE IF NOT EXISTS sync_state (
    key         TEXT PRIMARY KEY,
    value       TEXT
);
-- 存储键值对，例如：
-- key = 'last_sync_time'，value = 上次程序正常关闭时的时间戳

### 索引
CREATE INDEX IF NOT EXISTS idx_items_parent ON items(parent_path);
CREATE INDEX IF NOT EXISTS idx_items_rating ON items(rating);
CREATE INDEX IF NOT EXISTS idx_items_color  ON items(color);
CREATE INDEX IF NOT EXISTS idx_items_tags   ON items(tags);

---

## 模块四：同步机制

### 懒更新队列（SyncQueue）
- 用户操作触发 JSON 安全写入成功后，将该文件夹路径加入队列
- 防抖合并：同一路径的多次变更合并为一条，只同步最终状态
- 后台线程空闲时批量从队列取出，解析 JSON 后写入数据库
- SQLite 所有写操作使用事务批量提交，不逐条提交
- 程序正常关闭前必须刷空队列，确保数据库与 JSON 一致

### 增量同步（SyncEngine，程序启动时自动执行）
- 从 sync_state 表读取 last_sync_time
- 遍历所有已知文件夹路径，获取 .am_meta.json 的 mtime
- 只对 mtime > last_sync_time 的文件执行解析和数据库更新
- 全部完成后将 sync_state 中 last_sync_time 更新为当前时间戳

### 全量扫描（SyncEngine，用户手动触发）
- 忽略 last_sync_time
- 递归遍历所有可访问路径，收集全部 .am_meta.json 文件
- 清空并重建 folders、items、tags 三张表
- 提供进度回调接口：
  std::function<void(int current, int total)>
  供调用方显示进度

### 标签聚合表维护
- tags 表不做实时维护
- 在每次增量同步完成后和全量扫描完成后重新聚合计数
- 聚合逻辑：从 items 表和 folders 表的 tags 字段解析所有
  标签字符串，统计每个标签出现的条目数，全量写入 tags 表

### 路径失效清理（由 UsnWatcher 触发）
收到 FILE_DELETE 或 RENAME_OLD_NAME 事件时执行：
1. 删除 folders 表中 path = 目标路径 的记录
2. 删除 items 表中 path = 目标路径 的记录
3. 删除 items 表中 parent_path = 目标路径 的所有子记录
4. 触发一次标签聚合表重新计数

---

## 分阶段实现顺序

### 第一阶段（优先实现）
- .am_meta.json 的完整安全读写逻辑
- 懒更新队列
- 数据库 schema 初始化

### 第二阶段
- 增量同步（启动时自动执行）
- 全量扫描（用户手动触发，带进度回调）
- 程序关闭时刷空懒更新队列

### 第三阶段
- MFT 读取（先做接口抽象 IFileIndexer 隔离实现细节）
- USN Journal 实时监听
- 路径失效级联清理

---

## 注意事项
- 只在 Windows 系统环境下运行，不做任何跨平台处理
- 只使用 MSVC 2022 编译，不使用 MinGW，不使用任何
  GCC / Clang 风格的编译选项或特性
- 所有文件路径统一使用宽字符 std::wstring
- .am_meta.json 的路径操作使用宽字符，文件内容本身为 UTF-8
- MFT 读取和 USN 监听依赖管理员权限，程序必须通过
  .manifest 文件声明 requireAdministrator
- 各模块之间通过接口解耦，不直接互相引用实现文件