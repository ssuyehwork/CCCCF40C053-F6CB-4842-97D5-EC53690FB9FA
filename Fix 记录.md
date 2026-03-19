The 'Character Count' advanced filter must strictly exclude specific items to ensure data purity: items with item_type = 'image', titles starting with '[截图]', locked items (is_locked = 1), and favorite items (is_favorite = 1).

For performance optimization in UI lists (e.g., TodoCalendarWindow), cache the associated task date in Qt::UserRole + 1 of the QListWidgetItem to avoid expensive database queries when the item is selected or clicked.

l

In sidebar implementations (e.g., QuickWindow), use a 'silentTypes' whitelist (e.g., All, Today, Trash) to explicitly suppress context menus on system-preset categories, preventing 'protected category' alerts during right-click interactions.

Header File Policy (AGENTS.md): Every modification must ensure inclusion of all necessary headers and synchronize .cpp/.h files. Every submission must include a '同步自查报告' checklist verifying signals, slots, functions, and the presence of Q_OBJECT macro.

When reusing QListWidgetItem objects in high-frequency list updates (like 24h task lists), explicitly clear the associated task ID (e.g., item->setData(Qt::UserRole, 0)) to prevent interaction logic from targeting stale data during subsequent double-clicks.

TODO task status coloring follows strict hex codes: Completed (#2ecc71 Green), In-Progress/Overdue (#f39c12 Yellow-Orange), and Default/New tasks (#ffffff White).

Code Maintenance Standards: 1. Always add dated Chinese comments explaining modification intent (e.g., '// 2026-03-xx ...'). 2. Strictly avoid magic numbers; extract to constants. 3. Prioritize defensive programming (early returns over deep nested if-else).

Native Qt tooltips (QToolTip, setToolTip) are strictly forbidden. All components must use ToolTipOverlay.h for consistency. Native

JULES AI C++/Qt Operational Protocol: Strictly Chinese only. Responses to tasks must start with the handshake: '我已加载 C++/Qt 终极宪法，只说中文，绝不越权乱改，已开启强制握手模式等待您的指令。' Follow a 3-phase state machine: 1. Depth discussion (consensus on plan/pseudo-code); 2. System Lock (wait for '批准修改'); 3. Execution. Exemption: Direct modification is only allowed for fixing compilation errors introduced by the agent's previous edit. No AI pleasantries allowed.

UI Styling and Layout Redlines (AGENTS.md): 1. Interface buttons must follow right-to-left order: Close -> Maximize -> Minimize -> Stay on Top -> Edit. 2. Unauthorized QSS modifications to padding/margin are forbidden. 3. Global hotkeys must use project-encapsulated hotkey utilities, never direct native hooks.

Code Detection Heuristics: Logic in NoteModel.cpp identifies content as code using structural markers (e.g., '{' and '}' containing ':"' or newlines for JSON) and common keywords (e.g., 'import', 'class', 'public', 'package') rather than single-character triggers to minimize false positives.

Visual Consistency: Link (hyperlink) icons across the application (Note list, context menus) are unified to use the green color #17B345.

Database Validation Performance: High-frequency capture methods (e.g., addNote) call getTrialStatus(false) to utilize cached authorization states instead of triggering synchronous hardware指纹 or disk checks.

Multi-threaded UI Safety: Database logic that requires UI interaction (e.g., getTrialStatus conflict dialogs) must use QMetaObject::invokeMethod to ensure the dialog is executed on the main GUI thread, preventing crashes during background asynchronous operations.

Shutdown Logic: During application exit, the 'Exiting' ToolTip/notification is designed to remain visible until the process terminates completely to provide continuous visual feedback during the database packing/sync period.

Database Optimization: Synchronous saveKernelToShell() calls in high-frequency paths (addNote, updateTodo, addTodo, deleteTodo) are replaced with markDirty(). Persistence is now handled by an idle-triggered backup mechanism (30s idle or 10 incrementals) to prevent UI blocking.

The program contains a hardcoded hardware serial number check (SN: '494000PAOD9L') in src/core/DatabaseManager.cpp (around line 2225); if hardware info does not match and no activation code is found, the program triggers exit(-5).

UI windows with variable content (e.g., SettingsWindow with multiple tabs) must avoid hardcoded fixed sizes (setFixedSize). Use adjustSize() or dynamic layout logic during content transitions to ensure all controls are visible without clipping or occlusion.

To eliminate flickering during movement of frameless windows in Windows environments, implement nativeEvent() to handle WM_NCHITTEST and return HTCAPTION for draggable regions (e.g., title bar), allowing the OS to manage the dragging kernel.

In ClipboardMonitor, the clipboardChanged signal must be emitted only after all internal state checks and content hash deduplication to prevent redundant visual feedback (fireworks/tips) from programmatic or duplicate clipboard updates.

C++20 Lambda Capture Rule: Implicit capture of 'this' via [=] is deprecated. Always use [this] or [=, this] explicitly when a lambda needs to access class members or methods to ensure compiler compatibility and avoid warnings.

The project is a C++/Qt 6 application named RapidNotes, primarily targeting Windows as evidenced by WinAPI and DWM-specific optimizations, using C++20 standards.

Toolbar and sidebar functional buttons (e.g., btnSidebar in QuickWindow, m_btnFilter and m_btnMeta in HeaderBar) must use a consistent gray background (rgba(255, 255, 255, 0.1)) for their checked/active state instead of high-contrast blue (#3A90FF or #4a90e2) to ensure a unified UI style.

To prevent 'Use-After-Free' crashes, all raw pointer deletions (especially m_activeShape in ScreenshotTool) must be immediately followed by setting the pointer to nullptr, and critical paths like paintEvent must include null-pointer checks before dereferencing.

UI-related signals (such as noteAdded and noteUpdated) in DatabaseManager must be emitted from the main thread to prevent random crashes; use QMetaObject::invokeMethod with Qt::QueuedConnection if the emission is triggered from a worker thread.

Recursive SQL queries (CTEs) in DatabaseManager for category operations must include a hard depth limit (e.g., depth < 50) to prevent stack overflow crashes caused by potential circular references in corrupted or manipulated database files.

In DatabaseManager, critical file persistence operations (like saveKernelToShell and backupDatabaseLatest) must use an atomic 'write-to-temp-then-rename' strategy (e.g., writing to a .tmp file and verifying integrity before replacing the production file) to prevent data corruption during power failures or crashes.

In ClipboardMonitor, clipboard images exceeding a specific threshold (e.g., 20MB) must be downsampled (e.g., to 2560px) before PNG encoding to prevent UI thread blocking and OOM crashes.

The full-screen mosaic in ScreenshotTool should be lazily initialized only when the mosaic tool is actually requested and use a lower sampling rate (e.g., 1/40) to save VRAM and system memory.

In OCRManager::preprocessImage, scaling must be capped (e.g., max 4000px) to prevent massive memory allocations during grayscale/sharpening operations on high-resolution screenshots.

In NoteModel.cpp, caches for thumbnails (m_thumbnailCache), tooltips (m_tooltipCache), and plain text (m_plainContentCache) must have hard size limits (100 for images, 500 for text) to prevent unbounded memory growth during scrolling; caches should be cleared when limits are exceeded.

Asynchronous database operations involving large binary data (like addNoteAsync for screenshots) must include flow control or throttling (e.g., checking QThreadPool::globalInstance()->activeThreadCount()) to prevent memory exhaustion from a backlog of large data blobs in the task queue.

To prevent memory exhaustion (OOM) and crashes in resource-heavy UI overlays (like Screen Picker, Pixel Ruler, Screenshot Tool), implement singleton protection (using setObjectName and findChild) and explicitly clear large data buffers (e.g., m_screenPixmap = QPixmap();) immediately in confirm or cancel handlers to ensure resource release doesn't wait for asynchronous object destruction.

When loading images from files for color extraction or previews, use QImageReader with setScaledSize (e.g., max 2560px) to perform streaming downscaling, preventing OOM crashes when users open extremely large (4K/8K+) image files.

To optimize UI performance and memory, always scale a QImage to its display size before converting it to a QPixmap. Avoid converting a large raw QImage to a QPixmap and then scaling the QPixmap, as this consumes excessive VRAM/RAM.

Every assistant response must include at least one valid tool call, regardless of whether the user requested a specific action or information.

The 'trash' SVG icon must consistently use the standard project red color (#e74c3c) across all application scenarios, including UI components (menus, buttons, search bars) and model-layer definitions (e.g., system categories like the Recycle Bin in CategoryModel), to provide a uniform visual warning for destructive operations.

Context menu (QMenu) selection style must use gray (#3E3E42) background for 'item:selected' instead of blue (#4a90e2) to prevent visual overlap with blue-toned icons and improve identification.

Focus indicator lines (1px green #2ecc71, e.g., m_listFocusLine) must only be visible when the sidebar is expanded; logic in updateFocusLines() must be coupled with sidebar visibility toggles in both QuickWindow and MainWindow.

SVG icons are managed in SvgIcons.h using an inline QMap. Large static data structures like this must be wrapped in #ifndef Q_MOC_RUN to prevent MOC parsing errors or namespace misidentification (e.g., SvgIcons::MainWindow).

User preferences for UI effects (e.g., fireworks) must be implemented as toggleable options in the System Settings (SettingsWindow) rather than being permanently enabled or disabled.

The screen color picker functionality is required to automatically copy the selected HEX color code to the system clipboard upon successful extraction.

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