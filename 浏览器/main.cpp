#include "共享内存协议.h"
#include "输入事件协议.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include "include/internal/cef_types.h"

#include <windows.h>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

    constexpr int kInitWidth = 1280;
    constexpr int kInitHeight = 720;

    // 创建共享内存（浏览器进程作为创建者）
    bool CreateSharedFrameMemory(
        const std::wstring& name,
        size_t bytes,
        HANDLE& out_mapping,
        uint8_t*& out_view
    ) {
        out_mapping = CreateFileMappingW(
            INVALID_HANDLE_VALUE,
            nullptr,
            PAGE_READWRITE,
            0,
            static_cast<DWORD>(bytes),
            name.c_str());
        if (!out_mapping) {
            std::wcerr << L"[SDL] CreateFileMappingW 失败, err=" << GetLastError() << std::endl;
            return false;
        }

        out_view = static_cast<uint8_t*>(
            MapViewOfFile(out_mapping, FILE_MAP_ALL_ACCESS, 0, 0, bytes));
        if (!out_view) {
            std::wcerr << L"[SDL] MapViewOfFile 失败, err=" << GetLastError() << std::endl;
            CloseHandle(out_mapping);
            out_mapping = nullptr;
            return false;
        }

        // 初始化 header，避免 CEF 首帧前读端拿到脏数据
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

    bool CreateImeUiSharedMemory(
        const std::wstring& name,
        HANDLE& out_mapping,
        uint8_t*& out_view
    ) {
        const DWORD bytes = static_cast<DWORD>(sizeof(ImeUiState));
        out_mapping = CreateFileMappingW(
            INVALID_HANDLE_VALUE,
            nullptr,
            PAGE_READWRITE,
            0,
            bytes,
            name.c_str());
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

        auto* state = reinterpret_cast<ImeUiState*>(out_view);
        state->magic = kImeUiMagic;
        state->version = kImeUiVersion;
        state->visible = 0;
        state->seq = 0;
        state->x = state->y = state->w = state->h = 0;
        state->cursor_x = state->cursor_y = 0;
        state->cursor_type = static_cast<uint32_t>(CT_POINTER);
        state->cursor_seq = 0;
        return true;
    }

    // 启动 CEF.exe，并把共享内存名字传过去
    bool StartCefProcess(
        const std::wstring& shm_name,
        const std::wstring& input_pipe_name,
        const std::wstring& ime_shm_name,
        int width,
        int height,
        const std::wstring& url,
        PROCESS_INFORMATION& out_pi,
        HANDLE& out_job
    ) 
    {
        wchar_t module_path[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, module_path, MAX_PATH);
        std::filesystem::path exe_dir = std::filesystem::path(module_path).parent_path();
        std::filesystem::path cef_exe = exe_dir / L"CEF.exe";

        if (!std::filesystem::exists(cef_exe)) {
            std::wcerr << L"[SDL] 未找到 CEF.exe: " << cef_exe.wstring() << std::endl;
            return false;
        }

        std::wstring cmd =
            L"\"" + cef_exe.wstring() + L"\""
            L" --shm-name=" + shm_name +
            L" --width=" + std::to_wstring(width) +
            L" --height=" + std::to_wstring(height) +
            L" --input-pipe=" + input_pipe_name +
            L" --ime-shm-name=" + ime_shm_name +
            L" --url=" + url;

        STARTUPINFOW si{};
        si.cb = sizeof(si);
        ZeroMemory(&out_pi, sizeof(out_pi));

        std::vector<wchar_t> cmd_buffer(cmd.begin(), cmd.end());
        cmd_buffer.push_back(L'\0');

        const BOOL ok = CreateProcessW(
            nullptr,
            cmd_buffer.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            exe_dir.wstring().c_str(),
            &si,
            &out_pi);
        if (!ok) {
            std::wcerr << L"[SDL] CreateProcessW 启动 CEF 失败, err=" << GetLastError() << std::endl;
            return false;
        }

        // 用 JobObject 绑定子进程：主进程退出时自动清理 CEF 子进程
        out_job = CreateJobObjectW(nullptr, nullptr);
        if (out_job) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit_info{};
            limit_info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            if (SetInformationJobObject(
                out_job,
                JobObjectExtendedLimitInformation,
                &limit_info,
                sizeof(limit_info))) {
                AssignProcessToJobObject(out_job, out_pi.hProcess);
            }
        }

        std::wcout << L"[SDL] CEF 已启动, pid=" << out_pi.dwProcessId << std::endl;
        return true;
    }

    void DestroyCefProcess(PROCESS_INFORMATION& pi, HANDLE job) {
        if (pi.hThread) {
            CloseHandle(pi.hThread);
            pi.hThread = nullptr;
        }
        if (pi.hProcess) {
            CloseHandle(pi.hProcess);
            pi.hProcess = nullptr;
        }
        // 关闭 job 时，若子进程仍存活会被系统结束（KILL_ON_JOB_CLOSE）
        if (job) {
            CloseHandle(job);
        }
    }

    HANDLE ConnectInputPipeClient(const std::wstring& pipe_name, DWORD timeout_ms) {
        const DWORD start = GetTickCount();
        while (GetTickCount() - start < timeout_ms) {
            HANDLE pipe = CreateFileW(
                pipe_name.c_str(),
                GENERIC_WRITE,
                0,
                nullptr,
                OPEN_EXISTING,
                0,
                nullptr);
            if (pipe != INVALID_HANDLE_VALUE) {
                DWORD mode = PIPE_READMODE_MESSAGE;
                SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr);
                return pipe;
            }
            Sleep(20);
        }
        return INVALID_HANDLE_VALUE;
    }

    bool SendInputPacket(HANDLE pipe, const InputEventPacket& pkt) {
        if (pipe == INVALID_HANDLE_VALUE) {
            return false;
        }
        DWORD written = 0;
        return WriteFile(pipe, &pkt, sizeof(pkt), &written, nullptr) && written == sizeof(pkt);
    }

    uint32_t BuildCefModifiersFromSDL() {
        uint32_t flags = 0;

        SDL_Keymod mod = SDL_GetModState();
        if (mod & SDL_KMOD_SHIFT) flags |= EVENTFLAG_SHIFT_DOWN;
        if (mod & SDL_KMOD_CTRL)  flags |= EVENTFLAG_CONTROL_DOWN;
        if (mod & SDL_KMOD_ALT)   flags |= EVENTFLAG_ALT_DOWN;

        float mx = 0.0f;
        float my = 0.0f;
        SDL_MouseButtonFlags btn = SDL_GetMouseState(&mx, &my);
        if (btn & SDL_BUTTON_LMASK) flags |= EVENTFLAG_LEFT_MOUSE_BUTTON;
        if (btn & SDL_BUTTON_MMASK) flags |= EVENTFLAG_MIDDLE_MOUSE_BUTTON;
        if (btn & SDL_BUTTON_RMASK) flags |= EVENTFLAG_RIGHT_MOUSE_BUTTON;

        return flags;
    }

    uint32_t ToWinVkFromSDL(SDL_Keycode key) {
        // 先满足常见输入，后续可扩展完整映射表
        if (key >= SDLK_A && key <= SDLK_Z) {
            return static_cast<uint32_t>('A' + (key - SDLK_A));
        }
        if (key >= SDLK_0 && key <= SDLK_9) {
            return static_cast<uint32_t>('0' + (key - SDLK_0));
        }
        switch (key) {
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
        default: return static_cast<uint32_t>(key);
        }
    }

    // SDL3 滚轮数据统一转换为 CEF 所需整型 delta。
    // 优先使用 integer_*（SDL 内部已累积为整“刻度”），否则用 float 并保留残差。
    int BuildWheelDelta(float raw_delta,
                        int integer_delta,
                        SDL_MouseWheelDirection direction,
                        float& residual) {
        float delta = 0.0f;
        if (integer_delta != 0) {
            delta = static_cast<float>(integer_delta) * 120.0f;
        }
        else {
            delta = raw_delta * 120.0f;
        }

        if (direction == SDL_MOUSEWHEEL_FLIPPED) {
            delta = -delta;
        }

        delta += residual;
        int out = static_cast<int>(delta);
        residual = delta - static_cast<float>(out);

        // 避免高精度触控板的超小浮点量长期被截断成 0。
        if (out == 0 && delta != 0.0f) {
            out = (delta > 0.0f) ? 1 : -1;
            residual = 0.0f;
        }
        return out;
    }

    SDL_SystemCursor MapCefCursorToSdl(uint32_t cursor_type) {
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
        case CT_MIDDLE_PANNING_HORIZONTAL:
            return SDL_SYSTEM_CURSOR_MOVE;
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
        case CT_NOTALLOWED:
            return SDL_SYSTEM_CURSOR_NOT_ALLOWED;
        default:
            return SDL_SYSTEM_CURSOR_DEFAULT;
        }
    }
} // namespace

int main(int argc, char* argv[]) {
    // 1) 初始化 SDL
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "SDL_Init 失败: " << SDL_GetError() << std::endl;
        return -1;
    }

    // 2) 这里使用兼容模式，避免额外引入 GL 加载器（方便当前工程直接跑通）
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    SDL_Window* window = SDL_CreateWindow(
        "浏览器",
        kInitWidth,
        kInitHeight,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!window) {
        std::cerr << "SDL_CreateWindow 失败: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return -1;
    }

    SDL_GLContext gl_ctx = SDL_GL_CreateContext(window);
    if (!gl_ctx) {
        std::cerr << "SDL_GL_CreateContext 失败: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }
    SDL_GL_SetSwapInterval(1);

    // 3) 创建共享内存
    const size_t shm_bytes = sizeof(SharedFrameHeader) + kMaxPixelBytes;
    const std::wstring shm_name = L"Local\\CefSdlOsrFrame_" + std::to_wstring(GetCurrentProcessId());
    HANDLE shm_mapping = nullptr;
    uint8_t* shm_view = nullptr;
    if (!CreateSharedFrameMemory(shm_name, shm_bytes, shm_mapping, shm_view)) {
        SDL_GL_DestroyContext(gl_ctx);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }

    // 3.1) 创建 IME UI 共享内存（CEF 写 caret 区域，SDL 读并设置输入法候选位置）
    const std::wstring ime_shm_name = L"Local\\CefSdlImeUi_" + std::to_wstring(GetCurrentProcessId());
    HANDLE ime_mapping = nullptr;
    uint8_t* ime_view = nullptr;
    if (!CreateImeUiSharedMemory(ime_shm_name, ime_mapping, ime_view)) {
        UnmapViewOfFile(shm_view);
        CloseHandle(shm_mapping);
        SDL_GL_DestroyContext(gl_ctx);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }

    // 4) 启动 CEF 子进程（离屏渲染写共享内存）
    PROCESS_INFORMATION cef_pi{};
    HANDLE cef_job = nullptr;
    const std::wstring input_pipe_name = L"\\\\.\\pipe\\CefSdlInput_" + std::to_wstring(GetCurrentProcessId());
    if (!StartCefProcess(
        shm_name,
        input_pipe_name,
        ime_shm_name,
        kInitWidth,
        kInitHeight,
        L"https://www.baidu.com",
        cef_pi,
        cef_job)) {
        UnmapViewOfFile(ime_view);
        CloseHandle(ime_mapping);
        UnmapViewOfFile(shm_view);
        CloseHandle(shm_mapping);
        SDL_GL_DestroyContext(gl_ctx);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }
    HANDLE input_pipe = ConnectInputPipeClient(input_pipe_name, 8000);
    if (input_pipe == INVALID_HANDLE_VALUE) {
        std::wcerr << L"[SDL] 输入管道连接失败，页面将无法响应输入: " << input_pipe_name << std::endl;
    }

    // 5) OpenGL 纹理初始化
    GLuint tex_id = 0;
    glGenTextures(1, &tex_id);
    glBindTexture(GL_TEXTURE_2D, tex_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    uint64_t last_frame_id = 0;
    int tex_w = 0;
    int tex_h = 0;
    std::vector<uint8_t> local_pixels;
    float wheel_residual_x = 0.0f;
    float wheel_residual_y = 0.0f;
    uint64_t last_cursor_seq = 0;
    SDL_SystemCursor current_cursor_id = SDL_SYSTEM_CURSOR_DEFAULT;
    SDL_Cursor* cursor_cache[SDL_SYSTEM_CURSOR_COUNT] = {};

    uint64_t input_seq = 0;
    SDL_StartTextInput(window);

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }
            InputEventPacket pkt{};
            pkt.seq = ++input_seq;
            pkt.modifiers = BuildCefModifiersFromSDL();

            if (event.type == SDL_EVENT_MOUSE_MOTION) {
                pkt.type = static_cast<uint32_t>(InputEventType::MouseMove);
                pkt.x = static_cast<int>(event.motion.x);
                pkt.y = static_cast<int>(event.motion.y);
                SendInputPacket(input_pipe, pkt);
            }
            else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                pkt.type = static_cast<uint32_t>(InputEventType::MouseButton);
                pkt.x = static_cast<int>(event.button.x);
                pkt.y = static_cast<int>(event.button.y);
                pkt.mouse_up = (event.type == SDL_EVENT_MOUSE_BUTTON_UP) ? 1u : 0u;
                pkt.click_count = static_cast<uint32_t>(event.button.clicks);
                if (event.button.button == SDL_BUTTON_LEFT) pkt.mouse_button = static_cast<uint32_t>(MouseButtonType::Left);
                else if (event.button.button == SDL_BUTTON_MIDDLE) pkt.mouse_button = static_cast<uint32_t>(MouseButtonType::Middle);
                else if (event.button.button == SDL_BUTTON_RIGHT) pkt.mouse_button = static_cast<uint32_t>(MouseButtonType::Right);
                SendInputPacket(input_pipe, pkt);
            }
            else if (event.type == SDL_EVENT_MOUSE_WHEEL) {
                pkt.type = static_cast<uint32_t>(InputEventType::MouseWheel);
                pkt.x = static_cast<int>(event.wheel.mouse_x);
                pkt.y = static_cast<int>(event.wheel.mouse_y);
                pkt.delta_x = BuildWheelDelta(
                    event.wheel.x,
                    static_cast<int>(event.wheel.integer_x),
                    event.wheel.direction,
                    wheel_residual_x);
                pkt.delta_y = BuildWheelDelta(
                    event.wheel.y,
                    static_cast<int>(event.wheel.integer_y),
                    event.wheel.direction,
                    wheel_residual_y);
                if (pkt.delta_x != 0 || pkt.delta_y != 0) {
                    SendInputPacket(input_pipe, pkt);
                }
            }
            else if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) {
                pkt.type = static_cast<uint32_t>(InputEventType::Key);
                pkt.key_up = (event.type == SDL_EVENT_KEY_UP) ? 1u : 0u;
                pkt.key_code = ToWinVkFromSDL(event.key.key);
                pkt.native_key_code = pkt.key_code;
                SendInputPacket(input_pipe, pkt);
            }
            else if (event.type == SDL_EVENT_TEXT_INPUT) {
                pkt.type = static_cast<uint32_t>(InputEventType::Text);
                std::strncpy(pkt.text, event.text.text, sizeof(pkt.text) - 1);
                SendInputPacket(input_pipe, pkt);
            }
            else if (event.type == SDL_EVENT_WINDOW_RESIZED) {
                pkt.type = static_cast<uint32_t>(InputEventType::Resize);
                pkt.width = static_cast<uint32_t>(event.window.data1);
                pkt.height = static_cast<uint32_t>(event.window.data2);
                SendInputPacket(input_pipe, pkt);
            }
            else if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED || event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
                pkt.type = static_cast<uint32_t>(InputEventType::Focus);
                // 复用 key_up 字段：0=获取焦点，1=失去焦点
                pkt.key_up = (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) ? 1u : 0u;
                SendInputPacket(input_pipe, pkt);
            }
        }

        // 6.0) 同步 IME 候选框锚点：让输入法 UI 跟随网页输入光标
        if (ime_view) {
            auto* ime_state = reinterpret_cast<ImeUiState*>(ime_view);
            if (ime_state->magic == kImeUiMagic && ime_state->version == kImeUiVersion) {
                const uint64_t cursor_seq_begin = ime_state->cursor_seq;
                if (cursor_seq_begin != last_cursor_seq) {
                    MemoryBarrier();
                    const uint64_t cursor_seq_end = ime_state->cursor_seq;
                    if (cursor_seq_begin == cursor_seq_end) {
                        if (ime_state->cursor_type == static_cast<uint32_t>(CT_NONE)) {
                            SDL_HideCursor();
                        }
                        else {
                            SDL_ShowCursor();
                            const SDL_SystemCursor next_id = MapCefCursorToSdl(ime_state->cursor_type);
                            if (next_id != current_cursor_id) {
                                if (!cursor_cache[next_id]) {
                                    cursor_cache[next_id] = SDL_CreateSystemCursor(next_id);
                                }
                                if (cursor_cache[next_id]) {
                                    SDL_SetCursor(cursor_cache[next_id]);
                                    current_cursor_id = next_id;
                                }
                            }
                        }
                        last_cursor_seq = cursor_seq_end;
                    }
                }

                const uint64_t seq_begin = ime_state->seq;
                MemoryBarrier();
                const uint64_t seq_end = ime_state->seq;
                if (seq_begin == seq_end) {
                    if (ime_state->visible != 0) {
                        int win_w = 0, win_h = 0;
                        SDL_GetWindowSize(window, &win_w, &win_h);
                        const float scale_x = (tex_w > 0 && win_w > 0) ? (static_cast<float>(win_w) / static_cast<float>(tex_w)) : 1.0f;
                        const float scale_y = (tex_h > 0 && win_h > 0) ? (static_cast<float>(win_h) / static_cast<float>(tex_h)) : 1.0f;

                        SDL_Rect area{};
                        area.x = static_cast<int>(std::lround(static_cast<double>(ime_state->x) * scale_x));
                        area.y = static_cast<int>(std::lround(static_cast<double>(ime_state->y) * scale_y));
                        area.w = std::max(1, static_cast<int>(std::lround(static_cast<double>(ime_state->w) * scale_x)));
                        area.h = std::max(1, static_cast<int>(std::lround(static_cast<double>(ime_state->h) * scale_y)));
                        int cursor = static_cast<int>(std::lround(static_cast<double>(ime_state->cursor_x - ime_state->x) * scale_x));
                        if (cursor < 0) cursor = 0;
                        if (cursor > area.w) cursor = area.w;
                        SDL_SetTextInputArea(window, &area, cursor);
                    }
                    else {
                        SDL_SetTextInputArea(window, nullptr, 0);
                    }
                }
            }
        }

        // 6) 从共享内存读新帧（frame_id 变了才上传）
        auto* header = reinterpret_cast<SharedFrameHeader*>(shm_view);
        if (header->magic == kFrameMagic && header->version == kFrameVersion) {
            const uint64_t frame_begin = header->frame_id;
            if (frame_begin != 0 && frame_begin != last_frame_id) {
                const uint32_t w = header->width;
                const uint32_t h = header->height;
                const uint32_t pixel_bytes = header->pixel_bytes;
                const uint32_t stride = header->stride;

                const bool valid_size =
                    (w > 0 && h > 0 &&
                        w <= kMaxWidth &&
                        h <= kMaxHeight &&
                        stride == w * 4 &&
                        pixel_bytes == stride * h &&
                        pixel_bytes <= kMaxPixelBytes);

                if (valid_size) {
                    local_pixels.resize(pixel_bytes);
                    const uint8_t* pixel_src = shm_view + sizeof(SharedFrameHeader);
                    std::memcpy(local_pixels.data(), pixel_src, pixel_bytes);

                    // 二次读取 frame_id 做一致性校验，减少读写并发导致撕裂概率
                    MemoryBarrier();
                    const uint64_t frame_end = header->frame_id;
                    if (frame_begin == frame_end) {
                        glBindTexture(GL_TEXTURE_2D, tex_id);
                        if (tex_w != static_cast<int>(w) || tex_h != static_cast<int>(h)) {
                            tex_w = static_cast<int>(w);
                            tex_h = static_cast<int>(h);
                            glTexImage2D(
                                GL_TEXTURE_2D,
                                0,
                                GL_RGBA8,
                                tex_w,
                                tex_h,
                                0,
                                GL_BGRA,
                                GL_UNSIGNED_BYTE,
                                local_pixels.data());
                        }
                        else {
                            glTexSubImage2D(
                                GL_TEXTURE_2D,
                                0,
                                0,
                                0,
                                tex_w,
                                tex_h,
                                GL_BGRA,
                                GL_UNSIGNED_BYTE,
                                local_pixels.data());
                        }
                        last_frame_id = frame_end;
                    }
                }
            }
        }

        // 7) 绘制：把网页纹理画到整个窗口
        int win_w = 0;
        int win_h = 0;
        SDL_GetWindowSize(window, &win_w, &win_h);
        glViewport(0, 0, win_w, win_h);

        glClearColor(0.08f, 0.08f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, tex_id);

        // 使用固定管线绘制全屏四边形
        // 若出现上下颠倒，把 V 坐标 0/1 交换即可
        glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 0.0f); glVertex2f(-1.0f, 1.0f);
        glTexCoord2f(1.0f, 0.0f); glVertex2f(1.0f, 1.0f);
        glTexCoord2f(1.0f, 1.0f); glVertex2f(1.0f, -1.0f);
        glTexCoord2f(0.0f, 1.0f); glVertex2f(-1.0f, -1.0f);
        glEnd();

        SDL_GL_SwapWindow(window);
    }

    SDL_StopTextInput(window);
    if (input_pipe != INVALID_HANDLE_VALUE) {
        CloseHandle(input_pipe);
    }
    for (int i = 0; i < SDL_SYSTEM_CURSOR_COUNT; ++i) {
        if (cursor_cache[i]) {
            SDL_DestroyCursor(cursor_cache[i]);
            cursor_cache[i] = nullptr;
        }
    }
    // 8) 清理
    glDeleteTextures(1, &tex_id);

    DestroyCefProcess(cef_pi, cef_job);

    if (shm_view) {
        UnmapViewOfFile(shm_view);
        shm_view = nullptr;
    }
    if (shm_mapping) {
        CloseHandle(shm_mapping);
        shm_mapping = nullptr;
    }
    if (ime_view) {
        UnmapViewOfFile(ime_view);
        ime_view = nullptr;
    }
    if (ime_mapping) {
        CloseHandle(ime_mapping);
        ime_mapping = nullptr;
    }

    SDL_GL_DestroyContext(gl_ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
