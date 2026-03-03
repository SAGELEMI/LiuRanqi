#include "CEF调用.h"
#include "共享内存协议.h"
#include "输入事件协议.h"

#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/cef_browser.h"
#include "include/cef_v8.h"
#include "include/internal/cef_types.h"

#include <thread>
#include <atomic>
#include <algorithm>
#include <cstring>
#include <iostream>
namespace {
	// 从命令行读取整型开关（不存在则返回默认值）
	int ReadIntSwitch(CefRefPtr<CefCommandLine> cmd, const char* key, int default_value) {
		if (!cmd->HasSwitch(key)) {
			return default_value;
		}
		const std::wstring value = cmd->GetSwitchValue(key).ToWString();
		if (value.empty()) {
			return default_value;
		}
		return _wtoi(value.c_str());
	}
	// 从命令行读取字符串开关（不存在则返回默认值）
	std::wstring ReadWStringSwitch(CefRefPtr<CefCommandLine> cmd, const char* key, const std::wstring& default_value) {
		if (!cmd->HasSwitch(key)) {
			return default_value;
		}
		const std::wstring value = cmd->GetSwitchValue(key).ToWString();
		return value.empty() ? default_value : value;
	}
	// Renderer 侧执行 JS 并返回字符串结果（用于 ProcessMessage 演示）
	std::string EvalJsInRenderer(CefRefPtr<CefFrame> frame, const std::string& script) {
		if (!frame) {
			return "frame is null";
		}
		CefRefPtr<CefV8Context> ctx = frame->GetV8Context();
		if (!ctx.get()) {
			return "v8 context is null";
		}
		if (!ctx->Enter()) {
			return "v8 enter failed";
		}

		std::string out = "ok";
		CefRefPtr<CefV8Value> result;
		CefRefPtr<CefV8Exception> exception;
		const bool ok = ctx->Eval(script, frame->GetURL(), 0, result, exception);
		if (ok && result.get()) {
			if (result->IsString()) {
				out = result->GetStringValue().ToString();
			}
			else if (result->IsBool()) {
				out = result->GetBoolValue() ? "true" : "false";
			}
			else if (result->IsInt() || result->IsUInt()) {
				out = std::to_string(result->GetIntValue());
			}
			else if (result->IsDouble()) {
				out = std::to_string(result->GetDoubleValue());
			}
			else {
				out = "[js result: non-primitive]";
			}
		}
		else if (exception.get()) {
			out = "js exception: " + exception->GetMessage().ToString();
		}
		else {
			out = "js eval failed";
		}

		ctx->Exit();
		return out;
	}

	// 将 UTF-8 文本转换为 UTF-16（Windows wchar_t）供 KEYEVENT_CHAR 使用。
	std::wstring Utf8ToWide(const char* utf8, size_t max_len) {
		if (!utf8 || max_len == 0) {
			return L"";
		}
		size_t len = 0;
		while (len < max_len && utf8[len] != '\0') {
			++len;
		}
		if (len == 0) {
			return L"";
		}

		const int src_len = static_cast<int>(len);
		int wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, src_len, nullptr, 0);
		if (wlen <= 0) {
			// 某些输入法提交文本可能带非常规字节，回退到宽松转换。
			wlen = MultiByteToWideChar(CP_UTF8, 0, utf8, src_len, nullptr, 0);
			if (wlen <= 0) {
				return L"";
			}
		}

		std::wstring out(static_cast<size_t>(wlen), L'\0');
		MultiByteToWideChar(CP_UTF8, 0, utf8, src_len, out.data(), wlen);
		return out;
	}
}

CEFjihe::CEFjihe() = default;
CEFjihe::~CEFjihe() {
	CloseImeUiSharedMemory();
	CloseSharedMemory();
}

bool CEFjihe::InitSharedMemory(const std::wstring& shm_name, size_t total_size) {
	// CEF 进程作为“打开者”，共享内存由浏览器进程先创建
	shm_handle_ = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, shm_name.c_str());
	if (!shm_handle_) {
		std::wcerr << L"[CEF] OpenFileMappingW 失败, name=" << shm_name << L", err=" << GetLastError() << std::endl;
		return false;
	}
	shm_view_ = static_cast<uint8_t*>(MapViewOfFile(shm_handle_, FILE_MAP_ALL_ACCESS, 0, 0, total_size));
	if (!shm_view_) {
		std::wcerr << L"[CEF] MapViewOfFile 失败, err=" << GetLastError() << std::endl;
		CloseHandle(shm_handle_);
		shm_handle_ = nullptr;
		return false;
	}
	shm_name_ = shm_name;
	shm_size_ = total_size;
	std::wcout << L"[CEF] 共享内存已连接: " << shm_name_ << L", size=" << shm_size_ << std::endl;
	return true;
}
void CEFjihe::CloseSharedMemory() {
	if (shm_view_) {
		UnmapViewOfFile(shm_view_);
		shm_view_ = nullptr;
	}
	if (shm_handle_) {
		CloseHandle(shm_handle_);
		shm_handle_ = nullptr;
	}
	shm_size_ = 0;
}

bool CEFjihe::InitImeUiSharedMemory(const std::wstring& shm_name, size_t total_size) {
	ime_shm_handle_ = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, shm_name.c_str());
	if (!ime_shm_handle_) {
		std::wcerr << L"[CEF] IME OpenFileMappingW 失败, name=" << shm_name << L", err=" << GetLastError() << std::endl;
		return false;
	}

	ime_shm_view_ = static_cast<uint8_t*>(MapViewOfFile(ime_shm_handle_, FILE_MAP_ALL_ACCESS, 0, 0, total_size));
	if (!ime_shm_view_) {
		std::wcerr << L"[CEF] IME MapViewOfFile 失败, err=" << GetLastError() << std::endl;
		CloseHandle(ime_shm_handle_);
		ime_shm_handle_ = nullptr;
		return false;
	}

	ime_shm_name_ = shm_name;
	ime_shm_size_ = total_size;
	std::wcout << L"[CEF] IME共享内存已连接: " << ime_shm_name_ << L", size=" << ime_shm_size_ << std::endl;
	return true;
}

void CEFjihe::CloseImeUiSharedMemory() {
	if (ime_shm_view_) {
		UnmapViewOfFile(ime_shm_view_);
		ime_shm_view_ = nullptr;
	}
	if (ime_shm_handle_) {
		CloseHandle(ime_shm_handle_);
		ime_shm_handle_ = nullptr;
	}
	ime_shm_size_ = 0;
}

#pragma region CefApp
void CEFjihe::OnBeforeCommandLineProcessing(const CefString& process_type,
	CefRefPtr<CefCommandLine> command_line) {
	command_line->AppendSwitch("off-screen-rendering-enabled");// 开启离屏渲染（OSR）
	command_line->AppendSwitch("disable-gpu-compositing");// 禁用 GPU 合成，强制使用软件渲染
}
#pragma endregion

#pragma region CefClient
bool CEFjihe::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
	CefRefPtr<CefFrame> frame,
	CefProcessId source_process,
	CefRefPtr<CefProcessMessage> message) {
	const std::string name = message->GetName();

	// Renderer <- Browser：执行 JS 并回传结果
	if (source_process == PID_BROWSER && name == kMsgEvalJs) {
		const std::string script = message->GetArgumentList()->GetString(0).ToString();
		const std::string result_text = EvalJsInRenderer(frame, script);

		auto result_msg = CefProcessMessage::Create(kMsgJsResult);
		result_msg->GetArgumentList()->SetString(0, result_text);
		frame->SendProcessMessage(PID_BROWSER, result_msg);
		return true;
	}

	// Browser <- Renderer：打印 JS 结果
	if (source_process == PID_RENDERER && name == kMsgJsResult) {
		const std::string js_result = message->GetArgumentList()->GetString(0).ToString();
		std::cout << "[CEF][ProcessMessage] JS结果: " << js_result << std::endl;
		return true;
	}

	return false;
}
#pragma endregion

#pragma region CefLifeSpanHandler
void CEFjihe::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
	// 将新创建的浏览器添加到列表中
	browser_list_.push_back(browser);
	std::cout << "[CEF] 浏览器创建成功, id=" << browser->GetIdentifier() << std::endl;
}
void CEFjihe::OnBeforeClose(CefRefPtr<CefBrowser> browser)
{
	const int closing_id = browser ? browser->GetIdentifier() : -1;
	browser_list_.remove_if([closing_id](const CefRefPtr<CefBrowser>& b) {
		return b && b->GetIdentifier() == closing_id;
		});
	if (browser_list_.empty()) {
		std::cout << "[CEF] 所有浏览器已关闭, 退出消息循环" << std::endl;
		CefQuitMessageLoop();
	}
}
void CEFjihe::CloseAllBrowsers(bool force_close) {
	is_closing_ = true;
	if (!CefCurrentlyOn(TID_UI)) {
		//确保在UI线程上执行
		CefPostTask(TID_UI, base::BindOnce(&CEFjihe::CloseAllBrowsers, this, force_close));
		return;
	}
	if (browser_list_.empty()) {
		return;
	}
	for (auto& browser : browser_list_) {
		if (browser.get()) {
			browser->GetHost()->CloseBrowser(force_close);
		}
	}
}
#pragma endregion

#pragma region CefLoadHandler
void CEFjihe::OnLoadingStateChange(CefRefPtr<CefBrowser> browser,
	bool isLoading,
	bool canGoBack,
	bool canGoForward) {
	// Browser -> Renderer：页面加载完成后请求 renderer 执行 JS
	if (!isLoading && browser && browser->GetMainFrame()) {
		auto msg = CefProcessMessage::Create(kMsgEvalJs);
		msg->GetArgumentList()->SetString(0, "document.title");
		browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, msg);
	}
}
#pragma endregion

#pragma region CefDisplayHandler
bool CEFjihe::OnCursorChange(CefRefPtr<CefBrowser> browser,
	CefCursorHandle cursor,
	cef_cursor_type_t type,
	const CefCursorInfo& custom_cursor_info) {
	if (ime_shm_view_ && ime_shm_size_ >= sizeof(ImeUiState)) {
		auto* state = reinterpret_cast<ImeUiState*>(ime_shm_view_);
		state->cursor_type = static_cast<uint32_t>(type);
		MemoryBarrier();
		state->cursor_seq += 1;
	}
	// 光标实际显示在 SDL 进程处理，返回 true 阻止 CEF 默认处理。
	return true;
}
#pragma endregion

#pragma region CefRenderHandler（OSR 核心）
void CEFjihe::GetViewRect(CefRefPtr<CefBrowser> browser, CefRect& rect) {
	rect = CefRect(0, 0, view_w_, view_h_);
}
bool CEFjihe::GetScreenPoint(CefRefPtr<CefBrowser> browser,
	int viewX,
	int viewY,
	int& screenX,
	int& screenY) {
	// 当前架构下 SDL 窗口在另一个进程，CEF 无法直接拿到真实屏幕坐标。
	// 返回 false 让 CEF 按视图坐标处理；IME 候选框由 SDL_SetTextInputArea 精确控制。
	return false;
}
void CEFjihe::OnPaint(CefRefPtr<CefBrowser> browser,PaintElementType type,const RectList& dirtyRects,const void* buffer,int width,int height) {
	if (type != PET_VIEW || !buffer || !shm_view_) {
		return;
	}
	if (width <= 0 || height <= 0) {
		return;
	}
	if (width > static_cast<int>(kMaxWidth) || height > static_cast<int>(kMaxHeight)) {
		return;
	}

	const uint32_t stride = static_cast<uint32_t>(width) * 4;
	const uint32_t pixel_bytes = stride * static_cast<uint32_t>(height);
	if (pixel_bytes > kMaxPixelBytes) {
		return;
	}
	if (sizeof(SharedFrameHeader) + pixel_bytes > shm_size_) {
		return;
	}

	auto* header = reinterpret_cast<SharedFrameHeader*>(shm_view_);
	uint8_t* pixel_dst = shm_view_ + sizeof(SharedFrameHeader);

	// 先写元数据和像素
	header->magic = kFrameMagic;
	header->version = kFrameVersion;
	header->width = static_cast<uint32_t>(width);
	header->height = static_cast<uint32_t>(height);
	header->stride = stride;
	header->pixel_bytes = pixel_bytes;

	std::memcpy(pixel_dst, buffer, pixel_bytes);

	// 用 frame_id 作为“提交标记”，最后写入，读端据此判断新帧到达
	MemoryBarrier();
	header->frame_id = ++frame_counter_;
}

void CEFjihe::OnImeCompositionRangeChanged(CefRefPtr<CefBrowser> browser,
	const CefRange& selected_range,
	const RectList& character_bounds) {
	if (!ime_shm_view_ || ime_shm_size_ < sizeof(ImeUiState)) {
		return;
	}

	auto* state = reinterpret_cast<ImeUiState*>(ime_shm_view_);
	state->magic = kImeUiMagic;
	state->version = kImeUiVersion;

	if (character_bounds.empty()) {
		state->visible = 0;
		MemoryBarrier();
		state->seq = ++ime_seq_;
		return;
	}

	// 组合串候选位置使用“光标前一个字符”的右侧作为锚点更稳定。
	size_t idx = 0;
	const size_t caret_to = static_cast<size_t>(selected_range.to);
	if (caret_to > 0) {
		idx = caret_to - 1;
	}
	if (idx >= character_bounds.size()) {
		idx = character_bounds.size() - 1;
	}
	const CefRect& rect = character_bounds[idx];
	const int w = (rect.width > 0) ? rect.width : 1;
	const int h = (rect.height > 0) ? rect.height : 1;

	// 候选框更适合锚在字符矩形的下方，避免覆盖正在输入的文本。
	state->x = rect.x;
	state->y = rect.y + h;
	state->w = w;
	state->h = h;
	state->cursor_x = rect.x + w;
	state->cursor_y = rect.y + h;
	state->visible = 1;
	MemoryBarrier();
	state->seq = ++ime_seq_;
}
#pragma endregion

#pragma region 输入
bool CEFjihe::StartInputPipeServer(const std::wstring& pipe_name) {
	if (pipe_name.empty() || input_pipe_running_) {
		return false;
	}
	input_pipe_name_ = pipe_name;
	input_pipe_stop_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	if (!input_pipe_stop_event_) {
		return false;
	}

	input_pipe_running_ = true;
	input_pipe_thread_ = std::thread([this]() { InputPipeLoop(); });
	std::wcout << L"[CEF] 输入管道启动: " << input_pipe_name_ << std::endl;
	return true;
}

void CEFjihe::StopInputPipeServer() {
	if (!input_pipe_running_) {
		return;
	}
	input_pipe_running_ = false;
	if (input_pipe_stop_event_) {
		SetEvent(input_pipe_stop_event_);
	}
	if (input_pipe_thread_.joinable()) {
		input_pipe_thread_.join();
	}
	if (input_pipe_stop_event_) {
		CloseHandle(input_pipe_stop_event_);
		input_pipe_stop_event_ = nullptr;
	}
}

void CEFjihe::InputPipeLoop() {
	while (input_pipe_running_) {
		if (WaitForSingleObject(input_pipe_stop_event_, 0) == WAIT_OBJECT_0) {
			break;
		}

		HANDLE pipe = CreateNamedPipeW(
			input_pipe_name_.c_str(),
			PIPE_ACCESS_INBOUND,
			PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
			1, // 单实例即可
			sizeof(InputEventPacket),
			sizeof(InputEventPacket),
			1000,
			nullptr);

		if (pipe == INVALID_HANDLE_VALUE) {
			break;
		}

		BOOL connected = ConnectNamedPipe(pipe, nullptr)
			? TRUE
			: (GetLastError() == ERROR_PIPE_CONNECTED);
		if (!connected) {
			CloseHandle(pipe);
			continue;
		}

		while (input_pipe_running_) {
			InputEventPacket pkt{};
			DWORD read_bytes = 0;
			BOOL ok = ReadFile(pipe, &pkt, sizeof(pkt), &read_bytes, nullptr);
			if (!ok || read_bytes != sizeof(pkt)) {
				break;
			}
			if (pkt.magic != kInputMagic || pkt.version != kInputVersion) {
				continue;
			}

			// 管道线程不能直接调 BrowserHost，必须切 UI 线程
			CefPostTask(TID_UI, base::BindOnce(&CEFjihe::HandleInputPacketOnUI, this, pkt));
		}

		FlushFileBuffers(pipe);
		DisconnectNamedPipe(pipe);
		CloseHandle(pipe);
	}
}

CefBrowserHost::MouseButtonType CEFjihe::ToCefMouseButton(uint32_t btn) {
	switch (static_cast<MouseButtonType>(btn)) {
	case MouseButtonType::Left:
		return MBT_LEFT;
	case MouseButtonType::Middle:
		return MBT_MIDDLE;
	case MouseButtonType::Right:
		return MBT_RIGHT;
	default:
		return MBT_LEFT;
	}
}

void CEFjihe::HandleInputPacketOnUI(const InputEventPacket& pkt) {
	if (!CefCurrentlyOn(TID_UI)) {
		CefPostTask(TID_UI, base::BindOnce(&CEFjihe::HandleInputPacketOnUI, this, pkt));
		return;
	}
	if (browser_list_.empty()) {
		return;
	}

	CefRefPtr<CefBrowser> browser = browser_list_.front();
	if (!browser) {
		return;
	}
	CefRefPtr<CefBrowserHost> host = browser->GetHost();
	if (!host) {
		return;
	}

	CefMouseEvent mouse_event;
	mouse_event.x = pkt.x;
	mouse_event.y = pkt.y;
	mouse_event.modifiers = pkt.modifiers;

	switch (static_cast<InputEventType>(pkt.type)) {
	case InputEventType::MouseMove:
		host->SetFocus(true);
		host->SendMouseMoveEvent(mouse_event, false);
		break;

	case InputEventType::MouseButton:
		host->SetFocus(true);
		host->SendMouseClickEvent(
			mouse_event,
			ToCefMouseButton(pkt.mouse_button),
			pkt.mouse_up != 0,
			static_cast<int>(pkt.click_count));
		break;

	case InputEventType::MouseWheel:
		host->SetFocus(true);
		host->SendMouseWheelEvent(mouse_event, pkt.delta_x, pkt.delta_y);
		break;

	case InputEventType::Key: {
		host->SetFocus(true);
		CefKeyEvent key_event{};
		key_event.windows_key_code = static_cast<int>(pkt.key_code);
		key_event.native_key_code = static_cast<int>(pkt.native_key_code);
		key_event.modifiers = pkt.modifiers;
		key_event.type = pkt.key_up ? KEYEVENT_KEYUP : KEYEVENT_RAWKEYDOWN;
		host->SendKeyEvent(key_event);
		break;
	}

	case InputEventType::Text: {
		host->SetFocus(true);
		// Text 事件是 UTF-8，必须先转 UTF-16；逐字节发送会导致中文乱码。
		const std::wstring wide_text = Utf8ToWide(pkt.text, sizeof(pkt.text));
		for (wchar_t ch : wide_text) {
			CefKeyEvent char_event{};
			char_event.type = KEYEVENT_CHAR;
			char_event.windows_key_code = static_cast<int>(ch);
			char_event.native_key_code = char_event.windows_key_code;
			char_event.character = static_cast<char16_t>(ch);
			char_event.unmodified_character = static_cast<char16_t>(ch);
			char_event.modifiers = pkt.modifiers;
			host->SendKeyEvent(char_event);
		}
		break;
	}

	case InputEventType::Resize:
		view_w_ = static_cast<int>(pkt.width);
		view_h_ = static_cast<int>(pkt.height);
		host->WasResized();
		break;

	case InputEventType::Focus:
		host->SetFocus(pkt.key_up == 0);
		if (pkt.key_up != 0 && ime_shm_view_ && ime_shm_size_ >= sizeof(ImeUiState)) {
			auto* state = reinterpret_cast<ImeUiState*>(ime_shm_view_);
			state->visible = 0;
			MemoryBarrier();
			state->seq = ++ime_seq_;
		}
		break;

	default:
		break;
	}
}
#pragma endregion

int CEFjihe::运行(int argc, char* argv[], void* sandbox_info) {
#if CEF_API_VERSION != CEF_EXPERIMENTAL
	std::cout << "使用 CEF API 版本: " << CEF_API_VERSION << std::endl;
#endif

	CefMainArgs main_args(GetModuleHandle(nullptr));

	// 关键：子进程执行必须传入 app，确保 Renderer 回调可用
	CefRefPtr<CEFjihe> app(new CEFjihe);
	const int exit_code = CefExecuteProcess(main_args, app, sandbox_info);
	if (exit_code >= 0) {
		return exit_code;
	}

	// 读取命令行参数（来自 浏览器.exe）
	CefRefPtr<CefCommandLine> cmd = CefCommandLine::CreateCommandLine();
	cmd->InitFromString(::GetCommandLineW());
	app->view_w_ = std::clamp(ReadIntSwitch(cmd, "width", 1280), 64, static_cast<int>(kMaxWidth));
	app->view_h_ = std::clamp(ReadIntSwitch(cmd, "height", 720), 64, static_cast<int>(kMaxHeight));
	app->startup_url_ = CefString(ReadWStringSwitch(cmd, "url", L"https://www.baidu.com"));
	app->shm_name_ = ReadWStringSwitch(cmd, "shm-name", L"");
	app->ime_shm_name_ = ReadWStringSwitch(cmd, "ime-shm-name", L"");

	app->input_pipe_name_ = ReadWStringSwitch(cmd, "input-pipe", L"");
	if (!app->input_pipe_name_.empty()) {
		app->StartInputPipeServer(app->input_pipe_name_);
	}

	CefSettings settings;
	if (!sandbox_info) {
		settings.no_sandbox = true;
	}
	settings.windowless_rendering_enabled = true; // 必须开启：离屏渲染

	if (!CefInitialize(main_args, settings, app, sandbox_info)) {
		return CefGetExitCode();
	}

	// 连接共享内存（浏览器进程先创建）
	const size_t shm_bytes = sizeof(SharedFrameHeader) + kMaxPixelBytes;
	if (!app->shm_name_.empty()) {
		app->InitSharedMemory(app->shm_name_, shm_bytes);
	}
	else {
		std::cout << "[CEF] 未收到 --shm-name，OnPaint 数据将不会输出到共享内存" << std::endl;
	}
	if (!app->ime_shm_name_.empty()) {
		app->InitImeUiSharedMemory(app->ime_shm_name_, sizeof(ImeUiState));
	}
	else {
		std::cout << "[CEF] 未收到 --ime-shm-name，IME 候选框位置不会同步到 SDL" << std::endl;
	}

	CefWindowInfo window_info;
	window_info.SetAsWindowless(nullptr); // 必须：OSR 模式

	CefBrowserSettings browser_settings;
	browser_settings.windowless_frame_rate = 60; // 提高刷新稳定性

	std::cout << "[CEF] 创建离屏浏览器, url=" << app->startup_url_.ToString()
		<< ", size=" << app->view_w_ << "x" << app->view_h_ << std::endl;

	CefBrowserHost::CreateBrowserSync(
		window_info,
		app,
		app->startup_url_,
		browser_settings,
		nullptr,
		nullptr);

	CefRunMessageLoop();

	app->StopInputPipeServer();
	app->CloseImeUiSharedMemory();
	app->CloseSharedMemory();
	CefShutdown();
	return 0;
}
