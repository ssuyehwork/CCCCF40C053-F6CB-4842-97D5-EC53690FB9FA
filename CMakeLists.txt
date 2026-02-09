cmake_minimum_required(VERSION 3.16)

project(RapidNotes VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Sql Network Concurrent Svg)

# 源代码列表 - 确保 NoteEditWindow 相关的两个文件都在这里
set(SOURCES
    src/main.cpp
    src/core/DatabaseManager.cpp
    src/core/FileCryptoHelper.cpp
    src/core/AES.cpp
    src/core/HotkeyManager.cpp
    src/core/ClipboardMonitor.cpp
    src/core/KeyboardHook.cpp
    src/core/OCRManager.cpp
    src/models/NoteModel.cpp
    src/models/CategoryModel.cpp
    src/ui/FloatingBall.cpp
    src/ui/QuickWindow.cpp
    src/ui/DropTreeView.cpp
    src/ui/MainWindow.cpp
    src/ui/CleanListView.h
    src/ui/CleanListView.cpp
    src/ui/StringUtils.h
    src/ui/QuickPreview.h
    src/ui/HeaderBar.cpp
    src/ui/MetadataPanel.cpp
    src/ui/CategoryPasswordDialog.h
    src/ui/CategoryPasswordDialog.cpp
    src/ui/CategoryLockWidget.h
    src/ui/CategoryLockWidget.cpp
    src/ui/Toolbox.cpp
    src/ui/TimePasteWindow.cpp
    src/ui/PasswordGeneratorWindow.cpp
    src/ui/OCRWindow.cpp
    src/ui/OCRResultWindow.h
    src/ui/OCRResultWindow.cpp
    src/ui/KeywordSearchWindow.h
    src/ui/KeywordSearchWindow.cpp
    src/ui/FileStorageWindow.h
    src/ui/FileStorageWindow.cpp
    src/ui/TagManagerWindow.cpp
    src/ui/FileSearchWindow.h
    src/ui/FileSearchWindow.cpp
    src/ui/ColorPickerWindow.h
    src/ui/ColorPickerWindow.cpp
    src/ui/FireworksOverlay.h
    src/ui/FireworksOverlay.cpp
    src/ui/SystemTray.cpp
    src/ui/SettingsWindow.h
    src/ui/SettingsWindow.cpp
    src/ui/Editor.cpp
    src/ui/FlowLayout.cpp
    src/ui/FlowLayout.h
    src/ui/AdvancedTagSelector.cpp
    src/ui/AdvancedTagSelector.h
    src/ui/ClickableLineEdit.h
    src/ui/TagEditorWidget.h
    src/ui/TagEditorWidget.cpp
    src/ui/TagEditDialog.h
    src/ui/TagEditDialog.cpp
    src/ui/FramelessDialog.h
    src/ui/FramelessDialog.cpp
    src/ui/FilterPanel.cpp
    src/ui/FilterPanel.h
    src/ui/NoteDelegate.h
    src/ui/QuickNoteDelegate.h
    src/ui/NoteEditWindow.h    # <--- 必须有
    src/ui/NoteEditWindow.cpp  # <--- 必须有
    src/ui/ScreenshotTool.h
    src/ui/ScreenshotTool.cpp
    src/ui/SearchLineEdit.h
    src/ui/SearchLineEdit.cpp
    src/ui/WritingAnimation.h
    src/ui/WritingAnimation.cpp
    src/ui/HelpWindow.h
    src/ui/HelpWindow.cpp
    resources/resources.qrc
)

if(WIN32)
    list(APPEND SOURCES resources/app_icon.rc)
endif()

add_executable(RapidNotes ${SOURCES})

target_link_libraries(RapidNotes PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Sql
    Qt6::Network
    Qt6::Concurrent
    Qt6::Svg
)

if(WIN32)
    target_link_libraries(RapidNotes PRIVATE user32 shell32 psapi dwmapi)
    set_target_properties(RapidNotes PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
endif()