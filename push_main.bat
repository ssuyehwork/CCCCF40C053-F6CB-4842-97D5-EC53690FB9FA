@echo off
echo ============================================
echo   Inspirenote Git Auto Push (main branch)
echo ============================================

REM 切换到脚本所在目录
cd /d "%~dp0"

echo.
echo [1] 清理 Git 缓存（忽略 build/.qtcreator）
git rm -r --cached build 2>nul
git rm -r --cached .qtcreator 2>nul
git rm -r --cached build-* 2>nul

echo.
echo [2] 添加所有文件
git add .

echo.
echo [3] 提交更改
set /p msg=请输入提交信息（默认：update）： 
if "%msg%"=="" set msg=update
git commit -m "%msg%"

echo.
echo [4] 推送到 GitHub main 分支
git push -u origin main

echo.
echo ============================================
echo   推送完成！
echo ============================================
pause