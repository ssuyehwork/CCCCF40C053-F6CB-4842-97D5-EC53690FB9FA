# 极速灵感 (RapidNotes) 从零开始编译教程

欢迎使用 RapidNotes！这份教程将带你从零开始，在 Windows 环境下配置环境并生成自己的 `.exe` 文件。

## 第一步：下载并安装必要的工具

### 1. 安装 Qt 6
- **下载地址**：[Qt 官网下载页面](https://www.qt.io/download-open-source)
- **安装步骤**：
  - 运行安装程序并注册/登录 Qt 账号。
  - 在“选择组件”页面，勾选以下项：
    - `Qt 6.x` (建议选择最新稳定版，如 6.5 或 6.6)
    - `MinGW 11.2.0` (或更高版本)
    - `Qt Shader Tools`
    - `Qt SQL` 相关驱动
- **点击下一步直至安装完成**。

### 2. 安装 CMake (可选)
- Qt Creator 自带 CMake，但你也可以从 [CMake 官网](https://cmake.org/download/) 下载独立版。

---

## 第二步：打开并配置项目

1. **启动 Qt Creator**。
2. **打开项目**：
   - 点击 `文件 (File)` -> `打开文件或项目 (Open File or Project)`。
   - 导航到项目文件夹，选择 `CMakeLists.txt`。
3. **配置 Kit (构建套件)**：
   - 在弹出的配置界面，勾选你安装的 `Desktop Qt 6.x.x MinGW 64-bit`。
   - 点击 `Configure Project` 按钮。

---

## 第三步：编译与运行

1. **选择构建模式**：
   - 在界面左下角，点击小电脑图标，确保选择了 `Release` 模式（运行速度最快）。
2. **开始编译**：
   - 点击左下角的 **绿色锤子图标** (构建项目) 或直接按 `Ctrl + B`。
   - 等待下方的进度条变绿。
3. **运行程序**：
   - 点击左下角的 **绿色播放图标** (运行) 或按 `Ctrl + R`。
   - 此时，你的桌面应该会出现悬浮球和主界面！

---

## 第四步：如何找到生成的 .exe 文件

1. 默认情况下，编译出的文件位于项目文件夹旁边的 `build-RapidNotes-xxx-Release` 目录中。
2. 进入该目录下的 `bin` 或根目录，你会发现 `RapidNotes.exe`。
3. **注意**：如果直接双击 `.exe` 提示缺少 DLL，请使用 Qt 提供的 `windeployqt` 工具进行打包。

---

## 常见问题
- **编译报错找不到模块？** 确保在 Qt 安装时勾选了 `Sql`, `Network`, `Concurrent` 模块。
- **热键无效？** 某些电脑上 `Alt+Space` 可能被系统占用，可以在 `main.cpp` 中修改热键 ID。

祝你使用愉快！