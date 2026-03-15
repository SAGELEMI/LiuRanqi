#pragma once
#include <cstdint>
//共享内存协议
// 帧数据标识号：用于快速识别共享内存内容是否为本项目协议
static constexpr uint32_t kFrameMagic = 0x4345464D; // "CEFM"
// 协议版本：后续升级协议时可做兼容判断
static constexpr uint32_t kFrameVersion = 1;

// 限制上限：防止写入异常尺寸导致越界
static constexpr uint32_t kMaxWidth = 3840;
static constexpr uint32_t kMaxHeight = 2160;
static constexpr uint32_t kMaxPixelBytes = kMaxWidth * kMaxHeight * 4;

// ProcessMessage 消息名（只传控制消息，不传大像素）
static constexpr char kMsgEvalJs[] = "app.eval_js";
static constexpr char kMsgJsResult[] = "app.js_result";
static constexpr char kMsgJsInvokeNative[] = "app.js_invoke_native";
static constexpr char kMsgNativeResult[] = "app.native_result";

// JS 桥接演示状态共享内存（CEF -> SDL）。
static constexpr uint32_t kBridgeDemoMagic = 0x42444745; // "BDGE"
static constexpr uint32_t kBridgeDemoVersion = 1;
static constexpr size_t kBridgeDemoTextBytes = 512;

// 共享内存布局：
// [SharedFrameHeader][pixel BGRA...]
struct SharedFrameHeader {
    uint32_t magic = kFrameMagic;      // 协议标识号
    uint32_t version = kFrameVersion;  // 协议版本
    uint32_t width = 0;                // 当前帧宽
    uint32_t height = 0;               // 当前帧高
    uint32_t stride = 0;               // 每行字节，通常 width * 4
    uint64_t frame_id = 0;             // 帧号：每次写入新帧后递增
    uint32_t pixel_bytes = 0;          // 像素总字节：stride * height
    uint32_t reserved = 0;             // 预留字段（将来扩展）
};

// CEF 把最近一次 JS 桥接演示状态写到这里，SDL 每帧读取并展示。
struct BridgeDemoState {
    uint32_t magic = kBridgeDemoMagic;                           // 协议标识号
    uint32_t version = kBridgeDemoVersion;                      // 协议版本
    uint64_t seq = 0;                                           // 每次更新递增
    char status_text[kBridgeDemoTextBytes] = { 0 };             // UTF-8 状态文本
};
