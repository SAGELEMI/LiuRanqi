# 浏览器（SDL3 + CEF 离屏渲染示例）

一个基于 **SDL3 + OpenGL + CEF(OSR)** 的双进程浏览器示例：
- `浏览器.exe` 负责窗口、渲染、输入采集（SDL3）
- `CEF.exe` 负责网页渲染（CEF 离屏渲染）
- 两个进程通过**共享内存 + 命名管道**通信

## 功能概览

- CEF 离屏渲染（OSR）输出 BGRA 帧到共享内存
- SDL 进程读取帧并用 OpenGL 纹理显示
- SDL 输入事件通过命名管道转发给 CEF（鼠标/键盘/滚轮/文本/窗口尺寸/焦点）
- 中文输入法支持：CEF 输出候选框锚点，SDL 用 `SDL_SetTextInputArea` 同步
- 光标形态同步：CEF 光标类型映射到 SDL 系统光标
- 父进程退出时自动清理 CEF 子进程（JobObject）

## 目录结构

```text
.
├─ CEF/                 # CEF 工程源码（离屏渲染、输入消费）
├─ SDL/               # SDL 工程源码（窗口、绘制、输入采集）
├─ common/              # 工程间协议头文件（共享内存/输入事件）
├─ 生成/                # 可执行文件与运行时 DLL 输出目录
├─ 静态库/              # 静态库输出目录
└─ CMakeLists.txt       # 顶层构建脚本
```

## 构建环境

- Windows x64
- CMake >= 3.20
- MSVC（Visual Studio 2022 或等效工具链）
- Ninja（可选，使用 `CMakeSettings.json` 时默认是 Ninja）
- SDL3（默认路径：`../SDL3`）SDL3-devel-3.4.2-VC
- CEF（默认路径：`../CEF`）cef_binary_145.0.26+g6ed7554+chromium-145.0.7632.110_windows64

> 顶层 CMake 已强制 `x64 + Release`，并统一输出到 `生成/`。

## 第三方目录要求

默认通过以下路径查找依赖（可通过 `-DSDL3_ROOT` / `-DCEF_ROOT` 覆盖）：

- `../SDL3`
  - `include/SDL3/...`
  - `cmake/SDL3Config.cmake`
  - `lib/x64/SDL3.lib`
- `../CEF`
  - `include/cef_app.h`
  - `cmake/FindCEF.cmake`
  - `Release/libcef.lib`
  - `out/build/x64-Release/libcef_dll_wrapper/libcef_dll_wrapper.lib`

## 构建步骤（命令行）

```powershell
cmake -S . -B out/build/x64-Release -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DSDL3_ROOT="..\SDL3" `
  -DCEF_ROOT="..\CEF"

cmake --build out/build/x64-Release --config Release
```

构建完成后，主要产物：
- `生成/浏览器.exe`
- `生成/CEF.exe`
- `生成/*.dll`（SDL3 + CEF 运行时由构建后步骤自动复制）

## 运行

```powershell
.\生成\浏览器.exe
```

运行流程：
1. `浏览器.exe` 创建窗口、共享内存、输入管道。
2. 启动 `CEF.exe`，并传入 `--shm-name` / `--input-pipe` / `--ime-shm-name` 等参数。
3. CEF 将网页帧写入共享内存，SDL 侧实时读取并显示。

默认首页：`https://www.baidu.com`

## 进程通信协议

### 1) 共享内存（帧）

定义文件：`common/ShmemProtocol.h`

- 头部结构：`SharedFrameHeader`
- 布局：`[SharedFrameHeader][BGRA 像素数据]`
- 新帧提交标记：`frame_id` 递增

### 2) 输入事件管道（SDL -> CEF）

定义文件：`common/InputProtocol.h`

- 固定包结构：`InputEventPacket`
- 事件类型：鼠标移动/按键/滚轮/文本/Resize/Focus
- 协议校验：`magic + version`

### 3) IME UI 状态（CEF -> SDL）

定义文件：`common/InputProtocol.h`

- 结构：`ImeUiState`
- 用于同步候选框锚点与光标类型

## CEF 调试参数（手动运行 `CEF.exe` 时）

- `--shm-name=<name>`：帧共享内存名称
- `--input-pipe=<name>`：输入命名管道名称
- `--ime-shm-name=<name>`：IME UI 共享内存名称
- `--width=<w>` / `--height=<h>`：初始视图大小
- `--url=<url>`：启动页面

## 常见问题

- 启动报错“未找到 CEF.exe”
  - 确认 `生成/CEF.exe` 存在，且从 `生成/浏览器.exe` 同目录运行。

- 窗口有画面但无法输入
  - 检查日志是否出现输入管道连接失败（`CefSdlInput_*`）。

- 中文输入法候选框位置不正确
  - 检查 `ImeUiState` 是否在更新，确认窗口缩放和纹理尺寸同步正常。
