#include "SdlRuntime.h"

#include "InputProtocol.h"
#include "ShmemProtocol.h"
#include "SdlUi.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_video.h>
#include <SDL3_ttf/SDL_ttf.h>

#include "include/internal/cef_types.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include <commdlg.h>
#include <windows.h>

// SDL 主窗口初始宽度。
constexpr int kInitialWindowWidth = 1280;

// SDL 主窗口初始高度。
constexpr int kInitialWindowHeight = 720;

// SDL 主窗口标题。
constexpr const char* kWindowTitle = "浏览器";

// SDL 运行时上下文，集中保存主流程需要的全部状态。
struct SdlAppRuntime {
    SDL_Window* window = nullptr;                                              // SDL 窗口句柄。
    SDL_GLContext gl_context = nullptr;                                        // OpenGL 上下文。
    bool text_input_started = false;                                           // 是否已经开启 SDL 文本输入。
    bool ttf_started = false;                                                  // 是否已经初始化 SDL_ttf。
    size_t shared_memory_bytes = sizeof(SharedFrameHeader) + kMaxPixelBytes;   // 帧共享内存总大小。
    std::wstring shared_memory_name;                                           // 帧共享内存名称。
    HANDLE shared_mapping = nullptr;                                           // 帧共享内存句柄。
    uint8_t* shared_view = nullptr;                                            // 帧共享内存映射地址。
    std::wstring ime_shared_memory_name;                                       // IME 共享内存名称。
    HANDLE ime_mapping = nullptr;                                              // IME 共享内存句柄。
    uint8_t* ime_view = nullptr;                                               // IME 共享内存映射地址。
    std::wstring bridge_demo_shared_memory_name;                               // JS 桥接演示共享内存名称。
    HANDLE bridge_demo_mapping = nullptr;                                      // JS 桥接演示共享内存句柄。
    uint8_t* bridge_demo_view = nullptr;                                       // JS 桥接演示共享内存映射地址。
    PROCESS_INFORMATION cef_process{};                                         // CEF 子进程信息。
    HANDLE cef_job = nullptr;                                                  // CEF JobObject 句柄。
    std::wstring input_pipe_name;                                              // 输入管道名称。
    HANDLE input_pipe = INVALID_HANDLE_VALUE;                                  // 输入管道句柄。
    GLuint browser_texture_id = 0;                                             // 浏览器纹理 ID。
    uint64_t last_frame_id = 0;                                                // 上次上传的帧号。
    int browser_texture_width = 0;                                             // 浏览器纹理宽度。
    int browser_texture_height = 0;                                            // 浏览器纹理高度。
    std::vector<uint8_t> browser_pixels;                                       // 浏览器像素缓存。
    float wheel_residual_x = 0.0f;                                             // 横向滚轮残差。
    float wheel_residual_y = 0.0f;                                             // 纵向滚轮残差。
    uint64_t last_cursor_seq = 0;                                              // 上次应用的浏览器光标序号。
    SDL_SystemCursor current_cursor_id = SDL_SYSTEM_CURSOR_DEFAULT;             // 当前系统光标类型。
    SDL_Cursor* cursor_cache[SDL_SYSTEM_CURSOR_COUNT] = {};                     // 系统光标缓存。
    uint64_t input_sequence = 0;                                               // 输入事件序号。
    uint64_t last_bridge_demo_seq = 0;                                         // 上次读取的桥接状态序号。
    UiState ui_state;                                                          // 顶部工具栏状态。
    UiFontSet ui_fonts;                                                        // 顶部工具栏字体资源。
    bool is_running = true;                                                    // 主循环运行标志。
    SDL_SystemCursor browser_cursor_id = SDL_SYSTEM_CURSOR_DEFAULT;             // 浏览器希望使用的光标类型。
    bool ui_cursor_override = false;                                           // UI 是否正在接管光标。
    int last_sent_browser_width = 0;                                           // 上次发送给 CEF 的浏览区宽度。
    int last_sent_browser_height = 0;                                          // 上次发送给 CEF 的浏览区高度。
};

// 计算浏览区高度，始终保证最小值为 1。
static int GetBrowserViewportHeight(int window_height) {
    return std::max(1, window_height - kToolbarHeight);
}

// 判断 y 坐标是否落在工具栏区域。
static bool IsToolbarArea(int window_y) {
    return window_y >= 0 && window_y < kToolbarHeight;
}

// 把窗口坐标系 y 转成浏览区坐标系 y。
static int ConvertToBrowserY(int window_y) {
    return window_y - kToolbarHeight;
}

// 构造输入事件基础包，统一填充序号、类型和修饰键。
static InputEventPacket CreateBaseInputPacket(SdlAppRuntime& runtime, InputEventType event_type) {
    InputEventPacket packet{};
    packet.seq = ++runtime.input_sequence;
    packet.type = static_cast<uint32_t>(event_type);
    packet.modifiers = 0;
    const SDL_Keymod key_mod = SDL_GetModState();
    if (key_mod & SDL_KMOD_SHIFT) {
        packet.modifiers |= EVENTFLAG_SHIFT_DOWN;
    }
    if (key_mod & SDL_KMOD_CTRL) {
        packet.modifiers |= EVENTFLAG_CONTROL_DOWN;
    }
    if (key_mod & SDL_KMOD_ALT) {
        packet.modifiers |= EVENTFLAG_ALT_DOWN;
    }
    float mouse_x = 0.0f;
    float mouse_y = 0.0f;
    const SDL_MouseButtonFlags mouse_buttons = SDL_GetMouseState(&mouse_x, &mouse_y);
    if (mouse_buttons & SDL_BUTTON_LMASK) {
        packet.modifiers |= EVENTFLAG_LEFT_MOUSE_BUTTON;
    }
    if (mouse_buttons & SDL_BUTTON_MMASK) {
        packet.modifiers |= EVENTFLAG_MIDDLE_MOUSE_BUTTON;
    }
    if (mouse_buttons & SDL_BUTTON_RMASK) {
        packet.modifiers |= EVENTFLAG_RIGHT_MOUSE_BUTTON;
    }
    return packet;
}

// 把 SDL 按键值转换成 Windows 虚拟键码。
static uint32_t ToWindowsVirtualKey(SDL_Keycode keycode) {
    if (keycode >= SDLK_A && keycode <= SDLK_Z) {
        return static_cast<uint32_t>('A' + (keycode - SDLK_A));
    }
    if (keycode >= SDLK_0 && keycode <= SDLK_9) {
        return static_cast<uint32_t>('0' + (keycode - SDLK_0));
    }
    switch (keycode) {
    case SDLK_RETURN: return VK_RETURN;
    case SDLK_ESCAPE: return VK_ESCAPE;
    case SDLK_BACKSPACE: return VK_BACK;
    case SDLK_TAB: return VK_TAB;
    case SDLK_SPACE: return VK_SPACE;
    case SDLK_LEFT: return VK_LEFT;
    case SDLK_RIGHT: return VK_RIGHT;
    case SDLK_UP: return VK_UP;
    case SDLK_DOWN: return VK_DOWN;
    case SDLK_DELETE: return VK_DELETE;
    case SDLK_HOME: return VK_HOME;
    case SDLK_END: return VK_END;
    case SDLK_PAGEUP: return VK_PRIOR;
    case SDLK_PAGEDOWN: return VK_NEXT;
    case SDLK_F1: return VK_F1;
    case SDLK_F2: return VK_F2;
    case SDLK_F3: return VK_F3;
    case SDLK_F4: return VK_F4;
    case SDLK_F5: return VK_F5;
    case SDLK_F6: return VK_F6;
    case SDLK_F7: return VK_F7;
    case SDLK_F8: return VK_F8;
    case SDLK_F9: return VK_F9;
    case SDLK_F10: return VK_F10;
    case SDLK_F11: return VK_F11;
    case SDLK_F12: return VK_F12;
    default: return static_cast<uint32_t>(keycode);
    }
}

// 把 SDL 滚轮数据转换成 CEF 需要的整型 delta。
static int BuildWheelDelta(float raw_delta, int integer_delta, SDL_MouseWheelDirection direction, float& residual) {
    float delta = integer_delta != 0 ? static_cast<float>(integer_delta) * 120.0f : raw_delta * 120.0f;
    if (direction == SDL_MOUSEWHEEL_FLIPPED) {
        delta = -delta;
    }
    delta += residual;
    int output = static_cast<int>(delta);
    residual = delta - static_cast<float>(output);
    if (output == 0 && delta != 0.0f) {
        output = delta > 0.0f ? 1 : -1;
        residual = 0.0f;
    }
    return output;
}

// 把 CEF 光标类型映射成 SDL 光标类型。
static SDL_SystemCursor MapCefCursorToSdl(uint32_t cursor_type) {
    switch (static_cast<cef_cursor_type_t>(cursor_type)) {
    case CT_HAND: return SDL_SYSTEM_CURSOR_POINTER;
    case CT_IBEAM:
    case CT_VERTICALTEXT: return SDL_SYSTEM_CURSOR_TEXT;
    case CT_CROSS: return SDL_SYSTEM_CURSOR_CROSSHAIR;
    case CT_WAIT: return SDL_SYSTEM_CURSOR_WAIT;
    case CT_PROGRESS: return SDL_SYSTEM_CURSOR_PROGRESS;
    case CT_MOVE:
    case CT_MIDDLEPANNING:
    case CT_EASTPANNING:
    case CT_NORTHPANNING:
    case CT_NORTHEASTPANNING:
    case CT_NORTHWESTPANNING:
    case CT_SOUTHPANNING:
    case CT_SOUTHEASTPANNING:
    case CT_SOUTHWESTPANNING:
    case CT_WESTPANNING:
    case CT_MIDDLE_PANNING_VERTICAL:
    case CT_MIDDLE_PANNING_HORIZONTAL: return SDL_SYSTEM_CURSOR_MOVE;
    case CT_EASTWESTRESIZE: return SDL_SYSTEM_CURSOR_EW_RESIZE;
    case CT_NORTHSOUTHRESIZE: return SDL_SYSTEM_CURSOR_NS_RESIZE;
    case CT_NORTHEASTSOUTHWESTRESIZE: return SDL_SYSTEM_CURSOR_NESW_RESIZE;
    case CT_NORTHWESTSOUTHEASTRESIZE: return SDL_SYSTEM_CURSOR_NWSE_RESIZE;
    case CT_EASTRESIZE: return SDL_SYSTEM_CURSOR_E_RESIZE;
    case CT_NORTHRESIZE: return SDL_SYSTEM_CURSOR_N_RESIZE;
    case CT_NORTHEASTRESIZE: return SDL_SYSTEM_CURSOR_NE_RESIZE;
    case CT_NORTHWESTRESIZE: return SDL_SYSTEM_CURSOR_NW_RESIZE;
    case CT_SOUTHRESIZE: return SDL_SYSTEM_CURSOR_S_RESIZE;
    case CT_SOUTHEASTRESIZE: return SDL_SYSTEM_CURSOR_SE_RESIZE;
    case CT_SOUTHWESTRESIZE: return SDL_SYSTEM_CURSOR_SW_RESIZE;
    case CT_WESTRESIZE: return SDL_SYSTEM_CURSOR_W_RESIZE;
    case CT_NODROP:
    case CT_NOTALLOWED: return SDL_SYSTEM_CURSOR_NOT_ALLOWED;
    default: return SDL_SYSTEM_CURSOR_DEFAULT;
    }
}

// 为指定光标类型创建并缓存 SDL 系统光标。
static void SetCachedCursor(SdlAppRuntime& runtime, SDL_SystemCursor cursor_id) {
    if (!runtime.cursor_cache[cursor_id]) {
        runtime.cursor_cache[cursor_id] = SDL_CreateSystemCursor(cursor_id);
    }
    if (runtime.cursor_cache[cursor_id] && runtime.current_cursor_id != cursor_id) {
        SDL_SetCursor(runtime.cursor_cache[cursor_id]);
        runtime.current_cursor_id = cursor_id;
    }
}

// 根据当前工具栏状态决定是否覆盖浏览器光标。
static void ApplyToolbarCursor(SdlAppRuntime& runtime) {
    if (runtime.ui_state.input_focused || runtime.ui_state.input_hovered) {
        runtime.ui_cursor_override = true;
        SetCachedCursor(runtime, SDL_SYSTEM_CURSOR_TEXT);
        return;
    }
    if (runtime.ui_state.cpp_to_js_hovered || runtime.ui_state.demo_button_hovered || runtime.ui_state.button_hovered) {
        runtime.ui_cursor_override = true;
        SetCachedCursor(runtime, SDL_SYSTEM_CURSOR_POINTER);
        return;
    }
    if (runtime.ui_cursor_override) {
        runtime.ui_cursor_override = false;
        SetCachedCursor(runtime, runtime.browser_cursor_id);
    }
}

// 判断键盘事件是否应该被输入框吃掉，不再继续传给浏览器。
static bool ShouldCaptureKeyboardEvent(const SDL_Event& event, const UiState& ui_state) {
    if (!ui_state.input_focused) {
        return false;
    }
    return event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP || event.type == SDL_EVENT_TEXT_INPUT || event.type == SDL_EVENT_TEXT_EDITING;
}

// 向输入管道发送一个固定长度事件包。
static bool SendInputPacket(HANDLE pipe, const InputEventPacket& packet) {
    if (pipe == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    return WriteFile(pipe, &packet, sizeof(packet), &written, nullptr) && written == sizeof(packet);
}

// 发送浏览器焦点事件。
static void SendBrowserFocusPacket(SdlAppRuntime& runtime, bool browser_has_focus) {
    InputEventPacket packet = CreateBaseInputPacket(runtime, InputEventType::Focus);
    packet.key_up = browser_has_focus ? 0u : 1u;
    SendInputPacket(runtime.input_pipe, packet);
}

// 发送浏览区尺寸变化事件。
static void SendBrowserResizePacket(SdlAppRuntime& runtime, int browser_width, int browser_height) {
    InputEventPacket packet = CreateBaseInputPacket(runtime, InputEventType::Resize);
    packet.width = static_cast<uint32_t>(browser_width);
    packet.height = static_cast<uint32_t>(browser_height);
    SendInputPacket(runtime.input_pipe, packet);
}

// 发送导航事件，让浏览器加载指定地址。
static void SendNavigatePacket(SdlAppRuntime& runtime, const std::string& target_url) {
    InputEventPacket packet = CreateBaseInputPacket(runtime, InputEventType::Navigate);
    std::strncpy(packet.text, target_url.c_str(), sizeof(packet.text) - 1);
    packet.text[sizeof(packet.text) - 1] = '\0';
    SendInputPacket(runtime.input_pipe, packet);
}

// 发送执行 JS 事件，让 CEF 在当前页面里运行一段脚本。
static void SendExecuteJsPacket(SdlAppRuntime& runtime, const std::string& script) {
    InputEventPacket packet = CreateBaseInputPacket(runtime, InputEventType::ExecuteJs);
    std::strncpy(packet.text, script.c_str(), sizeof(packet.text) - 1);
    packet.text[sizeof(packet.text) - 1] = '\0';
    SendInputPacket(runtime.input_pipe, packet);
}

// 把 Windows 宽字符串路径转成 UTF-8，供输入框显示和协议发送使用。
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

// 把 UTF-8 字符串转成宽字符串，供启动参数和 Win32 API 使用。
static std::wstring Utf8ToWideString(const std::string& utf8_text) {
    if (utf8_text.empty()) {
        return L"";
    }
    const int wide_length = MultiByteToWideChar(CP_UTF8, 0, utf8_text.c_str(), static_cast<int>(utf8_text.size()), nullptr, 0);
    if (wide_length <= 0) {
        return L"";
    }
    std::wstring wide_text(static_cast<size_t>(wide_length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8_text.c_str(), static_cast<int>(utf8_text.size()), wide_text.data(), wide_length);
    return wide_text;
}

// 从 SDL 窗口取出原生 HWND，供 Win32 文件对话框作为父窗口使用。
static HWND GetNativeWindowHandle(SDL_Window* window) {
    if (!window) {
        return nullptr;
    }
    return static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
}

// 弹出本地 HTML 文件选择框；只有用户真正选中文件时才返回 true。
static bool SelectLocalHtmlFile(SDL_Window* window, std::string& out_display_text, std::string& out_target_url) {
    wchar_t file_buffer[4096] = {};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = GetNativeWindowHandle(window);
    dialog.lpstrFilter = L"HTML 文件 (*.html;*.htm)\0*.html;*.htm\0";
    dialog.lpstrFile = file_buffer;
    dialog.nMaxFile = static_cast<DWORD>(sizeof(file_buffer) / sizeof(file_buffer[0]));
    dialog.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&dialog)) {
        return false;
    }
    const std::wstring selected_file = file_buffer;
    out_display_text = WideToUtf8String(selected_file);
    out_target_url = BuildLocalFileNavigationTarget(selected_file);
    return !out_target_url.empty();
}

// 获取随程序一起分发的演示页绝对路径。
static std::wstring GetBundledDemoHtmlPath() {
    wchar_t module_path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, module_path, MAX_PATH);
    return (std::filesystem::path(module_path).parent_path() / L"demo" / L"js_bridge_demo.html").wstring();
}

// 启动时优先准备本地演示页地址，不存在时回退到默认网页。
static std::wstring PrepareStartupUrl(UiState& ui_state) {
    const std::wstring demo_path = GetBundledDemoHtmlPath();
    if (std::filesystem::exists(demo_path)) {
        ui_state.text = WideToUtf8String(demo_path);
        ui_state.status_text = "已自动加载演示页，可直接测试 C++ / JS 双向通信";
        const std::string demo_url = BuildLocalFileNavigationTarget(demo_path);
        return Utf8ToWideString(demo_url);
    }
    ui_state.status_text = "未找到 demo/js_bridge_demo.html，可手动打开本地 HTML";
    return L"https://www.baidu.com";
}

// 当输入框焦点变化时，把浏览器焦点同步到 CEF。
static void SyncBrowserFocusWithInputState(SdlAppRuntime& runtime, bool was_input_focused) {
    if (was_input_focused == runtime.ui_state.input_focused) {
        return;
    }
    SendBrowserFocusPacket(runtime, !runtime.ui_state.input_focused);
}

// 创建帧共享内存。
static bool CreateSharedFrameMemory(const std::wstring& name, size_t bytes, HANDLE& out_mapping, uint8_t*& out_view) {
    out_mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, static_cast<DWORD>(bytes), name.c_str());
    if (!out_mapping) {
        std::wcerr << L"[SDL] CreateFileMappingW 失败, err=" << GetLastError() << std::endl;
        return false;
    }
    out_view = static_cast<uint8_t*>(MapViewOfFile(out_mapping, FILE_MAP_ALL_ACCESS, 0, 0, bytes));
    if (!out_view) {
        std::wcerr << L"[SDL] MapViewOfFile 失败, err=" << GetLastError() << std::endl;
        CloseHandle(out_mapping);
        out_mapping = nullptr;
        return false;
    }
    auto* header = reinterpret_cast<SharedFrameHeader*>(out_view);
    header->magic = kFrameMagic;
    header->version = kFrameVersion;
    header->width = 0;
    header->height = 0;
    header->stride = 0;
    header->frame_id = 0;
    header->pixel_bytes = 0;
    header->reserved = 0;
    return true;
}

// 创建 IME 共享内存。
static bool CreateImeUiSharedMemory(const std::wstring& name, HANDLE& out_mapping, uint8_t*& out_view) {
    const DWORD bytes = static_cast<DWORD>(sizeof(ImeUiState));
    out_mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, bytes, name.c_str());
    if (!out_mapping) {
        std::wcerr << L"[SDL] IME CreateFileMappingW 失败, err=" << GetLastError() << std::endl;
        return false;
    }
    out_view = static_cast<uint8_t*>(MapViewOfFile(out_mapping, FILE_MAP_ALL_ACCESS, 0, 0, bytes));
    if (!out_view) {
        std::wcerr << L"[SDL] IME MapViewOfFile 失败, err=" << GetLastError() << std::endl;
        CloseHandle(out_mapping);
        out_mapping = nullptr;
        return false;
    }
    auto* ime_state = reinterpret_cast<ImeUiState*>(out_view);
    ime_state->magic = kImeUiMagic;
    ime_state->version = kImeUiVersion;
    ime_state->visible = 0;
    ime_state->seq = 0;
    ime_state->x = 0;
    ime_state->y = 0;
    ime_state->w = 0;
    ime_state->h = 0;
    ime_state->cursor_x = 0;
    ime_state->cursor_y = 0;
    ime_state->reserved1 = 0;
    ime_state->reserved2 = 0;
    ime_state->cursor_type = static_cast<uint32_t>(CT_POINTER);
    ime_state->reserved3 = 0;
    ime_state->cursor_seq = 0;
    return true;
}

// 创建 JS 桥接演示状态共享内存。
static bool CreateBridgeDemoSharedMemory(const std::wstring& name, HANDLE& out_mapping, uint8_t*& out_view) {
    const DWORD bytes = static_cast<DWORD>(sizeof(BridgeDemoState));
    out_mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, bytes, name.c_str());
    if (!out_mapping) {
        std::wcerr << L"[SDL] BridgeDemo CreateFileMappingW 失败, err=" << GetLastError() << std::endl;
        return false;
    }
    out_view = static_cast<uint8_t*>(MapViewOfFile(out_mapping, FILE_MAP_ALL_ACCESS, 0, 0, bytes));
    if (!out_view) {
        std::wcerr << L"[SDL] BridgeDemo MapViewOfFile 失败, err=" << GetLastError() << std::endl;
        CloseHandle(out_mapping);
        out_mapping = nullptr;
        return false;
    }
    auto* state = reinterpret_cast<BridgeDemoState*>(out_view);
    state->magic = kBridgeDemoMagic;
    state->version = kBridgeDemoVersion;
    state->seq = 0;
    state->status_text[0] = '\0';
    return true;
}

// 启动 CEF 子进程。
static bool StartCefProcess(const std::wstring& shared_memory_name, const std::wstring& input_pipe_name, const std::wstring& ime_shared_memory_name, const std::wstring& bridge_demo_shared_memory_name, int width, int height, const std::wstring& url, PROCESS_INFORMATION& out_process, HANDLE& out_job) {
    wchar_t module_path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, module_path, MAX_PATH);
    const std::wstring working_directory = std::filesystem::path(module_path).parent_path().wstring();
    std::wstring command_line = L"\"" + std::wstring(module_path) + L"\"";
    command_line += L" --lrq-cef-host";
    command_line += L" --shm-name=" + shared_memory_name;
    command_line += L" --width=" + std::to_wstring(width);
    command_line += L" --height=" + std::to_wstring(height);
    command_line += L" --input-pipe=" + input_pipe_name;
    command_line += L" --ime-shm-name=" + ime_shared_memory_name;
    command_line += L" --bridge-demo-shm-name=" + bridge_demo_shared_memory_name;
    command_line += L" --url=" + url;
    STARTUPINFOW startup_info{};
    startup_info.cb = sizeof(startup_info);
    ZeroMemory(&out_process, sizeof(out_process));
    std::vector<wchar_t> command_buffer(command_line.begin(), command_line.end());
    command_buffer.push_back(L'\0');
    const BOOL ok = CreateProcessW(nullptr, command_buffer.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, working_directory.c_str(), &startup_info, &out_process);
    if (!ok) {
        std::wcerr << L"[SDL] CreateProcessW 启动 CEF 失败, err=" << GetLastError() << std::endl;
        return false;
    }
    out_job = CreateJobObjectW(nullptr, nullptr);
    if (out_job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit_info{};
        limit_info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (SetInformationJobObject(out_job, JobObjectExtendedLimitInformation, &limit_info, sizeof(limit_info))) {
            AssignProcessToJobObject(out_job, out_process.hProcess);
        }
    }
    return true;
}

// 销毁 CEF 子进程句柄和 JobObject。
static void DestroyCefProcess(PROCESS_INFORMATION& process_info, HANDLE job) {
    if (process_info.hThread) {
        CloseHandle(process_info.hThread);
        process_info.hThread = nullptr;
    }
    if (process_info.hProcess) {
        CloseHandle(process_info.hProcess);
        process_info.hProcess = nullptr;
    }
    if (job) {
        CloseHandle(job);
    }
}

// 连接到 CEF 输入管道。
static HANDLE ConnectInputPipeClient(const std::wstring& pipe_name, DWORD timeout_ms) {
    const DWORD start_ticks = GetTickCount();
    while (GetTickCount() - start_ticks < timeout_ms) {
        HANDLE pipe = CreateFileW(pipe_name.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (pipe != INVALID_HANDLE_VALUE) {
            DWORD mode = PIPE_READMODE_MESSAGE;
            SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr);
            return pipe;
        }
        Sleep(20);
    }
    return INVALID_HANDLE_VALUE;
}

// 初始化 SDL、窗口、OpenGL 和 UI 字体资源。
static bool InitializeWindowAndUi(SdlAppRuntime& runtime) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "SDL_Init 失败: " << SDL_GetError() << std::endl;
        return false;
    }
    if (!TTF_Init()) {
        std::cerr << "TTF_Init 失败: " << SDL_GetError() << std::endl;
        return false;
    }
    runtime.ttf_started = true;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    runtime.window = SDL_CreateWindow(kWindowTitle, kInitialWindowWidth, kInitialWindowHeight, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!runtime.window) {
        std::cerr << "SDL_CreateWindow 失败: " << SDL_GetError() << std::endl;
        return false;
    }
    if (!SDL_SetWindowMinimumSize(runtime.window, 960, 540)) {
        std::cerr << "SDL_SetWindowMinimumSize 失败: " << SDL_GetError() << std::endl;
    }
    int window_width = 0;
    int window_height = 0;
    SDL_GetWindowSize(runtime.window, &window_width, &window_height);
    UpdateToolbarLayout(runtime.ui_state, window_width);
    runtime.gl_context = SDL_GL_CreateContext(runtime.window);
    if (!runtime.gl_context) {
        std::cerr << "SDL_GL_CreateContext 失败: " << SDL_GetError() << std::endl;
        return false;
    }
    if (!InitializeUiFonts(runtime.ui_fonts)) {
        std::cerr << "InitializeUiFonts 失败: " << SDL_GetError() << std::endl;
        return false;
    }
    SDL_GL_SetSwapInterval(1);
    return true;
}

// 初始化共享内存区域。
static bool InitializeSharedMemory(SdlAppRuntime& runtime) {
    runtime.shared_memory_name = L"Local\\CefSdlOsrFrame_" + std::to_wstring(GetCurrentProcessId());
    if (!CreateSharedFrameMemory(runtime.shared_memory_name, runtime.shared_memory_bytes, runtime.shared_mapping, runtime.shared_view)) {
        return false;
    }
    runtime.ime_shared_memory_name = L"Local\\CefSdlImeUi_" + std::to_wstring(GetCurrentProcessId());
    if (!CreateImeUiSharedMemory(runtime.ime_shared_memory_name, runtime.ime_mapping, runtime.ime_view)) {
        return false;
    }
    runtime.bridge_demo_shared_memory_name = L"Local\\CefSdlBridgeDemo_" + std::to_wstring(GetCurrentProcessId());
    if (!CreateBridgeDemoSharedMemory(runtime.bridge_demo_shared_memory_name, runtime.bridge_demo_mapping, runtime.bridge_demo_view)) {
        return false;
    }
    return true;
}

// 启动 CEF 并连接输入管道。
static bool InitializeCefBridge(SdlAppRuntime& runtime) {
    runtime.input_pipe_name = L"\\\\.\\pipe\\CefSdlInput_" + std::to_wstring(GetCurrentProcessId());
    const std::wstring startup_url = PrepareStartupUrl(runtime.ui_state);
    if (!StartCefProcess(runtime.shared_memory_name, runtime.input_pipe_name, runtime.ime_shared_memory_name, runtime.bridge_demo_shared_memory_name, kInitialWindowWidth, GetBrowserViewportHeight(kInitialWindowHeight), startup_url, runtime.cef_process, runtime.cef_job)) {
        return false;
    }
    runtime.input_pipe = ConnectInputPipeClient(runtime.input_pipe_name, 8000);
    if (runtime.input_pipe == INVALID_HANDLE_VALUE) {
        std::wcerr << L"[SDL] 输入管道连接失败，页面将无法响应输入: " << runtime.input_pipe_name << std::endl;
    }
    return true;
}

// 初始化浏览器纹理对象。
static void InitializeBrowserTexture(SdlAppRuntime& runtime) {
    glGenTextures(1, &runtime.browser_texture_id);
    glBindTexture(GL_TEXTURE_2D, runtime.browser_texture_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
}

// 处理一个 SDL 事件。
static void HandleSdlEvent(const SDL_Event& event, SdlAppRuntime& runtime) {
    if (event.type == SDL_EVENT_QUIT) {
        runtime.is_running = false;
        return;
    }
    const bool was_input_focused = runtime.ui_state.input_focused;
    UiActions actions{};
    HandleUiEvent(event, runtime.ui_state, actions);
    ApplyToolbarCursor(runtime);
    SyncBrowserFocusWithInputState(runtime, was_input_focused);
    if (event.type == SDL_EVENT_KEY_DOWN && runtime.ui_state.input_focused && (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER)) {
        runtime.ui_state.input_focused = false;
        ApplyToolbarCursor(runtime);
        SyncBrowserFocusWithInputState(runtime, true);
        SendNavigatePacket(runtime, BuildNavigationTarget(runtime.ui_state.text));
        return;
    }
    if (actions.cpp_to_js_clicked) {
        SendExecuteJsPacket(runtime, "(window.onNativePulse ? window.onNativePulse() : 'window.onNativePulse not found')");
        return;
    }
    if (actions.load_demo_clicked) {
        const std::wstring demo_path = GetBundledDemoHtmlPath();
        if (std::filesystem::exists(demo_path)) {
            runtime.ui_state.text = WideToUtf8String(demo_path);
            SendNavigatePacket(runtime, BuildLocalFileNavigationTarget(demo_path));
        } else {
            runtime.ui_state.status_text = "未找到 demo/js_bridge_demo.html，无法加载演示页";
        }
        return;
    }
    if (actions.open_html_clicked) {
        std::string selected_file_path;
        std::string selected_target_url;
        if (SelectLocalHtmlFile(runtime.window, selected_file_path, selected_target_url)) {
            runtime.ui_state.text = selected_file_path;
            SendNavigatePacket(runtime, selected_target_url);
        }
        return;
    }
    if (ShouldCaptureKeyboardEvent(event, runtime.ui_state)) {
        return;
    }
    if (event.type == SDL_EVENT_MOUSE_MOTION) {
        const int mouse_x = static_cast<int>(event.motion.x);
        const int mouse_y = static_cast<int>(event.motion.y);
        if (IsToolbarArea(mouse_y)) {
            return;
        }
        InputEventPacket packet = CreateBaseInputPacket(runtime, InputEventType::MouseMove);
        packet.x = mouse_x;
        packet.y = std::max(0, ConvertToBrowserY(mouse_y));
        SendInputPacket(runtime.input_pipe, packet);
        return;
    }
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
        const int mouse_x = static_cast<int>(event.button.x);
        const int mouse_y = static_cast<int>(event.button.y);
        if (IsToolbarArea(mouse_y)) {
            return;
        }
        InputEventPacket packet = CreateBaseInputPacket(runtime, InputEventType::MouseButton);
        packet.x = mouse_x;
        packet.y = std::max(0, ConvertToBrowserY(mouse_y));
        packet.mouse_up = event.type == SDL_EVENT_MOUSE_BUTTON_UP ? 1u : 0u;
        packet.click_count = static_cast<uint32_t>(event.button.clicks);
        if (event.button.button == SDL_BUTTON_LEFT) {
            packet.mouse_button = static_cast<uint32_t>(MouseButtonType::Left);
        } else if (event.button.button == SDL_BUTTON_MIDDLE) {
            packet.mouse_button = static_cast<uint32_t>(MouseButtonType::Middle);
        } else if (event.button.button == SDL_BUTTON_RIGHT) {
            packet.mouse_button = static_cast<uint32_t>(MouseButtonType::Right);
        }
        SendInputPacket(runtime.input_pipe, packet);
        return;
    }
    if (event.type == SDL_EVENT_MOUSE_WHEEL) {
        const int mouse_x = static_cast<int>(event.wheel.mouse_x);
        const int mouse_y = static_cast<int>(event.wheel.mouse_y);
        if (IsToolbarArea(mouse_y)) {
            return;
        }
        InputEventPacket packet = CreateBaseInputPacket(runtime, InputEventType::MouseWheel);
        packet.x = mouse_x;
        packet.y = std::max(0, ConvertToBrowserY(mouse_y));
        packet.delta_x = BuildWheelDelta(event.wheel.x, static_cast<int>(event.wheel.integer_x), event.wheel.direction, runtime.wheel_residual_x);
        packet.delta_y = BuildWheelDelta(event.wheel.y, static_cast<int>(event.wheel.integer_y), event.wheel.direction, runtime.wheel_residual_y);
        if (packet.delta_x != 0 || packet.delta_y != 0) {
            SendInputPacket(runtime.input_pipe, packet);
        }
        return;
    }
    if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) {
        if (event.key.key == SDLK_F12 || event.key.scancode == SDL_SCANCODE_F12) {
            return;
        }
        InputEventPacket packet = CreateBaseInputPacket(runtime, InputEventType::Key);
        packet.key_up = event.type == SDL_EVENT_KEY_UP ? 1u : 0u;
        packet.key_code = ToWindowsVirtualKey(event.key.key);
        packet.native_key_code = packet.key_code;
        SendInputPacket(runtime.input_pipe, packet);
        return;
    }
    if (event.type == SDL_EVENT_TEXT_INPUT) {
        InputEventPacket packet = CreateBaseInputPacket(runtime, InputEventType::Text);
        std::strncpy(packet.text, event.text.text, sizeof(packet.text) - 1);
        packet.text[sizeof(packet.text) - 1] = '\0';
        SendInputPacket(runtime.input_pipe, packet);
        return;
    }
    if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
        SendBrowserFocusPacket(runtime, false);
        return;
    }
    if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED && !runtime.ui_state.input_focused) {
        SendBrowserFocusPacket(runtime, true);
        return;
    }
}

// 拉取并处理当前帧的 SDL 事件。
static void PumpSdlEvents(SdlAppRuntime& runtime) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        HandleSdlEvent(event, runtime);
    }
}

// 每帧同步窗口尺寸和浏览区尺寸，保证拖拽时实时刷新。
static void SyncBrowserResizeRealtime(SdlAppRuntime& runtime) {
    int window_width = 0;
    int window_height = 0;
    SDL_GetWindowSize(runtime.window, &window_width, &window_height);
    window_width = std::max(1, window_width);
    window_height = std::max(1, window_height);
    UpdateToolbarLayout(runtime.ui_state, window_width);
    const int browser_width = window_width;
    const int browser_height = GetBrowserViewportHeight(window_height);
    if (browser_width == runtime.last_sent_browser_width && browser_height == runtime.last_sent_browser_height) {
        return;
    }
    SendBrowserResizePacket(runtime, browser_width, browser_height);
    runtime.last_sent_browser_width = browser_width;
    runtime.last_sent_browser_height = browser_height;
}

// 同步 IME 候选框位置和浏览器光标样式。
static void SyncImeUiAndCursor(SdlAppRuntime& runtime) {
    if (!runtime.ime_view) {
        return;
    }
    auto* ime_state = reinterpret_cast<ImeUiState*>(runtime.ime_view);
    if (ime_state->magic != kImeUiMagic || ime_state->version != kImeUiVersion) {
        return;
    }
    const uint64_t cursor_seq_begin = ime_state->cursor_seq;
    if (cursor_seq_begin != runtime.last_cursor_seq) {
        MemoryBarrier();
        const uint64_t cursor_seq_end = ime_state->cursor_seq;
        if (cursor_seq_begin == cursor_seq_end) {
            if (ime_state->cursor_type == static_cast<uint32_t>(CT_NONE)) {
                SDL_HideCursor();
            } else {
                SDL_ShowCursor();
                runtime.browser_cursor_id = MapCefCursorToSdl(ime_state->cursor_type);
                if (!runtime.ui_cursor_override) {
                    SetCachedCursor(runtime, runtime.browser_cursor_id);
                }
            }
            runtime.last_cursor_seq = cursor_seq_end;
        }
    }
    const uint64_t seq_begin = ime_state->seq;
    MemoryBarrier();
    const uint64_t seq_end = ime_state->seq;
    if (seq_begin != seq_end) {
        return;
    }
    if (ime_state->visible == 0) {
        SDL_SetTextInputArea(runtime.window, nullptr, 0);
        return;
    }
    int window_width = 0;
    int window_height = 0;
    SDL_GetWindowSize(runtime.window, &window_width, &window_height);
    const int browser_height = GetBrowserViewportHeight(window_height);
    const float scale_x = runtime.browser_texture_width > 0 && window_width > 0 ? static_cast<float>(window_width) / static_cast<float>(runtime.browser_texture_width) : 1.0f;
    const float scale_y = runtime.browser_texture_height > 0 && browser_height > 0 ? static_cast<float>(browser_height) / static_cast<float>(runtime.browser_texture_height) : 1.0f;
    SDL_Rect area{};
    area.x = static_cast<int>(std::lround(static_cast<double>(ime_state->x) * scale_x));
    area.y = kToolbarHeight + static_cast<int>(std::lround(static_cast<double>(ime_state->y) * scale_y));
    area.w = std::max(1, static_cast<int>(std::lround(static_cast<double>(ime_state->w) * scale_x)));
    area.h = std::max(1, static_cast<int>(std::lround(static_cast<double>(ime_state->h) * scale_y)));
    int cursor_offset = static_cast<int>(std::lround(static_cast<double>(ime_state->cursor_x - ime_state->x) * scale_x));
    cursor_offset = std::clamp(cursor_offset, 0, area.w);
    SDL_SetTextInputArea(runtime.window, &area, cursor_offset);
}

// 同步 CEF 回写的 JS 桥接演示状态，让 SDL 状态栏实时展示最近一次交互结果。
static void SyncBridgeDemoStatus(SdlAppRuntime& runtime) {
    if (!runtime.bridge_demo_view) {
        return;
    }
    auto* state = reinterpret_cast<BridgeDemoState*>(runtime.bridge_demo_view);
    if (state->magic != kBridgeDemoMagic || state->version != kBridgeDemoVersion) {
        return;
    }
    const uint64_t seq_begin = state->seq;
    if (seq_begin == 0 || seq_begin == runtime.last_bridge_demo_seq) {
        return;
    }
    MemoryBarrier();
    const std::string status_text = state->status_text;
    const uint64_t seq_end = state->seq;
    if (seq_begin != seq_end) {
        return;
    }
    runtime.ui_state.status_text = status_text.empty() ? "桥接状态为空" : status_text;
    runtime.last_bridge_demo_seq = seq_end;
}

// 如果共享内存里有新帧，则上传浏览器纹理。
static void UploadFrameIfNeeded(SdlAppRuntime& runtime) {
    auto* header = reinterpret_cast<SharedFrameHeader*>(runtime.shared_view);
    if (!header || header->magic != kFrameMagic || header->version != kFrameVersion) {
        return;
    }
    const uint64_t frame_begin = header->frame_id;
    if (frame_begin == 0 || frame_begin == runtime.last_frame_id) {
        return;
    }
    const uint32_t width = header->width;
    const uint32_t height = header->height;
    const uint32_t stride = header->stride;
    const uint32_t pixel_bytes = header->pixel_bytes;
    const bool valid_size = width > 0 && height > 0 && width <= kMaxWidth && height <= kMaxHeight && stride == width * 4 && pixel_bytes == stride * height && pixel_bytes <= kMaxPixelBytes;
    if (!valid_size) {
        return;
    }
    runtime.browser_pixels.resize(pixel_bytes);
    const uint8_t* pixel_source = runtime.shared_view + sizeof(SharedFrameHeader);
    std::memcpy(runtime.browser_pixels.data(), pixel_source, pixel_bytes);
    MemoryBarrier();
    const uint64_t frame_end = header->frame_id;
    if (frame_begin != frame_end) {
        return;
    }
    glBindTexture(GL_TEXTURE_2D, runtime.browser_texture_id);
    if (runtime.browser_texture_width != static_cast<int>(width) || runtime.browser_texture_height != static_cast<int>(height)) {
        runtime.browser_texture_width = static_cast<int>(width);
        runtime.browser_texture_height = static_cast<int>(height);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, runtime.browser_texture_width, runtime.browser_texture_height, 0, GL_BGRA, GL_UNSIGNED_BYTE, runtime.browser_pixels.data());
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, runtime.browser_texture_width, runtime.browser_texture_height, GL_BGRA, GL_UNSIGNED_BYTE, runtime.browser_pixels.data());
    }
    runtime.last_frame_id = frame_end;
}

// 绘制浏览器内容纹理。
static void RenderBrowserTexture(const SdlAppRuntime& runtime) {
    if (runtime.browser_texture_id == 0) {
        return;
    }
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, runtime.browser_texture_id);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f);
    glVertex2f(-1.0f, 1.0f);
    glTexCoord2f(1.0f, 0.0f);
    glVertex2f(1.0f, 1.0f);
    glTexCoord2f(1.0f, 1.0f);
    glVertex2f(1.0f, -1.0f);
    glTexCoord2f(0.0f, 1.0f);
    glVertex2f(-1.0f, -1.0f);
    glEnd();
}

// 渲染整帧内容。
static void RenderOneFrame(SdlAppRuntime& runtime) {
    int window_width = 0;
    int window_height = 0;
    SDL_GetWindowSize(runtime.window, &window_width, &window_height);
    const int browser_height = GetBrowserViewportHeight(window_height);
    UpdateInputCaretBlink(runtime.ui_state, static_cast<uint64_t>(SDL_GetTicks()));
    glViewport(0, 0, window_width, window_height);
    glClearColor(0.08f, 0.08f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glViewport(0, 0, window_width, browser_height);
    RenderBrowserTexture(runtime);
    glViewport(0, 0, window_width, window_height);
    DrawToolbar(runtime.ui_state, runtime.ui_fonts, window_width, window_height);
    SDL_GL_SwapWindow(runtime.window);
}

// 回收运行时全部资源。
static void CleanupRuntime(SdlAppRuntime& runtime) {
    if (runtime.text_input_started && runtime.window) {
        SDL_StopTextInput(runtime.window);
        runtime.text_input_started = false;
    }
    if (runtime.input_pipe != INVALID_HANDLE_VALUE) {
        CloseHandle(runtime.input_pipe);
        runtime.input_pipe = INVALID_HANDLE_VALUE;
    }
    for (int index = 0; index < SDL_SYSTEM_CURSOR_COUNT; ++index) {
        if (runtime.cursor_cache[index]) {
            SDL_DestroyCursor(runtime.cursor_cache[index]);
            runtime.cursor_cache[index] = nullptr;
        }
    }
    if (runtime.browser_texture_id != 0) {
        glDeleteTextures(1, &runtime.browser_texture_id);
        runtime.browser_texture_id = 0;
    }
    CleanupUiFonts(runtime.ui_fonts);
    DestroyCefProcess(runtime.cef_process, runtime.cef_job);
    runtime.cef_job = nullptr;
    if (runtime.shared_view) {
        UnmapViewOfFile(runtime.shared_view);
        runtime.shared_view = nullptr;
    }
    if (runtime.shared_mapping) {
        CloseHandle(runtime.shared_mapping);
        runtime.shared_mapping = nullptr;
    }
    if (runtime.ime_view) {
        UnmapViewOfFile(runtime.ime_view);
        runtime.ime_view = nullptr;
    }
    if (runtime.ime_mapping) {
        CloseHandle(runtime.ime_mapping);
        runtime.ime_mapping = nullptr;
    }
    if (runtime.bridge_demo_view) {
        UnmapViewOfFile(runtime.bridge_demo_view);
        runtime.bridge_demo_view = nullptr;
    }
    if (runtime.bridge_demo_mapping) {
        CloseHandle(runtime.bridge_demo_mapping);
        runtime.bridge_demo_mapping = nullptr;
    }
    if (runtime.gl_context) {
        SDL_GL_DestroyContext(runtime.gl_context);
        runtime.gl_context = nullptr;
    }
    if (runtime.window) {
        SDL_DestroyWindow(runtime.window);
        runtime.window = nullptr;
    }
    if (runtime.ttf_started) {
        TTF_Quit();
        runtime.ttf_started = false;
    }
    SDL_Quit();
}

// SDL 主程序入口，负责窗口、共享内存、CEF 输入转发和工具栏渲染。
int RunSdlApp(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    SdlAppRuntime runtime;
    if (!InitializeWindowAndUi(runtime)) {
        CleanupRuntime(runtime);
        return -1;
    }
    if (!InitializeSharedMemory(runtime)) {
        CleanupRuntime(runtime);
        return -1;
    }
    if (!InitializeCefBridge(runtime)) {
        CleanupRuntime(runtime);
        return -1;
    }
    InitializeBrowserTexture(runtime);
    SDL_StartTextInput(runtime.window);
    runtime.text_input_started = true;
    while (runtime.is_running) {
        PumpSdlEvents(runtime);
        SyncBrowserResizeRealtime(runtime);
        SyncImeUiAndCursor(runtime);
        SyncBridgeDemoStatus(runtime);
        UploadFrameIfNeeded(runtime);
        RenderOneFrame(runtime);
    }
    CleanupRuntime(runtime);
    return 0;
}
