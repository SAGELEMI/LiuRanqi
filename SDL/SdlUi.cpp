#include "SdlUi.h"

#include <SDL3/SDL_opengl.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <string>
#include <windows.h>

// 输入框左侧文字内边距，文字和光标都基于这个偏移计算。
constexpr int kInputTextPaddingX = 8;

// 输入框顶部文字内边距，用于让文字和边框留出呼吸空间。
constexpr int kInputTextPaddingY = 6;

// 按钮顶部文字内边距，用于把按钮文案放到视觉中心附近。
constexpr int kButtonTextPaddingY = 4;

// 输入框最小宽度，窗口很窄时也保留基础可用空间。
constexpr int kMinimumInputWidth = 120;

// 工具栏统一字体路径，当前直接使用系统自带中文字体。
constexpr const char* kUiFontPath = "C:/Windows/Fonts/msyh.ttc";

// 把像素 x 坐标转换成 OpenGL NDC 坐标。
static float PixelToNdcX(int pixel_x, int viewport_width) {
    return (2.0f * static_cast<float>(pixel_x) / static_cast<float>(viewport_width)) - 1.0f;
}

// 把像素 y 坐标转换成 OpenGL NDC 坐标。
static float PixelToNdcY(int pixel_y, int viewport_height) {
    return 1.0f - (2.0f * static_cast<float>(pixel_y) / static_cast<float>(viewport_height));
}

// 绘制纯色矩形。
static void DrawFilledRect(int x, int y, int width, int height, int viewport_width, int viewport_height, float r, float g, float b, float a) {
    const int right = x + width;
    const int bottom = y + height;
    glColor4f(r, g, b, a);
    glBegin(GL_QUADS);
    glVertex2f(PixelToNdcX(x, viewport_width), PixelToNdcY(y, viewport_height));
    glVertex2f(PixelToNdcX(right, viewport_width), PixelToNdcY(y, viewport_height));
    glVertex2f(PixelToNdcX(right, viewport_width), PixelToNdcY(bottom, viewport_height));
    glVertex2f(PixelToNdcX(x, viewport_width), PixelToNdcY(bottom, viewport_height));
    glEnd();
}

// 绘制矩形边框。
static void DrawRectOutline(int x, int y, int width, int height, int viewport_width, int viewport_height, float r, float g, float b, float a) {
    const int right = x + width;
    const int bottom = y + height;
    glColor4f(r, g, b, a);
    glBegin(GL_LINE_LOOP);
    glVertex2f(PixelToNdcX(x, viewport_width), PixelToNdcY(y, viewport_height));
    glVertex2f(PixelToNdcX(right, viewport_width), PixelToNdcY(y, viewport_height));
    glVertex2f(PixelToNdcX(right, viewport_width), PixelToNdcY(bottom, viewport_height));
    glVertex2f(PixelToNdcX(x, viewport_width), PixelToNdcY(bottom, viewport_height));
    glEnd();
}

// 判断坐标点是否落在指定矩形内。
static bool IsPointInsideRect(int x, int y, const SDL_Rect& rect) {
    return x >= rect.x && y >= rect.y && x < (rect.x + rect.w) && y < (rect.y + rect.h);
}

// 把宽字符串转成 UTF-8 字符串，供 CEF LoadURL 使用。
static std::string WideToUtf8String(const std::wstring& wide_text) {
    if (wide_text.empty()) {
        return "";
    }
    const int utf8_length = WideCharToMultiByte(CP_UTF8, 0, wide_text.c_str(), static_cast<int>(wide_text.size()), nullptr, 0, nullptr, nullptr);
    if (utf8_length <= 0) {
        return "";
    }
    std::string utf8_text(static_cast<size_t>(utf8_length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide_text.c_str(), static_cast<int>(wide_text.size()), utf8_text.data(), utf8_length, nullptr, nullptr);
    return utf8_text;
}

// 判断当前输入是否更像本地 HTML 文件路径。
static bool LooksLikeLocalHtmlPath(const std::string& raw_input) {
    if (raw_input.find("://") != std::string::npos) {
        return false;
    }
    const std::filesystem::path input_path = std::filesystem::u8path(raw_input);
    const std::wstring extension = input_path.extension().wstring();
    return extension == L".html" || extension == L".htm";
}

// 根据字体和字符串计算文字宽度，供按钮居中和光标定位使用。
static int MeasureTextWidth(const std::string& text, TTF_Font* font) {
    if (!font || text.empty()) {
        return 0;
    }
    int text_width = 0;
    int text_height = 0;
    if (!TTF_GetStringSize(font, text.c_str(), text.size(), &text_width, &text_height)) {
        return 0;
    }
    return text_width;
}

// 把 SDL_Surface 转成临时 OpenGL 纹理。
static GLuint CreateTextureFromSurface(SDL_Surface* surface) {
    if (!surface) {
        return 0;
    }
    SDL_Surface* rgba_surface = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
    if (!rgba_surface) {
        return 0;
    }
    GLuint texture_id = 0;
    glGenTextures(1, &texture_id);
    glBindTexture(GL_TEXTURE_2D, texture_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, rgba_surface->w, rgba_surface->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba_surface->pixels);
    SDL_DestroySurface(rgba_surface);
    return texture_id;
}

// 使用临时纹理把文字贴到指定矩形内。
static void DrawTexturedQuad(GLuint texture_id, int x, int y, int width, int height, int viewport_width, int viewport_height) {
    const int right = x + width;
    const int bottom = y + height;
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texture_id);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f);
    glVertex2f(PixelToNdcX(x, viewport_width), PixelToNdcY(y, viewport_height));
    glTexCoord2f(1.0f, 0.0f);
    glVertex2f(PixelToNdcX(right, viewport_width), PixelToNdcY(y, viewport_height));
    glTexCoord2f(1.0f, 1.0f);
    glVertex2f(PixelToNdcX(right, viewport_width), PixelToNdcY(bottom, viewport_height));
    glTexCoord2f(0.0f, 1.0f);
    glVertex2f(PixelToNdcX(x, viewport_width), PixelToNdcY(bottom, viewport_height));
    glEnd();
}

// 按当前按钮状态决定显示字号。
static int GetButtonFontPixelSize(bool hovered, bool pressed) {
    if (pressed) {
        return 12;
    }
    if (hovered) {
        return 13;
    }
    return 14;
}

// 绘制单段 UTF-8 文字。
static void DrawUtf8Text(const std::string& text, TTF_Font* font, int x, int y, SDL_Color color, int viewport_width, int viewport_height) {
    if (!font || text.empty()) {
        return;
    }
    SDL_Surface* text_surface = TTF_RenderText_Blended(font, text.c_str(), text.size(), color);
    if (!text_surface) {
        return;
    }
    GLuint texture_id = CreateTextureFromSurface(text_surface);
    if (texture_id != 0) {
        DrawTexturedQuad(texture_id, x, y, text_surface->w, text_surface->h, viewport_width, viewport_height);
        glDeleteTextures(1, &texture_id);
    }
    SDL_DestroySurface(text_surface);
}

// 绘制输入框背景和边框。
static void DrawInputFieldBackground(const UiState& ui_state, int viewport_width, int viewport_height) {
    const float base = ui_state.input_focused ? 0.20f : (ui_state.input_hovered ? 0.17f : 0.14f);
    DrawFilledRect(ui_state.input_rect.x, ui_state.input_rect.y, ui_state.input_rect.w, ui_state.input_rect.h, viewport_width, viewport_height, base, base + 0.02f, base + 0.08f, 0.96f);
    if (ui_state.input_focused) {
        DrawRectOutline(ui_state.input_rect.x, ui_state.input_rect.y, ui_state.input_rect.w, ui_state.input_rect.h, viewport_width, viewport_height, 0.25f, 0.75f, 0.95f, 1.0f);
        return;
    }
    if (ui_state.input_hovered) {
        DrawRectOutline(ui_state.input_rect.x, ui_state.input_rect.y, ui_state.input_rect.w, ui_state.input_rect.h, viewport_width, viewport_height, 0.60f, 0.62f, 0.68f, 1.0f);
        return;
    }
    DrawRectOutline(ui_state.input_rect.x, ui_state.input_rect.y, ui_state.input_rect.w, ui_state.input_rect.h, viewport_width, viewport_height, 0.35f, 0.35f, 0.40f, 1.0f);
}

// 绘制按钮背景和边框。
static void DrawButtonBackground(const SDL_Rect& rect, bool hovered, bool pressed, int viewport_width, int viewport_height) {
    const float r = pressed ? 0.12f : (hovered ? 0.22f : 0.18f);
    const float g = pressed ? 0.48f : (hovered ? 0.66f : 0.58f);
    const float b = pressed ? 0.76f : (hovered ? 0.95f : 0.86f);
    DrawFilledRect(rect.x, rect.y, rect.w, rect.h, viewport_width, viewport_height, r, g, b, 0.96f);
    DrawRectOutline(rect.x, rect.y, rect.w, rect.h, viewport_width, viewport_height, 0.95f, 0.95f, 0.98f, 1.0f);
}

// 绘制输入框文字和闪烁光标。
static void DrawInputContent(const UiState& ui_state, const UiFontSet& font_set, int viewport_width, int viewport_height) {
    const SDL_Color input_color{ 230, 230, 236, 255 };
    const int text_x = ui_state.input_rect.x + kInputTextPaddingX;
    const int text_y = ui_state.input_rect.y + kInputTextPaddingY;
    DrawUtf8Text(ui_state.text, font_set.input_font, text_x, text_y, input_color, viewport_width, viewport_height);
    if (!ui_state.input_focused || !ui_state.caret_visible) {
        return;
    }
    const int text_width = MeasureTextWidth(ui_state.text, font_set.input_font);
    const int caret_x = std::min(text_x + text_width, ui_state.input_rect.x + ui_state.input_rect.w - 6);
    DrawFilledRect(caret_x, ui_state.input_rect.y + 5, 1, ui_state.input_rect.h - 10, viewport_width, viewport_height, 0.90f, 0.92f, 0.96f, 1.0f);
}

// 绘制按钮文案。
static void DrawButtonContent(const SDL_Rect& rect, const std::string& text, bool hovered, bool pressed, const UiFontSet& font_set, int viewport_width, int viewport_height) {
    if (!font_set.button_font) {
        return;
    }
    const int button_font_px = GetButtonFontPixelSize(hovered, pressed);
    TTF_SetFontSize(font_set.button_font, static_cast<float>(button_font_px));
    const int text_width = MeasureTextWidth(text, font_set.button_font);
    const int text_x = rect.x + std::max(10, (rect.w - text_width) / 2);
    const int text_y = rect.y + kButtonTextPaddingY;
    const SDL_Color button_color{ 245, 245, 250, 255 };
    DrawUtf8Text(text, font_set.button_font, text_x, text_y, button_color, viewport_width, viewport_height);
}

// 绘制状态文本区域，展示最近一次 C++ / JS 交互结果。
static void DrawStatusBar(const UiState& ui_state, const UiFontSet& font_set, int viewport_width, int viewport_height) {
    const int status_left = 12;
    const int status_top = 48;
    const int status_width = std::max(1, viewport_width - 24);
    const int status_height = 24;
    DrawFilledRect(status_left, status_top, status_width, status_height, viewport_width, viewport_height, 0.09f, 0.10f, 0.14f, 0.90f);
    DrawRectOutline(status_left, status_top, status_width, status_height, viewport_width, viewport_height, 0.20f, 0.24f, 0.31f, 1.0f);
    const SDL_Color status_color{ 208, 215, 224, 255 };
    DrawUtf8Text(ui_state.status_text, font_set.input_font, status_left + 8, status_top + 5, status_color, viewport_width, viewport_height);
}

bool InitializeUiFonts(UiFontSet& font_set) {
    font_set.input_font = TTF_OpenFont(kUiFontPath, 14.0f);
    if (!font_set.input_font) {
        return false;
    }
    font_set.button_font = TTF_OpenFont(kUiFontPath, 13.0f);
    if (!font_set.button_font) {
        TTF_CloseFont(font_set.input_font);
        font_set.input_font = nullptr;
        return false;
    }
    return true;
}

void CleanupUiFonts(UiFontSet& font_set) {
    if (font_set.input_font) {
        TTF_CloseFont(font_set.input_font);
        font_set.input_font = nullptr;
    }
    if (font_set.button_font) {
        TTF_CloseFont(font_set.button_font);
        font_set.button_font = nullptr;
    }
}

void UpdateToolbarLayout(UiState& ui_state, int window_width) {
    const int left = 12;
    const int top = 10;
    const int gap = 8;
    const int right = 12;
    const int open_html_width = 96;
    const int demo_button_width = 84;
    const int cpp_to_js_width = 96;
    const int button_height = 24;
    const int input_height = 28;
    ui_state.button_rect.w = open_html_width;
    ui_state.button_rect.h = button_height;
    ui_state.button_rect.x = std::max(left + kMinimumInputWidth + demo_button_width + cpp_to_js_width + gap * 2, window_width - right - open_html_width);
    ui_state.button_rect.y = top + (input_height - button_height) / 2;
    ui_state.demo_button_rect.w = demo_button_width;
    ui_state.demo_button_rect.h = button_height;
    ui_state.demo_button_rect.x = ui_state.button_rect.x - gap - demo_button_width;
    ui_state.demo_button_rect.y = ui_state.button_rect.y;
    ui_state.cpp_to_js_rect.w = cpp_to_js_width;
    ui_state.cpp_to_js_rect.h = button_height;
    ui_state.cpp_to_js_rect.x = ui_state.demo_button_rect.x - gap - cpp_to_js_width;
    ui_state.cpp_to_js_rect.y = ui_state.button_rect.y;
    ui_state.input_rect.x = left;
    ui_state.input_rect.y = top;
    ui_state.input_rect.h = input_height;
    ui_state.input_rect.w = std::max(kMinimumInputWidth, ui_state.cpp_to_js_rect.x - gap - ui_state.input_rect.x);
}

void UpdateInputCaretBlink(UiState& ui_state, uint64_t now_ms) {
    if (!ui_state.input_focused) {
        ui_state.caret_visible = false;
        ui_state.caret_last_toggle_ms = now_ms;
        return;
    }
    if (now_ms - ui_state.caret_last_toggle_ms >= 500) {
        ui_state.caret_visible = !ui_state.caret_visible;
        ui_state.caret_last_toggle_ms = now_ms;
    }
}

void HandleUiEvent(const SDL_Event& event, UiState& ui_state, UiActions& actions) {
    actions = {};
    if (event.type == SDL_EVENT_MOUSE_MOTION) {
        const int mouse_x = static_cast<int>(event.motion.x);
        const int mouse_y = static_cast<int>(event.motion.y);
        ui_state.input_hovered = IsPointInsideRect(mouse_x, mouse_y, ui_state.input_rect);
        ui_state.cpp_to_js_hovered = IsPointInsideRect(mouse_x, mouse_y, ui_state.cpp_to_js_rect);
        ui_state.demo_button_hovered = IsPointInsideRect(mouse_x, mouse_y, ui_state.demo_button_rect);
        ui_state.button_hovered = IsPointInsideRect(mouse_x, mouse_y, ui_state.button_rect);
    }
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
        const int mouse_x = static_cast<int>(event.button.x);
        const int mouse_y = static_cast<int>(event.button.y);
        if (IsPointInsideRect(mouse_x, mouse_y, ui_state.input_rect)) {
            ui_state.input_focused = true;
            ui_state.caret_visible = true;
            return;
        }
        if (IsPointInsideRect(mouse_x, mouse_y, ui_state.cpp_to_js_rect)) {
            ui_state.cpp_to_js_pressed = true;
            ui_state.input_focused = false;
            return;
        }
        if (IsPointInsideRect(mouse_x, mouse_y, ui_state.demo_button_rect)) {
            ui_state.demo_button_pressed = true;
            ui_state.input_focused = false;
            return;
        }
        if (IsPointInsideRect(mouse_x, mouse_y, ui_state.button_rect)) {
            ui_state.button_pressed = true;
            ui_state.input_focused = false;
            return;
        }
        ui_state.input_focused = false;
        ui_state.cpp_to_js_pressed = false;
        ui_state.demo_button_pressed = false;
        ui_state.button_pressed = false;
    }
    if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT) {
        const int mouse_x = static_cast<int>(event.button.x);
        const int mouse_y = static_cast<int>(event.button.y);
        actions.cpp_to_js_clicked = ui_state.cpp_to_js_pressed && IsPointInsideRect(mouse_x, mouse_y, ui_state.cpp_to_js_rect);
        actions.load_demo_clicked = ui_state.demo_button_pressed && IsPointInsideRect(mouse_x, mouse_y, ui_state.demo_button_rect);
        actions.open_html_clicked = ui_state.button_pressed && IsPointInsideRect(mouse_x, mouse_y, ui_state.button_rect);
        ui_state.cpp_to_js_pressed = false;
        ui_state.demo_button_pressed = false;
        ui_state.button_pressed = false;
    }
    if (!ui_state.input_focused) {
        return;
    }
    if (event.type == SDL_EVENT_TEXT_INPUT) {
        if (event.text.text) {
            ui_state.text += event.text.text;
            ui_state.caret_visible = true;
        }
        return;
    }
    if (event.type != SDL_EVENT_KEY_DOWN) {
        return;
    }
    if (event.key.key == SDLK_BACKSPACE && !ui_state.text.empty()) {
        ui_state.text.pop_back();
        ui_state.caret_visible = true;
        return;
    }
    if (event.key.key == SDLK_ESCAPE) {
        ui_state.input_focused = false;
        return;
    }
}

void DrawToolbar(const UiState& ui_state, const UiFontSet& font_set, int window_width, int window_height) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_TEXTURE_2D);
    DrawFilledRect(0, 0, window_width, kToolbarHeight, window_width, window_height, 0.05f, 0.06f, 0.09f, 0.92f);
    DrawInputFieldBackground(ui_state, window_width, window_height);
    DrawButtonBackground(ui_state.cpp_to_js_rect, ui_state.cpp_to_js_hovered, ui_state.cpp_to_js_pressed, window_width, window_height);
    DrawButtonBackground(ui_state.demo_button_rect, ui_state.demo_button_hovered, ui_state.demo_button_pressed, window_width, window_height);
    DrawButtonBackground(ui_state.button_rect, ui_state.button_hovered, ui_state.button_pressed, window_width, window_height);
    DrawInputContent(ui_state, font_set, window_width, window_height);
    DrawButtonContent(ui_state.cpp_to_js_rect, ui_state.cpp_to_js_text, ui_state.cpp_to_js_hovered, ui_state.cpp_to_js_pressed, font_set, window_width, window_height);
    DrawButtonContent(ui_state.demo_button_rect, ui_state.demo_button_text, ui_state.demo_button_hovered, ui_state.demo_button_pressed, font_set, window_width, window_height);
    DrawButtonContent(ui_state.button_rect, ui_state.button_text, ui_state.button_hovered, ui_state.button_pressed, font_set, window_width, window_height);
    DrawStatusBar(ui_state, font_set, window_width, window_height);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
}

std::string BuildLocalFileNavigationTarget(const std::wstring& file_path) {
    if (file_path.empty()) {
        return "";
    }
    std::wstring absolute_path = std::filesystem::absolute(file_path).wstring();
    std::replace(absolute_path.begin(), absolute_path.end(), L'\\', L'/');
    return "file:///" + WideToUtf8String(absolute_path);
}

std::string BuildNavigationTarget(const std::string& raw_input) {
    if (raw_input.empty()) {
        return "https://www.baidu.com";
    }
    if (!LooksLikeLocalHtmlPath(raw_input)) {
        return raw_input;
    }
    return BuildLocalFileNavigationTarget(std::filesystem::u8path(raw_input).wstring());
}
