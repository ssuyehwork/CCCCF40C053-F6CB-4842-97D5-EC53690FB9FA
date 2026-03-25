cmake_minimum_required(VERSION 3.16)

project(RapidNotes VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 2026-03-xx 按照用户要求：彻底废除 MinGW 支持，锁定 MSVC (Visual Studio 2022) 环境
if(NOT MSVC)
    message(FATAL_ERROR "🔴 [编译器拦截] 此项目已废除 MinGW 支持，必须且只能使用 Visual Studio 2022 (MSVC) 进行编译。")
endif()

# 2026-03-xx 按照用户要求：MSVC/VS2022 专属编译环境深度优化
add_compile_options(/utf-8)       # 解决中文字符集乱码
add_compile_options(/MP)          # 启用多处理器并行编译，榨干 CPU 性能
# [FIX] 2026-03-xx 移除 /permissive- 以兼容 Windows SDK/WinRT 的内部重定义冲突

# 屏蔽 VS 的非标准警告并优化 Windows 头文件包含
add_definitions(-D_CRT_SECURE_NO_WARNINGS)
# [FIX] 2026-03-xx 移除 WIN32_LEAN_AND_MEAN 以确保 COM/WinRT 所需的底层头文件完整加载
add_definitions(-DNOMINMAX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# 2026-03-xx 按照用户要求：增加环境自检提示，协助解决 Qt6 路径未找到问题
if(NOT Qt6_DIR AND NOT CMAKE_PREFIX_PATH)
    message(STATUS "🔍 [环境检测] 正在尝试从默认路径定位 Qt6...")
    set(CMAKE_PREFIX_PATH "C:/Qt/6.10.2/msvc2022_64")
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Sql Network Concurrent Svg)

# 2026-03-20 深度治理日志输出：
# 1. 禁用调试日志 (qDebug)
add_definitions(-DQT_NO_DEBUG_OUTPUT)
# 2. 禁用普通信息日志 (qInfo)
add_definitions(-DQT_NO_INFO_OUTPUT)
# 3. 禁用警告日志 (qWarning) 
add_definitions(-DQT_NO_WARNING_OUTPUT)

# 2026-03-xx 按照用户要求：将生成的 exe 强制输出到项目源码根目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR})

include_directories(src)

# 通用核心源文件 (MainWindow 与 RapidNotes 共享)
set(COMMON_SOURCES
    src/core/AES.cpp
    src/core/AES.h
    src/core/DatabaseManager.cpp
    src/core/DatabaseManager.h
    src/core/FileCryptoHelper.cpp
    src/core/FileCryptoHelper.h
    src/core/FileStorageHelper.cpp
    src/core/FileStorageHelper.h
    src/core/HardwareInfoHelper.cpp
    src/core/HardwareInfoHelper.h
    src/core/OCRManager.cpp
    src/core/OCRManager.h
    src/core/ShortcutManager.cpp
    src/core/ShortcutManager.h
    src/core/ActionRecorder.cpp
    src/core/ActionRecorder.h
    src/models/CategoryModel.cpp
    src/models/CategoryModel.h
    src/models/NoteModel.cpp
    src/models/NoteModel.h
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
    src/ui/HeaderBar.cpp
    src/ui/HeaderBar.h
    src/ui/Editor.cpp
    src/ui/Editor.h
    src/ui/MetadataPanel.cpp
    src/ui/MetadataPanel.h
    src/ui/FilterPanel.cpp
    src/ui/FilterPanel.h
    src/ui/CategoryLockWidget.cpp
    src/ui/CategoryLockWidget.h
    src/ui/CleanListView.cpp
    src/ui/CleanListView.h
    src/ui/DropTreeView.cpp
    src/ui/DropTreeView.h
    src/ui/FramelessDialog.cpp
    src/ui/FramelessDialog.h
    src/ui/CategoryPasswordDialog.cpp
    src/ui/CategoryPasswordDialog.h
    src/ui/PasswordVerifyDialog.cpp
    src/ui/PasswordVerifyDialog.h
    src/ui/SearchLineEdit.cpp
    src/ui/SearchLineEdit.h
    src/ui/FireworksOverlay.cpp
    src/ui/FireworksOverlay.h
    src/ui/FlowLayout.cpp
    src/ui/FlowLayout.h
    src/ui/StringUtils.h
    src/ui/IconHelper.h
    src/ui/SvgIcons.h
    src/ui/ToolTipOverlay.h
    src/ui/CategoryDelegate.h
    src/ui/NoteDelegate.h
    src/ui/TitleEditorDialog.h
    src/ui/QuickPreview.h
    resources/resources.qrc
    resources/app.manifest
)

# 笔记端专属源文件
set(NOTES_SOURCES
    src/main.cpp
    src/core/ClipboardMonitor.cpp
    src/core/ClipboardMonitor.h
    src/core/HotkeyManager.cpp
    src/core/HotkeyManager.h
    src/core/HttpServer.cpp
    src/core/HttpServer.h
    src/core/KeyboardHook.cpp
    src/core/KeyboardHook.h
    src/core/ReminderService.cpp
    src/core/ReminderService.h
    src/ui/QuickWindow.cpp
    src/ui/QuickWindow.h
    src/ui/SystemTray.cpp
    src/ui/SystemTray.h
    src/ui/Toolbox.cpp
    src/ui/Toolbox.h
    src/ui/SettingsWindow.cpp
    src/ui/SettingsWindow.h
    src/ui/OCRWindow.cpp
    src/ui/OCRWindow.h
    src/ui/OCRResultWindow.cpp
    src/ui/OCRResultWindow.h
    src/ui/ScreenshotTool.cpp
    src/ui/ScreenshotTool.h
    src/ui/ActivationDialog.cpp
    src/ui/ActivationDialog.h
    src/ui/TodoCalendarWindow.cpp
    src/ui/TodoCalendarWindow.h
    # ... 其他次要 UI 组件
    src/ui/AdvancedTagSelector.cpp
    src/ui/AdvancedTagSelector.h
    src/ui/ColorPickerWindow.cpp
    src/ui/ColorPickerWindow.h
    src/ui/FileSearchWidget.cpp
    src/ui/FileSearchWidget.h
    src/ui/FileSearchWindow.cpp
    src/ui/FileSearchWindow.h
    src/ui/FileStorageWindow.cpp
    src/ui/FileStorageWindow.h
    src/ui/HelpWindow.cpp
    src/ui/HelpWindow.h
    src/ui/IntegratedSearchWindow.cpp
    src/ui/IntegratedSearchWindow.h
    src/ui/KeywordSearchWidget.cpp
    src/ui/KeywordSearchWidget.h
    src/ui/KeywordSearchWindow.cpp
    src/ui/KeywordSearchWindow.h
    src/ui/NoteEditWindow.cpp
    src/ui/NoteEditWindow.h
    src/ui/PasswordGeneratorWindow.cpp
    src/ui/PasswordGeneratorWindow.h
    src/ui/PixelRulerOverlay.cpp
    src/ui/PixelRulerOverlay.h
    src/ui/ResizeHandle.cpp
    src/ui/ResizeHandle.h
    src/ui/SearchAppWindow.cpp
    src/ui/SearchAppWindow.h
    src/ui/SecurityLockDialog.cpp
    src/ui/SecurityLockDialog.h
    src/ui/TagEditDialog.cpp
    src/ui/TagEditDialog.h
    src/ui/TagEditorWidget.cpp
    src/ui/TagEditorWidget.h
    src/ui/TagManagerWindow.cpp
    src/ui/TagManagerWindow.h
    src/ui/TimePasteWindow.cpp
    src/ui/TimePasteWindow.h
    src/ui/UnifiedSearchWindow.cpp
    src/ui/UnifiedSearchWindow.h
)

# 管理端专属源文件
set(MANAGER_SOURCES
    src/manager_main.cpp
)

if(WIN32)
    set(RC_FILE resources/app_icon.rc)
endif()

# 目标 1: 原版 RapidNotes
add_executable(RapidNotes ${COMMON_SOURCES} ${NOTES_SOURCES} ${RC_FILE})
target_compile_definitions(RapidNotes PRIVATE RAPID_NOTES_TARGET)
target_link_libraries(RapidNotes PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Sql Qt6::Network Qt6::Concurrent Qt6::Svg
    user32 shell32 psapi dwmapi windowsapp
)

# 目标 2: 独立管理端 RapidManager
add_executable(RapidManager ${COMMON_SOURCES} ${MANAGER_SOURCES} ${RC_FILE})
target_compile_definitions(RapidManager PRIVATE RAPID_MANAGER_TARGET)
target_link_libraries(RapidManager PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Sql Qt6::Network Qt6::Concurrent Qt6::Svg
    user32 shell32 psapi dwmapi windowsapp
)

if(WIN32)
    set_target_properties(RapidNotes RapidManager PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
