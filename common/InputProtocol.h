#pragma once
#include <cstdint>
// 输入事件协议
// 输入管道数据的魔数，用于快速校验包合法性
static constexpr uint32_t kInputMagic = 0x494E5054; // "INPT"
static constexpr uint32_t kInputVersion = 1;

// IME UI 共享状态（CEF -> SDL）
static constexpr uint32_t kImeUiMagic = 0x494D4555; // "IMEU"
static constexpr uint32_t kImeUiVersion = 1;

enum class InputEventType : uint32_t {
    MouseMove = 1,   // 鼠标移动
    MouseButton = 2, // 鼠标按下/抬起
    MouseWheel = 3,  // 鼠标滚轮
    Key = 4,         // 键盘按下/抬起
    Text = 5,        // 文本输入（字符）
    Resize = 6,      // 视图尺寸变化
    Focus = 7        // 焦点变化
};

enum class MouseButtonType : uint32_t {
    Left = 1,
    Middle = 2,
    Right = 3
};

// 固定长度事件包，便于 ReadFile/WriteFile 一次读写
struct InputEventPacket {
    uint32_t magic = kInputMagic;       // 魔数
    uint32_t version = kInputVersion;   // 协议版本
    uint32_t type = 0;                  // InputEventType
    uint32_t reserved0 = 0;

    uint64_t seq = 0;                   // 递增序号（用于排查丢包）

    int32_t x = 0;                      // 鼠标 x
    int32_t y = 0;                      // 鼠标 y
    int32_t delta_x = 0;                // 滚轮 x
    int32_t delta_y = 0;                // 滚轮 y

    uint32_t modifiers = 0;             // CEF EVENTFLAG_* 组合
    uint32_t mouse_button = 0;          // MouseButtonType
    uint32_t mouse_up = 0;              // 1=抬起, 0=按下
    uint32_t click_count = 1;           // 点击次数

    uint32_t key_code = 0;              // windows_key_code
    uint32_t native_key_code = 0;       // native key code（可先等于 key_code）
    uint32_t key_up = 0;                // 1=KEYUP, 0=KEYDOWN
    uint32_t reserved1 = 0;

    uint32_t width = 0;                 // Resize 宽
    uint32_t height = 0;                // Resize 高

    char text[64] = { 0 };                // Text 事件 UTF-8
};

// CEF 把输入法候选锚点写入该结构；SDL 读取后调用 SDL_SetTextInputArea。
struct ImeUiState {
    uint32_t magic = kImeUiMagic;
    uint32_t version = kImeUiVersion;
    uint32_t visible = 0;      // 1: 显示候选框锚点有效；0: 清除
    uint32_t reserved0 = 0;

    uint64_t seq = 0;          // 每次更新递增，SDL 按 seq 检测变化

    int32_t x = 0;             // 文本区域 x（窗口坐标）
    int32_t y = 0;             // 文本区域 y（窗口坐标）
    int32_t w = 0;             // 文本区域宽
    int32_t h = 0;             // 文本区域高
    int32_t cursor_x = 0;      // 光标 x（窗口坐标）
    int32_t cursor_y = 0;      // 光标 y（窗口坐标）
    int32_t reserved1 = 0;
    int32_t reserved2 = 0;

    // 鼠标光标形态（CEF -> SDL）
    uint32_t cursor_type = 0;  // 对应 cef_cursor_type_t
    uint32_t reserved3 = 0;
    uint64_t cursor_seq = 0;   // 光标变化递增
};
