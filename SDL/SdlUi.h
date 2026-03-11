#pragma once

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <cstdint>
#include <string>

// 顶部工具栏固定高度，浏览区需要扣掉这部分高度。
constexpr int kToolbarHeight = 84;

// 工具栏交互输出，避免 UI 层直接操作 CEF 或文件系统。
struct UiActions {
    bool open_html_clicked = false;                             // 点击“打开HTML”按钮。
    bool load_demo_clicked = false;                             // 点击“演示页”按钮。
    bool cpp_to_js_clicked = false;                             // 点击“C++调JS”按钮。
};

// 工具栏状态结构体，统一管理输入框和按钮的布局、交互和显示内容。
struct UiState {
    SDL_Rect input_rect{ 12, 10, 900, 28 };                    // 输入框区域。
    SDL_Rect cpp_to_js_rect{ 0, 0, 96, 24 };                   // “C++调JS”按钮区域。
    SDL_Rect demo_button_rect{ 0, 0, 84, 24 };                 // “演示页”按钮区域。
    SDL_Rect button_rect{ 0, 0, 96, 24 };                      // “打开HTML”按钮区域。
    bool input_focused = false;                                // 输入框是否处于编辑态。
    bool input_hovered = false;                                // 输入框是否处于悬停态。
    bool cpp_to_js_hovered = false;                            // “C++调JS”是否悬停。
    bool demo_button_hovered = false;                          // “演示页”是否悬停。
    bool button_hovered = false;                               // 按钮是否处于悬停态。
    bool cpp_to_js_pressed = false;                            // “C++调JS”是否按下。
    bool demo_button_pressed = false;                          // “演示页”是否按下。
    bool button_pressed = false;                               // 按钮是否处于按下态。
    std::string text = "https://www.baidu.com";                // 输入框当前文本。
    std::string cpp_to_js_text = "C++调JS";                    // 演示按钮文本。
    std::string demo_button_text = "演示页";                   // 演示页按钮文本。
    std::string button_text = "打开HTML";                      // 按钮固定文本。
    std::string status_text = "等待页面与按钮交互";             // SDL 顶部状态文本。
    bool caret_visible = true;                                 // 光标当前是否可见。
    uint64_t caret_last_toggle_ms = 0;                         // 上次切换光标可见性的时间戳。
    int input_font_px = 14;                                    // 输入框字号。
    int button_font_px = 13;                                   // 按钮默认字号。
};

// 工具栏字体资源结构体，避免每次绘制都重新打开字体文件。
struct UiFontSet {
    TTF_Font* input_font = nullptr;                            // 输入框字体对象。
    TTF_Font* button_font = nullptr;                           // 按钮字体对象。
};

// 初始化工具栏字体资源，当前固定使用系统中文字体。
bool InitializeUiFonts(UiFontSet& font_set);

// 释放工具栏字体资源。
void CleanupUiFonts(UiFontSet& font_set);

// 按窗口宽度更新输入框和按钮布局。
void UpdateToolbarLayout(UiState& ui_state, int window_width);

// 更新输入框光标闪烁状态。
void UpdateInputCaretBlink(UiState& ui_state, uint64_t now_ms);

// 处理工具栏鼠标和输入框文本事件。
void HandleUiEvent(const SDL_Event& event, UiState& ui_state, UiActions& actions);

// 绘制顶部工具栏、输入框、按钮、文字和闪烁光标。
void DrawToolbar(const UiState& ui_state, const UiFontSet& font_set, int window_width, int window_height);

// 把本地文件路径转换成 file:/// 形式的浏览器地址。
std::string BuildLocalFileNavigationTarget(const std::wstring& file_path);

// 把输入框文本转换成真正要导航的目标地址。
std::string BuildNavigationTarget(const std::string& raw_input);
