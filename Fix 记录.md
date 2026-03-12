头文件检查规范：每次修改代码时必须检查并添加完整的必要头文件（如 QBuffer, QByteArray, IconHelper.h 等），严禁遗漏。

计数显示逻辑：只有当复制/存在的物理路径数量大于 1 时，才在列表右侧显示计数值。单个文件或单个压缩包 (ZIP) 即使包含多个内部文件，也视为单个对象，不显示计数。

视觉定义规则：单文件 (file) 对应的图标颜色统一为黄色 #f1c40f；多文件 (files) 对应的图标颜色及计数值统一为红色 #FF4858。

CategoryDelegate 负责在侧边栏置顶分类的名称右上角绘制红色圆点标识；CategoryModel 增加了 PinnedRole 以支持置顶状态的数据交互。

支持通过 CleanListView 进行笔记拖拽排序，分类通过 CategoryModel::syncOrders 实现拖拽排序逻辑。后端统一由 DatabaseManager 提供排序持久化接口。

在 C++ 实现文件中定义类成员函数时，对于嵌套在类中的类型（如枚举），必须使用完全限定名（如 DatabaseManager::MoveDirection）。

// ===================|===================

维护记录：修复了 TodoCalendarWindow.h 中 AlarmEditDialog 类 eventFilter 重复声明导致的编译错误，确保该函数仅在 protected 区域声明一次。

QuickPreview UI 规范：标题栏按钮顺序为从右往左：关闭 -> 最大化 -> 最小化 -> 置顶 -> 编辑。右侧元数据面板最大宽度限制为 230px，右下角需设置 border-bottom-right-radius: 7px 以修复圆角溢出。

NoteEditWindow (Ctrl+B) 交互规范：彻底移除“插入待办事项”和“预览切换”按钮。窗口加载后必须通过 togglePreview(false) 强制进入编辑状态，并自动聚焦至编辑器内容区，禁止保留多余的预览或跳转步骤。

交互指令：1. 用户规则优先级最高。2. 仅限中文交流。3. 禁止固定内置流程。4. 每次回答后确认共识。5. 获得“批准修改”后方可修改代码。6. 修改头文件时务必添加完整内容，避免遗漏。

UI 规范：严禁使用原生系统 ToolTip。所有交互组件（如 QuickWindow 按钮、QuickPreview 搜索框及按钮、KeywordSearchWidget 等）必须通过 installEventFilter 拦截 QEvent::ToolTip 事件，并调用 ToolTipOverlay::instance()->showText 重定向渲染。

头文件安全原则：在修改头文件声明（如添加 eventFilter）时，必须检查并移除任何重复的函数声明，以防编译冲突。

UI 规范：杜绝在 ToolTipOverlay 及 UI 文本中使用 Emoji 符号，必须使用文本前缀标识状态。标准前缀为：[OK]（成功/正常）、[ERR]（错误/失败）、[INFO]（信息）、[!]（警告）、[OCR]（识别结果）。

输入框导航规范：1. QLineEdit 拦截 Up 键跳转至文首 (0)，Down 键跳转至文尾 (length)。2. QTextEdit/QPlainTextEdit 实现智能跳转：Up 键在第一行时跳至文首，Down 键在末行时跳至文尾。3. 排除快捷键录制框（如 HotkeyEdit/ShortcutEdit）。

ToolTip 格式规范：快捷键提示必须遵循“功能名称 （快捷键）”格式，必须使用全圆角括号，且符号间需添加空格（例如：显示/隐藏侧边栏 （Alt + Q））。

QuickPreview 架构优化：showPreview 接口改为接收 QVariantMap 笔记快照，避免切换预览时触发冗余的数据库查询，显著提升列表导航性能。

QuickWindow 侧边栏切换（qw_sidebar）默认快捷键为 Alt+Q。ShortcutManager::load() 包含强制迁移逻辑，会自动将用户本地配置中的 Ctrl+Q 升级为 Alt+Q，以解决旧版本冲突。

“自动归档”功能绑定至右键菜单“归类到此分类”指定的 ID（extensionTargetCategoryId）。开启状态显绿色（#00A650）及 switch_on 图标，反馈信息为“[OK] 自动归档已开启 （分类名称）”；关闭状态显灰色（#aaaaaa）及 switch_off 图标。反馈必须使用 ToolTipOverlay 且杜绝 Emoji。

Qt 坐标获取兼容性：在 QDropEvent 等事件处理中，应优先使用 event->pos() 结合 mapToGlobal() 或 mapFromGlobal()。避免直接调用 globalPosition() 或 position()，以确保在不同 Qt5/Qt6 环境下的编译兼容性。

QuickWindow 拖拽架构：子组件（CleanListView, DropTreeView）必须 ignore 外部拖拽事件以允许冒泡至 QuickWindow 处理；QuickWindow::dropEvent 支持通过坐标检测智能识别侧边栏落点分类并自动归类。

模拟系统按键必须优先使用 SendInput 接口并包含硬件扫描码（wScan），且需显式处理修饰键的弹起状态，以确保兼容性并防止冲突。

在事件过滤器中处理按键时，应显式提取 key() 和 modifiers() 到局部变量，并确保逻辑分支中引用正确的变量名以避免未定义错误。

数据库维护：DatabaseManager::createTables 包含迁移逻辑，确保 notes 和 categories 表具备 updated_at 等关键字段；回收站 UNION ALL 查询要求 20 个字段严格对齐，且必须使用命名绑定参数（如 :kw）以防止跨驱动兼容性导致的 'Unable to fetch row' 错误。

分页规范：MainWindow 和 QuickWindow 统一引用 DatabaseManager::DEFAULT_PAGE_SIZE（常量值为 100）作为列表分页条数。

环境同步指令：使用 git fetch origin main && git reset --hard origin/main 更新本地工作区状态。

MainWindow 的 eventFilter 中处理 Shift 焦点切换时，必须确保“侧边栏至列表”与“列表至侧边栏”的逻辑分支独立且平级，严禁嵌套，以保证双向切换可靠性。

DatabaseManager 提供了 extensionTargetCategoryIdChanged(int id) 信号，用于在采集目标分类变更时通知 UI（如 QuickWindow 和 Toolbox）同步更新 ToolTip 状态。

QuickWindow 通过 m_monitorTimer 定时器获取 GetForegroundWindow() 存入 m_lastActiveHwnd，在激活数据前先恢复目标窗口焦点，再模拟 Ctrl+V 粘贴。

ClipboardMonitor 具备进程级避让黑名单功能，通过 m_blacklistCache 实现过滤，配置项位于 RapidNotes/Security/avoidanceBlacklist。

技术栈：C++20, Qt6 (Core, Gui, Widgets, Sql, Network, Concurrent, Svg), CMake, SQLite (AES 加密)。

QuickWindow 列表中的 1-9 数字索引显示及相关激活逻辑已彻底移除。QuickNoteDelegate.h 不再进行索引数字绘制。

系统架构：桌面客户端与浏览器插件（CopyWithSource）通过本地 23333 端口的 HTTP 服务联动。

QuickWindow 和 MainWindow 实现了 1 像素绿色（#2ecc71）的焦点指示线，仅在焦点位于侧边栏或笔记列表时显示，提供实时视觉反馈。

“静默采集模式”开启后，OCR 或剪贴板捕获将不弹出预览窗口，仅通过 ToolTipOverlay 反馈状态。

QuickWindow 列表聚焦且无修饰键时，数字键映射为：'1' -> Home, '2' -> End, '3' -> 向上方向键, '4' -> 向下方向键。

代码修改原则：除非明确要求，否则禁止修改现有核心逻辑；移除代码逻辑前必须考虑上下文，避免破坏其他关联功能。

SettingsWindow.cpp 在调用 ClipboardMonitor::instance() 前必须包含头文件 #include "../core/ClipboardMonitor.h"。

在 QuickWindow 和 MainWindow 中，使用 Shift 键实现侧边栏分类树与笔记列表之间的双向焦点切换，切换后目标区域需自动选中首选项或恢复当前选中项。

项目名称为 RapidNotes，是基于 C++20 和 Qt6 开发的本地生产力工具，定位为专业级本地效率枢纽，具备 Windows 钩子、AES 加密数据库等核心特征。

全局“纯净粘贴”功能的热键 ID 固定为 7（默认 Ctrl+Shift+V）。