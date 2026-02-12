@echo off
setlocal

REM === 你的 Qt 安装路径（请确认）===
set QT_DIR=C:\Qt\6.10.1\mingw_64

REM === 你的 EXE 路径 ===
set EXE_PATH="G:\C++\Inspirenote\Inspirenote_release\build\Desktop_Qt_6_10_1_MinGW_64_bit-Release\RapidNotes.exe"

echo Deploying Qt6 runtime...
"%QT_DIR%\bin\windeployqt.exe" %EXE_PATH%

echo.
echo Deployment complete!
pause
