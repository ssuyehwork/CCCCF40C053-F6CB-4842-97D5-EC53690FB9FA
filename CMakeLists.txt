cmake_minimum_required(VERSION 3.16)

project(ArcMeta VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 2026-03-xx 彻底废除 MinGW 支持，锁定 MSVC (Visual Studio 2022) 环境
if(NOT MSVC)
    message(FATAL_ERROR "🔴 [编译器拦截] 此项目已废除 MinGW 支持，必须且只能使用 Visual Studio 2022 (MSVC) 进行编译。")
endif()

# 2026-03-xx MSVC/VS2022 专属编译环境深度优化
add_compile_options(/utf-8)       # 解决中文字符集乱码
add_compile_options(/MP)          # 启用多处理器并行编译

# 屏蔽 VS 的非标准警告并优化 Windows 头文件包含
add_definitions(-D_CRT_SECURE_NO_WARNINGS)
add_definitions(-DNOMINMAX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# 2026-03-xx 协助解决 Qt6 路径未找到问题
if(NOT Qt6_DIR AND NOT CMAKE_PREFIX_PATH)
    message(STATUS "🔍 [环境检测] 正在尝试从默认路径定位 Qt6...")
    set(CMAKE_PREFIX_PATH "C:/Qt/6.10.2/msvc2022_64")
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Sql Network Concurrent Svg)

# 2026-03-20 深度治理日志输出：
add_definitions(-DQT_NO_DEBUG_OUTPUT)
add_definitions(-DQT_NO_INFO_OUTPUT)
add_definitions(-DQT_NO_WARNING_OUTPUT)

# 2026-03-xx 将生成的 exe 强制输出到项目源码根目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR})

include_directories(src)

# 源代码列表 - 2026-04-04 按照用户要求，执行“超级管理器”重构，彻底移除 RapidNotes 时代笔记相关逻辑
set(SOURCES
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
    src/core/HotkeyManager.cpp
    src/core/HotkeyManager.h
    src/core/HttpServer.cpp
    src/core/HttpServer.h
    src/core/KeyboardHook.cpp
    src/core/KeyboardHook.h
    src/core/ShortcutManager.cpp
    src/core/ShortcutManager.h
    src/main.cpp
    src/db/Database.cpp
    src/db/Database.h
    src/db/CategoryRepo.cpp
    src/db/CategoryRepo.h
    src/db/ItemRepo.cpp
    src/db/ItemRepo.h
    src/meta/AmMetaJson.cpp
    src/meta/AmMetaJson.h
    src/meta/BatchRenameEngine.cpp
    src/meta/BatchRenameEngine.h
    src/meta/MetadataManager.cpp
    src/meta/MetadataManager.h
    src/meta/SyncQueue.cpp
    src/meta/SyncQueue.h
    src/models/CategoryModel.cpp
    src/models/CategoryModel.h
    src/ui/AdvancedTagSelector.cpp
    src/ui/AdvancedTagSelector.h
    src/ui/ActivationDialog.cpp
    src/ui/ActivationDialog.h
    src/ui/CategoryDelegate.h
    src/ui/CategoryLockWidget.cpp
    src/ui/CategoryLockWidget.h
    src/ui/CategoryPasswordDialog.cpp
    src/ui/CategoryPasswordDialog.h
    src/ui/CleanListView.cpp
    src/ui/CleanListView.h
    src/ui/ClickableLineEdit.h
    src/ui/ContentPanel.cpp
    src/ui/ContentPanel.h
    src/ui/DropListView.cpp
    src/ui/DropListView.h
    src/ui/DropTreeView.cpp
    src/ui/DropTreeView.h
    src/ui/TreeItemDelegate.h
    src/ui/UiHelper.h
    src/ui/FileSearchHistoryPopup.h
    src/ui/FileSearchWidget.cpp
    src/ui/FileSearchWidget.h
    src/ui/FileSearchWindow.cpp
    src/ui/FileSearchWindow.h
    src/ui/FileStorageWindow.cpp
    src/ui/FileStorageWindow.h
    src/ui/FilterPanel.cpp
    src/ui/FilterPanel.h
    src/ui/FlowLayout.cpp
    src/ui/FlowLayout.h
    src/ui/FramelessDialog.cpp
    src/ui/FramelessDialog.h
    src/ui/HeaderBar.cpp
    src/ui/HeaderBar.h
    src/ui/HelpWindow.cpp
    src/ui/HelpWindow.h
    src/ui/IconHelper.h
    src/ui/IntegratedSearchWindow.cpp
    src/ui/IntegratedSearchWindow.h
    src/ui/KeywordSearchWidget.cpp
    src/ui/KeywordSearchWidget.h
    src/ui/KeywordSearchWindow.cpp
    src/ui/KeywordSearchWindow.h
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
    src/ui/MetadataPanel.cpp
    src/ui/MetadataPanel.h
    src/ui/PasswordVerifyDialog.cpp
    src/ui/PasswordVerifyDialog.h
    src/ui/QuickPreview.h
    src/ui/ResizeHandle.cpp
    src/ui/ResizeHandle.h
    src/ui/SearchLineEdit.cpp
    src/ui/SearchLineEdit.h
    src/ui/SecurityLockDialog.cpp
    src/ui/SecurityLockDialog.h
    src/ui/SettingsWindow.cpp
    src/ui/SettingsWindow.h
    src/ui/StringUtils.h
    src/ui/SvgIcons.h
    src/ui/TagCapsule.h
    src/ui/SystemTray.cpp
    src/ui/SystemTray.h
    src/ui/TagEditDialog.cpp
    src/ui/TagEditDialog.h
    src/ui/TagEditorWidget.cpp
    src/ui/TagEditorWidget.h
    src/ui/TitleEditorDialog.h
    src/ui/ToolTipOverlay.h
    src/ui/UnifiedSearchWindow.cpp
    src/ui/UnifiedSearchWindow.h
    resources/resources.qrc
    resources/app.manifest
)

if(WIN32)
    list(APPEND SOURCES resources/app_icon.rc)
endif()

add_executable(ArcMeta ${SOURCES})

target_link_libraries(ArcMeta PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Sql
    Qt6::Network
    Qt6::Concurrent
    Qt6::Svg
)

if(WIN32)
    target_link_libraries(ArcMeta PRIVATE user32 shell32 psapi dwmapi)
    set_target_properties(ArcMeta PROPERTIES
        WIN32_EXECUTABLE TRUE
    )

endif()
