cmake_minimum_required(VERSION 3.16)

project(RapidNotes VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Sql Network Concurrent Svg)

include_directories(src)

# 源代码列表 - 确保 NoteEditWindow 相关的两个文件都在这里
set(SOURCES
    src/core/AES.cpp
    src/core/AES.h
    src/core/ClipboardMonitor.cpp
    src/core/ClipboardMonitor.h
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
    src/core/KeyboardHook.cpp
    src/core/KeyboardHook.h
    src/core/MessageCaptureHandler.cpp
    src/core/MessageCaptureHandler.h
    src/core/OCRManager.cpp
    src/core/OCRManager.h
    src/core/ReminderService.cpp
    src/core/ReminderService.h
    src/core/ShortcutManager.cpp
    src/core/ShortcutManager.h
    src/main.cpp
    src/models/CategoryModel.cpp
    src/models/CategoryModel.h
    src/models/NoteModel.cpp
    src/models/NoteModel.h
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
    src/ui/ColorPickerWindow.cpp
    src/ui/ColorPickerWindow.h
    src/ui/DropTreeView.cpp
    src/ui/DropTreeView.h
    src/ui/Editor.cpp
    src/ui/Editor.h
    src/ui/FileSearchWidget.cpp
    src/ui/FileSearchWidget.h
    src/ui/FilterPanel.cpp
    src/ui/FilterPanel.h
    src/ui/FireworksOverlay.cpp
    src/ui/FireworksOverlay.h
    src/ui/FloatingBall.cpp
    src/ui/FloatingBall.h
    src/ui/FlowLayout.cpp
    src/ui/FlowLayout.h
    src/ui/FramelessDialog.cpp
    src/ui/FramelessDialog.h
    src/ui/HeaderBar.cpp
    src/ui/HeaderBar.h
    src/ui/HelpWindow.cpp
    src/ui/HelpWindow.h
    src/ui/IconHelper.h
    src/ui/KeywordSearchWidget.cpp
    src/ui/KeywordSearchWidget.h
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
    src/ui/MetadataPanel.cpp
    src/ui/MetadataPanel.h
    src/ui/NoteDelegate.h
    src/ui/NoteEditWindow.cpp
    src/ui/NoteEditWindow.h
    src/ui/OCRResultWindow.cpp
    src/ui/OCRResultWindow.h
    src/ui/OCRWindow.cpp
    src/ui/OCRWindow.h
    src/ui/PasswordGeneratorWindow.cpp
    src/ui/PasswordGeneratorWindow.h
    src/ui/PixelRulerOverlay.cpp
    src/ui/PixelRulerOverlay.h
    src/ui/QuickNoteDelegate.h
    src/ui/QuickPreview.h
    src/ui/QuickWindow.cpp
    src/ui/QuickWindow.h
    src/ui/ResizeHandle.cpp
    src/ui/ResizeHandle.h
    src/ui/ScreenshotTool.cpp
    src/ui/ScreenshotTool.h
    src/ui/SearchAppWindow.cpp
    src/ui/SearchAppWindow.h
    src/ui/SearchLineEdit.cpp
    src/ui/SearchLineEdit.h
    src/ui/SettingsWindow.cpp
    src/ui/SettingsWindow.h
    src/ui/StringUtils.h
    src/ui/SvgIcons.h
    src/ui/SystemTray.cpp
    src/ui/SystemTray.h
    src/ui/TagEditDialog.cpp
    src/ui/TitleEditorDialog.h
    src/ui/TagEditDialog.h
    src/ui/TagEditorWidget.cpp
    src/ui/TagEditorWidget.h
    src/ui/TagManagerWindow.cpp
    src/ui/TagManagerWindow.h
    src/ui/TimePasteWindow.cpp
    src/ui/TimePasteWindow.h
    src/ui/TodoCalendarWindow.cpp
    src/ui/TodoCalendarWindow.h
    src/ui/ToolTipOverlay.h
    src/ui/Toolbox.cpp
    src/ui/Toolbox.h
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