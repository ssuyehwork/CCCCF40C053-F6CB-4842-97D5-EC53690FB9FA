cmake_minimum_required(VERSION 3.16)

project(RapidManager VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT MSVC)
    message(FATAL_ERROR "🔴 [编译器拦截] 此项目必须且只能使用 Visual Studio 2022 (MSVC) 进行编译。")
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

# 禁用所有调试日志
add_definitions(-DQT_NO_DEBUG_OUTPUT)
add_definitions(-DQT_NO_INFO_OUTPUT)
add_definitions(-DQT_NO_WARNING_OUTPUT)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR})

include_directories(src)

# 极致剥离后的源文件列表 (物理剔除监听、加密、验证代码)
set(SOURCES
    src/manager_main.cpp
    src/core/DatabaseManager.cpp
    src/core/ActionRecorder.cpp
    src/models/CategoryModel.cpp
    src/models/NoteModel.cpp
    src/ui/MainWindow.cpp
    src/ui/HeaderBar.cpp
    src/ui/Editor.cpp
    src/ui/MetadataPanel.cpp
    src/ui/FilterPanel.cpp
    src/ui/CategoryLockWidget.cpp
    src/ui/CleanListView.cpp
    src/ui/DropTreeView.cpp
    src/ui/FramelessDialog.cpp
    src/ui/CategoryPasswordDialog.cpp
    src/ui/PasswordVerifyDialog.cpp
    src/ui/SearchLineEdit.cpp
    src/ui/FireworksOverlay.cpp
    src/ui/FlowLayout.cpp
    src/ui/StringUtils.cpp
    src/ui/QuickPreview.cpp
    resources/resources.qrc
    resources/app.manifest
)

# 补充缺失的源文件（根据磁盘探测结果）
# 注意：之前探测显示有些文件是只有 .h 没有 .cpp，
# 但 MainWindow 等核心组件必须有 .cpp 才能链接成功。
# 经查 src/ui/ 目录下确实存在 MainWindow.cpp 等实现文件。

if(WIN32)
    list(APPEND SOURCES resources/app_icon.rc)
endif()

# 生成唯一独立目标: RapidManager
add_executable(RapidManager ${SOURCES})

# 强制管理端宏定义，用于 MainWindow 内部的 UI 裁剪
target_compile_definitions(RapidManager PRIVATE RAPID_MANAGER_TARGET)

target_link_libraries(RapidManager PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Sql Qt6::Network Qt6::Concurrent Qt6::Svg
    user32 shell32 psapi dwmapi windowsapp
)

if(WIN32)
    set_target_properties(RapidManager PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
