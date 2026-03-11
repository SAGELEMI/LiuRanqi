# 浏览器（SDL3 + CEF 离屏渲染示例）

一个基于 **SDL3 + OpenGL + CEF(OSR)** 的双进程浏览器示例：

## 功能概览

- CEF 离屏渲染（OSR）输出 BGRA 帧到共享内存
- SDL 用 OpenGL 纹理 作为浏览器的外壳
- SDL 输入事件通过命名管道转发给 CEF（鼠标/键盘/滚轮/文本/窗口尺寸/焦点）
- 中文输入法支持：CEF 输出候选框锚点，SDL 用 `SDL_SetTextInputArea` 同步
- 光标形态同步：CEF 光标类型映射到 SDL 系统光标
- 父进程退出时自动清理 CEF 子进程（JobObject）
- 输入地址导航到网页，可选择本地HTML文件
- 展示C++ 与 Js 之间的互动

## 目录结构

```text
.
├─ CEF/                # CEF 工程源码（离屏渲染、输入消费）
├─ SDL/                # SDL 工程源码（窗口、绘制、输入采集）
├─ common/             # 工程间协议头文件（共享内存/输入事件）
├─ cmake/              # 工程编译脚本
├─ demo/               # 测试资源
├─ Release/            # 可执行文件与运行时 DLL 输出目录
├─ lib/                # 静态库输出目录
├─ third_party/        # 调用模块
├─ main.cpp            # 程序入口
└─ CMakeLists.txt      # 顶层构建脚本
```

## 构建环境

- Windows x64
- CMake >= 3.20
- MSVC（Visual Studio 2022 或等效工具链）
- Ninja（可选，使用 `CMakeSettings.json` 时默认是 Ninja）
- SDL（默认路径：`third_party/SDL`）
- SDL_TTF（默认路径：`third_party/SDL_ttf`）
- CEF（默认路径：`third_party/CEF`）

> 顶层 CMake 已强制 `x64 + Release`，并统一输出到 `Release/`。

## 运行

```powershell
.\Release\LiuRanQi.exe
```

运行流程：
1. `LiuRanQi.exe` 创建窗口、共享内存、输入管道。
2. CEF 将网页帧写入共享内存，SDL 侧实时读取并显示。

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
