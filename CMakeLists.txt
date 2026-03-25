cmake_minimum_required(VERSION 3.16)

project(RapidNotes VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 2026-03-xx 锁定 MSVC 环境
if(NOT MSVC)
    message(FATAL_ERROR "🔴 此项目必须使用 Visual Studio 2022 (MSVC) 进行编译。")
endif()

add_compile_options(/utf-8)
add_compile_options(/MP)
add_definitions(-D_CRT_SECURE_NO_WARNINGS)
add_definitions(-DNOMINMAX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(NOT Qt6_DIR AND NOT CMAKE_PREFIX_PATH)
    set(CMAKE_PREFIX_PATH "C:/Qt/6.10.2/msvc2022_64")
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Sql Network Concurrent Svg)

# 禁用日志输出
add_definitions(-DQT_NO_DEBUG_OUTPUT)
add_definitions(-DQT_NO_INFO_OUTPUT)
add_definitions(-DQT_NO_WARNING_OUTPUT)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR})

include_directories(src)

# 1. 核心公共组件 (基于当前物理存在的文件)
set(CORE_SOURCES
    src/core/AES.cpp
    src/core/AES.h
    src/core/ActionRecorder.cpp
    src/core/ActionRecorder.h
    src/core/DatabaseManager.cpp
    src/core/DatabaseManager.h
    src/core/FileCryptoHelper.cpp
    src/core/FileCryptoHelper.h
    src/core/FileStorageHelper.cpp
    src/core/FileStorageHelper.h
    src/core/OCRManager.cpp
    src/core/OCRManager.h
    src/core/ShortcutManager.cpp
    src/core/ShortcutManager.h
    src/core/HardwareInfoHelper.cpp
    src/core/HardwareInfoHelper.h
    src/models/CategoryModel.cpp
    src/models/CategoryModel.h
    src/models/NoteModel.cpp
    src/models/NoteModel.h
    src/ui/IconHelper.h
    src/ui/StringUtils.h
    src/ui/SvgIcons.h
    src/ui/ToolTipOverlay.h
    resources/resources.qrc
    resources/app.manifest
)

# 2. 采集端现存组件
set(NOTES_SOURCES
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
    src/core/MessageCaptureHandler.cpp
    src/core/MessageCaptureHandler.h
    src/ui/CategoryLockWidget.cpp
    src/ui/CategoryLockWidget.h
    src/ui/FileSearchHistoryPopup.h
    src/ui/FireworksOverlay.cpp
    src/ui/FireworksOverlay.h
)

# 3. 管理端现存组件 (MainWindow 闭环)
set(MANAGER_SOURCES
    src/ui/AdvancedTagSelector.cpp
    src/ui/AdvancedTagSelector.h
    src/ui/CategoryDelegate.h
    src/ui/CategoryPasswordDialog.cpp
    src/ui/CategoryPasswordDialog.h
    src/ui/CleanListView.cpp
    src/ui/CleanListView.h
    src/ui/ClickableLineEdit.h
    src/ui/DropTreeView.cpp
    src/ui/DropTreeView.h
    src/ui/Editor.cpp
    src/ui/Editor.h
    src/ui/FilterPanel.cpp
    src/ui/FilterPanel.h
    src/ui/FlowLayout.cpp
    src/ui/FlowLayout.h
    src/ui/FramelessDialog.cpp
    src/ui/FramelessDialog.h
    src/ui/HeaderBar.cpp
    src/ui/HeaderBar.h
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
    src/ui/MetadataPanel.cpp
    src/ui/MetadataPanel.h
    src/ui/NoteDelegate.h
    src/ui/PasswordVerifyDialog.cpp
    src/ui/PasswordVerifyDialog.h
    src/ui/QuickPreview.h
    src/ui/SearchLineEdit.cpp
    src/ui/SearchLineEdit.h
    src/ui/SettingsWindow.cpp
    src/ui/SettingsWindow.h
    src/ui/TagCapsule.h
    src/ui/TitleEditorDialog.h
)

if(WIN32)
    set(RC_FILE resources/app_icon.rc)
endif()

# --- 目标 1：RapidNotes ---
add_executable(RapidNotes ${CORE_SOURCES} ${NOTES_SOURCES} src/main.cpp ${RC_FILE})
target_compile_definitions(RapidNotes PRIVATE RAPID_NOTES_TARGET)
target_link_libraries(RapidNotes PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Sql Qt6::Network Qt6::Concurrent Qt6::Svg)
if(WIN32)
    target_link_libraries(RapidNotes PRIVATE user32 shell32 psapi dwmapi windowsapp)
    set_target_properties(RapidNotes PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

# --- 目标 2：RapidManager ---
add_executable(RapidManager ${CORE_SOURCES} ${MANAGER_SOURCES} src/main.cpp ${RC_FILE})
target_compile_definitions(RapidManager PRIVATE RAPID_MANAGER_TARGET)
target_link_libraries(RapidManager PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Sql Qt6::Network Qt6::Concurrent Qt6::Svg)
if(WIN32)
    target_link_libraries(RapidManager PRIVATE user32 shell32 psapi dwmapi windowsapp)
    set_target_properties(RapidManager PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
