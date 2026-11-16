# WorkDesk

桌面端多 AI 聚合客户端。原生 Qt 实现，非网页套壳 —— 一个窗口统管多个 AI 服务、配置档案与对话风格，文件图片直接拖进来就能发。

Windows 10/11 x64 · Qt 6 Widgets · 解压即用

![主界面](docs/screenshots/window.png)

## 功能特性

- **多 AI 聚合**：多个 AI 服务并存，侧边栏一键切换，随时追加新服务。
- **原生 Qt 实现**：Qt 6 Widgets 重写，无 Electron、无内嵌浏览器，启动快、内存占用低。
- **配置档案管理**：每个服务独立保存端点、模型与参数，配置写入 `%APPDATA%\WorkDesk\config.json`。
- **性格调试**：为每个档案单独设定系统提示词与对话风格，切换服务即切换人格。
- **文件与图片直传**：拖拽或粘贴文件、截图直接发送，单文件上限 100MB。
- **流畅的流式体验**：增量解析流式响应，Markdown 渲染与代码高亮随收随绘。

## 下载

官方下载页：<https://workdesk-qt.pages.dev>

当前版本 v1.1.0（2026-11-15）· Windows 10/11 x64 · 解压即用，运行库已附带。

## 使用

1. 解压到**不含中文与空格**的目录。
2. 双击 `WorkDeskQt.exe`。
3. 首次运行在设置中填入各 AI 服务的接口地址、模型名与 API Key。

> API Key 仅保存在本机 `%APPDATA%\WorkDesk\config.json`，不随程序分发、不上传任何服务器。

## 从源码构建

环境要求：Windows 10/11 x64、Qt 6.8+（msvc2022_64）、CMake 3.21+、Ninja、Visual Studio（含 C++ 桌面开发）。

```powershell
# 指定 Qt 目录编译；加 -DeployDir 会自动 windeployqt 到目标目录
powershell -ExecutionPolicy Bypass -File .\build.ps1 -QtDir C:/Qt/6.12.0/msvc2022_64 -DeployDir D:\WorkDeskQt
```

手动构建：

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.12.0/msvc2022_64
cmake --build build
```

`nlohmann/json` 已内置于 `third_party/`，无需额外下载。若需引入其它第三方头文件目录，追加 `-DWD_EXTRA_INCLUDE=<路径>`。

## 目录结构

```
src/                C++ 源码（主窗口、聊天面板、消息气泡、Markdown、语法高亮、HTTP、配置、会话、便签、设置）
assets/             图标资源
third_party/        nlohmann/json 单头文件
docs/               官网静态页与界面截图
CMakeLists.txt      构建定义
build.ps1           一键编译与部署脚本
```

## 更新日志

### v1.1.0 — 2026-11-15

- 新增多 AI 配置管理（profiles），可并存任意数量的服务档案。
- 新增性格调试选项，为每个档案单独设定系统提示词。
- 新增文件与图片路径传递，单文件上限 100MB。
- 新增粘贴文件与截图发送。

## 许可证

[MIT](LICENSE)
