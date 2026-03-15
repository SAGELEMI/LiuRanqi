#include "CEFCall.h"
#include "common/ShmemProtocol.h"

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
		// 首先检查命令行是否包含指定的开关，如果没有则直接返回默认值，避免后续调用 GetSwitchValue 时出错。
		if (!cmd->HasSwitch(key)) {
			return default_value;
		}
		// 获取开关的值，注意这里的 GetSwitchValue 返回的是 CefString 类型，我们需要把它转换为 std::wstring 才能使用。这里的 value 可能是空字符串，如果用户传入了 --key=（等号后没有值）或者 --key（没有等号和值）都可能导致 value 为空，所以我们需要检查一下，如果 value 为空则返回默认值。
		const std::wstring value = cmd->GetSwitchValue(key).ToWString();
		// 注意这里的 value 可能是空字符串，如果用户传入了 --key = （等号后没有值）或者 --key（没有等号和值）都可能导致 value 为空，所以我们需要检查一下，如果 value 为空则返回默认值。
		if (value.empty()) {
			return default_value;
		}
		// 注意这里的 value 可能是空字符串，如果用户传入了 --key=（等号后没有值）或者 --key（没有等号和值）都可能导致 value 为空，所以我们需要检查一下，如果 value 为空则返回默认值。
		return _wtoi(value.c_str());
	}

	// 从命令行读取字符串开关（不存在则返回默认值）
	std::wstring ReadWStringSwitch(CefRefPtr<CefCommandLine> cmd, const char* key, const std::wstring& default_value) {
		// 首先检查命令行是否包含指定的开关，如果没有则直接返回默认值，避免后续调用 GetSwitchValue 时出错。
		if (!cmd->HasSwitch(key)) {
			return default_value;
		}
		// 获取开关的值，注意这里的 GetSwitchValue 返回的是 CefString 类型，我们需要把它转换为 std::wstring 才能使用。这里的 value 可能是空字符串，如果用户传入了 --key=（等号后没有值）或者 --key（没有等号和值）都可能导致 value 为空，所以我们需要检查一下，如果 value 为空则返回默认值。
		const std::wstring value = cmd->GetSwitchValue(key).ToWString();
		// 注意这里的 value 可能是空字符串，如果用户传入了 --key=（等号后没有值）或者 --key（没有等号和值）都可能导致 value 为空，所以我们需要检查一下，如果 value 为空则返回默认值。
		return value.empty() ? default_value : value;
	}

	// Renderer 侧执行 JS 并返回字符串结果（用于 ProcessMessage 演示）
	std::string EvalJsInRenderer(CefRefPtr<CefFrame> frame, const std::string& script) {
		// 首先检查传入的 frame 是否有效，如果 frame 是 null 则无法执行 JS 脚本，所以直接返回一个错误提示文本。
		if (!frame) {
			return "frame is null";
		}
		// 获取当前网页的 V8 上下文，注意这个上下文是与当前网页绑定的，如果当前网页没有加载完成或者没有绑定 V8 上下文则可能返回 null。
		CefRefPtr<CefV8Context> ctx = frame->GetV8Context();
		if (!ctx.get()) {
			return "v8 context is null";
		}
		if (!ctx->Enter()) {
			return "v8 enter failed";
		}
		// 默认结果是 "ok"，如果 Eval 成功但没有结果或者结果类型不受支持则返回这个默认值。
		std::string out = "ok";
		// Eval JS 脚本的结果会保存在这个 CefV8Value 对象中，注意它是一个智能指针，所以我们不需要手动释放它。
		CefRefPtr<CefV8Value> result;
		// Eval 的时候可能会抛出 JS 异常，所以我们准备一个 CefV8Exception 对象来接收异常信息。
		CefRefPtr<CefV8Exception> exception;
		// Eval JS 脚本，传入当前网页 URL 作为脚本来源，行号和列号都设置为 0（表示未知位置）。Eval 成功后 result 会包含 JS 执行结果，如果执行过程中发生异常则 exception 会包含异常信息。
		const bool ok = ctx->Eval(script, frame->GetURL(), 0, result, exception);
		if (ok && result.get()) {
			// Eval 成功并且有结果，尝试把结果转换为字符串返回给调用者。这里只处理了几种常见的 JS 基本类型，其他类型则返回一个提示文本。
			if (result->IsString()) {
				// 结果是字符串，直接返回它的值。
				out = result->GetStringValue().ToString();
			}
			else if (result->IsBool()) {
				// 结果是布尔值，返回 "true" 或 "false"。
				out = result->GetBoolValue() ? "true" : "false";
			}
			else if (result->IsInt() || result->IsUInt()) {
				// 结果是整数，返回它的字符串表示。
				out = std::to_string(result->GetIntValue());
			}
			else if (result->IsDouble()) {
				// 结果是浮点数，返回它的字符串表示。
				out = std::to_string(result->GetDoubleValue());
			}
			else {
				// 结果是其他类型（比如对象、数组、函数等），返回一个提示文本。
				out = "[js result: non-primitive]";
			}
		}
		else if (exception.get()) {
			// Eval 失败并且有异常信息，返回异常的消息文本。
			out = "js exception: " + exception->GetMessage().ToString();
		}
		else {
			// Eval 失败但没有异常信息，可能是 V8 内部错误或者其他原因。
			out = "js eval failed";
		}
		// 注意 Eval 之后必须退出 V8 上下文，否则会导致后续 JS 调用失败。
		ctx->Exit();
		return out;
	}

	// 将 UTF-8 文本转换为 UTF-16（Windows wchar_t）供 KEYEVENT_CHAR 使用。
	std::wstring Utf8ToWide(const char* utf8, size_t max_len) {
		// 如果输入文本为空或长度为 0，则直接返回空字符串，避免调用 MultiByteToWideChar 时出错。
		if (!utf8 || max_len == 0) {
			return L"";
		}
		size_t len = 0;
		// 计算输入文本的实际长度，确保不超过 max_len，并且以 null 结尾。注意这里的 len 是字节数，因为 UTF-8 是变长编码，一个字符可能占用多个字节。
		while (len < max_len && utf8[len] != '\0') {
			++len;
		}
		if (len == 0) {
			return L"";
		}
		// 注意 MultiByteToWideChar 的输入长度是以字节为单位的，而不是以字符为单位的，所以我们直接用 len（字节数）来调用它。
		const int src_len = static_cast<int>(len);
		// 首先尝试严格转换，如果输入文本包含无效 UTF-8 字节则转换会失败并返回 0，这时候我们再尝试宽松转换，确保尽可能转换出文本来。
		int wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, src_len, nullptr, 0);
		if (wlen <= 0) {
			// 某些输入法提交文本可能带非常规字节，回退到宽松转换。
			wlen = MultiByteToWideChar(CP_UTF8, 0, utf8, src_len, nullptr, 0);
			if (wlen <= 0) {
				return L"";
			}
		}
		// 分配足够的空间来存储转换后的宽字符文本，注意 wlen 是字符数，不是字节数，所以直接用 wlen 来构造 std::wstring。
		std::wstring out(static_cast<size_t>(wlen), L'\0');
		// 先尝试严格转换，如果失败了再尝试宽松转换，确保尽可能转换出文本来。
		MultiByteToWideChar(CP_UTF8, 0, utf8, src_len, out.data(), wlen);
		return out;
	}

	// 把 UTF-8 文本安全转成单引号 JS 字符串字面量。
	std::string EscapeForSingleQuotedJsString(const std::string& text) {
		std::string escaped;
		// 预先分配足够的空间，避免多次扩容。这里假设平均每个字符需要转义一次（实际可能更少），所以在原长度基础上加一些余量。
		escaped.reserve(text.size() + 16);
		// 转义单引号、反斜杠和常见控制字符，其他字符原样保留。
		for (const char ch : text) {
			switch (ch) {
			case '\\': escaped += "\\\\"; break;
			case '\'': escaped += "\\'"; break;
			case '\r': escaped += "\\r"; break;
			case '\n': escaped += "\\n"; break;
			case '\t': escaped += "\\t"; break;
			default: escaped.push_back(ch); break;
			}
		}
		return escaped;
	}

	// Renderer 侧暴露给网页的 JS -> C++ 桥接函数。
	class DemoNativeCallHandler final : public CefV8Handler {
	public:
		/// <summary>
		///  JS 调用时自动进入 Execute ()
		/// </summary>
		/// <param name="name">函数名</param>
		/// <param name="object">函数所属对象（通常是 window）</param>
		/// <param name="arguments">JS 传入的参数</param>
		/// <param name="retval">输出参数：要返回给 JS 的结果</param>
		/// <param name="exception">输出参数：要返回给 JS 的异常（如果有）</param>
		/// <returns></returns>
		bool Execute(const CefString& name,
			CefRefPtr<CefV8Value> object,
			const CefV8ValueList& arguments,
			CefRefPtr<CefV8Value>& retval,
			CefString& exception) override {
			(void)object;
			// 这里只处理名为 "nativeCall" 的函数，其他函数名返回 false 表示未处理。
			if (name != "nativeCall") {
				return false;
			}
			// 从 JS 传入的参数列表中获取第一个参数作为消息内容，如果没有传入或类型不对则使用默认文本。
			const std::string payload = !arguments.empty() && arguments[0] && arguments[0]->IsString()
				? arguments[0]->GetStringValue().ToString()
				: "JS 未传入文本";
			// 获取当前 V8 上下文和对应的 Frame 对象，如果获取失败则返回异常。
			CefRefPtr<CefV8Context> context = CefV8Context::GetCurrentContext();
			// 注意：在某些情况下（比如页面正在卸载时）上下文可能已经不存在了，这时候 GetFrame() 会返回 nullptr。
			CefRefPtr<CefFrame> frame = context ? context->GetFrame() : nullptr;
			if (!frame) {
				exception = "frame is null";
				return true;
			}
			// 创建一个新的 CEF 进程消息，消息名为 kMsgJsInvokeNative（这是我们自定义的消息名，用于标识这是 JS 调用 C++ 的消息）。
			auto message = CefProcessMessage::Create(kMsgJsInvokeNative);
			// 把 JS 传入的参数（payload）放到消息里，索引 0 是第一个参数。
			message->GetArgumentList()->SetString(0, payload);
			// 这里发送到浏览器进程，消息会在浏览器进程的 CEFjihe::OnProcessMessageReceived() 里被接收和处理。
			frame->SendProcessMessage(PID_BROWSER, message);
			// 给 JS 返回一个字符串结果，表示消息已发送（注意这只是表示消息已发送到浏览器进程，并不代表浏览器进程已经处理了这个消息）。
			retval = CefV8Value::CreateString("native message sent");
			return true;
		}

	private:
		// 由于 CefV8Handler 是通过 CEF 内部的引用计数机制来管理生命周期的，所以需要实现这个宏来提供 AddRef()、Release() 和 QueryInterface() 方法。
		IMPLEMENT_REFCOUNTING(DemoNativeCallHandler);
	};
}

CEFjihe::CEFjihe() = default;

CEFjihe::~CEFjihe() {
	// 关闭JS演示共享内存，释放资源
	CloseSharedMemory(bridge_demo_shm_view_, bridge_demo_shm_handle_, bridge_demo_shm_size_);
	// 关闭UI操作状态共享内存，释放资源
	CloseSharedMemory(ime_shm_view_, ime_shm_handle_, ime_shm_size_);
	// 关闭浏览器视图共享内存，释放资源
	CloseSharedMemory(shm_view_, shm_handle_, shm_size_);
}

#pragma region CefApp

void CEFjihe::OnBeforeCommandLineProcessing(
	const CefString& process_type,
	CefRefPtr<CefCommandLine> command_line) {
	// 开启离屏渲染（OSR）
	command_line->AppendSwitch("off-screen-rendering-enabled");
	// 禁用 GPU 合成，强制使用软件渲染
	command_line->AppendSwitch("disable-gpu-compositing");
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

	// Renderer <- Browser：把 Browser 处理后的结果回调给页面里的 JS。
	if (source_process == PID_BROWSER && name == kMsgNativeResult) {
		const std::string native_result = message->GetArgumentList()->GetString(0).ToString();
		const std::string escaped = EscapeForSingleQuotedJsString(native_result);
		EvalJsInRenderer(frame, "if (window.onNativeResult) window.onNativeResult('" + escaped + "'); 'native result applied';");
		return true;
	}

	// Browser <- Renderer：打印 JS 结果
	if (source_process == PID_RENDERER && name == kMsgJsResult) {
		const std::string js_result = message->GetArgumentList()->GetString(0).ToString();
		std::cout << "[CEF][ProcessMessage] JS结果: " << js_result << std::endl;
		WriteBridgeDemoStatus("C++ -> JS 成功，JS返回: " + js_result);
		return true;
	}

	// Browser <- Renderer：网页里的 JS 主动调用本地 C++。
	if (source_process == PID_RENDERER && name == kMsgJsInvokeNative) {
		const std::string js_payload = message->GetArgumentList()->GetString(0).ToString();
		const std::string native_result = "C++收到JS消息: " + js_payload;
		std::cout << "[CEF][ProcessMessage] JS调用C++: " << js_payload << std::endl;
		WriteBridgeDemoStatus("JS -> C++ 成功，收到: " + js_payload);
		auto result_msg = CefProcessMessage::Create(kMsgNativeResult);
		result_msg->GetArgumentList()->SetString(0, native_result);
		if (browser && browser->GetMainFrame()) {
			browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, result_msg);
		}
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
	// 拿到正在关闭的浏览器 ID（每个浏览器唯一 ID）
	const int closing_id = browser ? browser->GetIdentifier() : -1;
	// 从浏览器列表中移除这个正在关闭的浏览器，ID 精准匹配，不会删错
	browser_list_.remove_if([closing_id](const CefRefPtr<CefBrowser>& b) {
		return b && b->GetIdentifier() == closing_id;
		});
	// 所有浏览器都关完了，关闭退出
	if (browser_list_.empty()) {
		is_closing_ = false;
		std::cout << "[CEF] 所有浏览器已关闭, 退出消息循环" << std::endl;
		CefQuitMessageLoop();
	}
}

void CEFjihe::CloseAllBrowsers(bool force_close) {
	// 确保在UI线程上执行
	if (!CefCurrentlyOn(TID_UI)) {
		CefPostTask(TID_UI, base::BindOnce(&CEFjihe::CloseAllBrowsers, this, force_close));
		return;
	}
	// 没有浏览器，直接返回
	if (browser_list_.empty()) {
		return;
	}
	// 防止重复关闭
	if (is_closing_) {
		return;
	}
	is_closing_ = true;
	// 遍历关闭所有浏览器
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
	// Browser -> Renderer：只有【加载完成】+ 浏览器有效 + 主框架存在，才执行
	if (!isLoading && browser && browser->GetMainFrame()) {
		WriteBridgeDemoStatus("页面加载完成，可以测试 C++ <-> JS 双向通信");
		// 创建一条进程间消息
		auto msg = CefProcessMessage::Create(kMsgEvalJs);
		// 把要执行的 JS 代码 放进消息
		msg->GetArgumentList()->SetString(0, "document.title");
		// 把消息发给 JS 渲染进程，让渲染进程执行这段 JS
		browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, msg);
	}
}

#pragma endregion

#pragma region CefRenderProcessHandler

void CEFjihe::OnContextCreated(CefRefPtr<CefBrowser> browser,
	CefRefPtr<CefFrame> frame,
	CefRefPtr<CefV8Context> context) {
	// 写 (void) 是为了防止编译器报未使用变量警告，不影响功能
	(void)browser;
	(void)frame;
	//  获取 JS 的 window 对象，所有全局函数、变量都挂在它上面
	CefRefPtr<CefV8Value> global = context ? context->GetGlobal() : nullptr;
	// 安全判断：window 对象无效就直接返回
	if (!global) {
		return;
	}
	// 给 window 注册一个函数名叫 nativeCall
	// 这个函数绑定到 C++ 类 DemoNativeCallHandler
	// JS 调用 nativeCall() 时，C++ 的 handler 会收到消息
	global->SetValue("nativeCall",
		CefV8Value::CreateFunction("nativeCall", new DemoNativeCallHandler()),
		V8_PROPERTY_ATTRIBUTE_NONE);
}

#pragma endregion

#pragma region CefDisplayHandler

bool CEFjihe::OnCursorChange(CefRefPtr<CefBrowser> browser,
	CefCursorHandle cursor,
	cef_cursor_type_t type,
	const CefCursorInfo& custom_cursor_info) {
	// 检查共享内存是否有效
	if (ime_shm_view_ && ime_shm_size_ >= sizeof(ImeUiState)) {
		// 把光标类型（箭头 / 手型 / 文本框 / 等待）写入共享内存
		auto* state = reinterpret_cast<ImeUiState*>(ime_shm_view_);
		state->cursor_type = static_cast<uint32_t>(type);
		// 内存屏障
		MemoryBarrier();
		// 更新序号递增，确保状态更新的可见性；读端据此判断状态变化。
		state->cursor_seq += 1;
	}
	// 光标实际显示在 SDL 进程处理，返回 true 阻止 CEF 默认处理。
	return true;
}

#pragma endregion

#pragma region CefRenderHandler（OSR 核心）

void CEFjihe::GetViewRect(CefRefPtr<CefBrowser> browser, CefRect& rect) {
	// 这里直接返回离屏渲染的尺寸，CEF 会按照这个尺寸来渲染页面。实际应用中可以根据需要动态调整。
	rect = CefRect(0, 0, view_w_, view_h_);
}

void CEFjihe::OnPaint(CefRefPtr<CefBrowser> browser,
	PaintElementType type,
	const RectList& dirtyRects,
	const void* buffer,
	int width,
	int height) {
	// 安全过滤（只处理主窗口渲染）
	if (type != PET_VIEW || !buffer || !shm_view_) {
		return;
	}

	// 宽高合法性检查，防止异常尺寸导致崩溃
	if (width <= 0 || height <= 0) {
		return;
	}
	if (width > static_cast<int>(kMaxWidth) || height > static_cast<int>(kMaxHeight)) {
		return;
	}

	// 计算一行像素大小 & 总像素大小
	const uint32_t stride = static_cast<uint32_t>(width) * 4;
	const uint32_t pixel_bytes = stride * static_cast<uint32_t>(height);
	if (pixel_bytes > kMaxPixelBytes) {
		return;
	}

	// 检查共享内存够不够放下 头信息 + 图片
	if (sizeof(SharedFrameHeader) + pixel_bytes > shm_size_) {
		return;
	}

	// 填写共享内存头信息
	auto* header = reinterpret_cast<SharedFrameHeader*>(shm_view_);
	uint8_t* pixel_dst = shm_view_ + sizeof(SharedFrameHeader);

	// 先写元数据和像素
	header->magic = kFrameMagic;
	header->version = kFrameVersion;
	header->width = static_cast<uint32_t>(width);
	header->height = static_cast<uint32_t>(height);
	header->stride = stride;
	header->pixel_bytes = pixel_bytes;

	// 把 CEF 渲染的画面拷贝到共享内存
	std::memcpy(pixel_dst, buffer, pixel_bytes);
	// 内存屏障
	MemoryBarrier();
	// 用 frame_id 作为“提交标记”，最后写入，读端据此判断新帧到达
	header->frame_id = ++frame_counter_;
}

void CEFjihe::OnImeCompositionRangeChanged(CefRefPtr<CefBrowser> browser,
	const CefRange& selected_range,
	const RectList& character_bounds) {
	// 检查共享内存是否有效，无效直接返回，避免崩溃
	if (!ime_shm_view_ || ime_shm_size_ < sizeof(ImeUiState)) {
		return;
	}

	if (character_bounds.empty()) {
		// Chromium 在组合态切换时可能短暂上报空 bounds，这里保留上一次有效锚点，避免候选框跳回左上角。
		if (ime_composition_active_ && ime_last_rect_valid_) {
			PublishImeUiState(
				ime_last_rect_.x,
				ime_last_rect_.y,
				ime_last_rect_.width,
				ime_last_rect_.height,
				ime_last_rect_.x + ime_last_rect_.width,
				ime_last_rect_.y + ime_last_rect_.height
			);
			return;
		}
		ClearImeUiState();
		return;
	}

	// 组合串候选位置使用“光标前一个字符”的右侧作为锚点更稳定。
	size_t idx = 0;
	const size_t caret_to = selected_range.to > 0 ? static_cast<size_t>(selected_range.to) : 0;
	if (caret_to > 0) {
		idx = caret_to - 1;
	}
	if (idx >= character_bounds.size()) {
		idx = character_bounds.size() - 1;
	}
	const CefRect& rect = character_bounds[idx];

	// 宽高至少为 1，避免网页异常导致坐标错误
	const int w = (rect.width > 0) ? rect.width : 1;
	const int h = (rect.height > 0) ? rect.height : 1;
	ime_last_rect_ = CefRect(rect.x, rect.y + h, w, h);
	ime_last_rect_valid_ = true;

	// 候选框更适合锚在字符矩形的下方，避免覆盖正在输入的文本。
	PublishImeUiState(rect.x, rect.y + h, w, h, rect.x + w, rect.y + h);
}

#pragma endregion

#pragma region 输入事件

bool CEFjihe::StartInputPipeServer(const std::wstring& pipe_name) {
	// 确保在 UI 线程上执行，避免与输入事件处理线程冲突。
	if (pipe_name.empty() || input_pipe_running_) {
		return false;
	}
	// 存储管道名称，创建事件对象用于线程控制。
	input_pipe_name_ = pipe_name;
	// 创建一个手动关闭的事件对象，初始状态为非信号状态，用于通知输入管道线程停止。
	input_pipe_stop_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	// 关闭事件对象失败则返回，避免后续线程无法正确控制。
	if (!input_pipe_stop_event_) {
		return false;
	}
	// 设置运行控制标志，启动输入管道线程。
	input_pipe_running_ = true;
	input_pipe_thread_ = std::thread([this]() { InputPipeLoop(); });
	std::wcout << L"[CEF] 输入管道启动: " << input_pipe_name_ << std::endl;
	return true;
}

void CEFjihe::StopInputPipeServer() {
	// 确保在 UI 线程上执行，避免与输入事件处理线程冲突。
	if (!input_pipe_running_) {
		return;
	}
	// 设置运行控制标志，通知线程退出循环。
	input_pipe_running_ = false;
	// 触发事件，确保线程从 WaitForMultipleObjects 中醒来。
	if (input_pipe_stop_event_) {
		SetEvent(input_pipe_stop_event_);
	}
	// 等待线程退出，清理资源。
	if (input_pipe_thread_.joinable()) {
		input_pipe_thread_.join();
	}
	// 关闭事件句柄，清理资源。
	if (input_pipe_stop_event_) {
		CloseHandle(input_pipe_stop_event_);
		input_pipe_stop_event_ = nullptr;
	}
}

void CEFjihe::HandleInputPacketOnUI(const InputEventPacket& pkt) {
	// 确保在 UI 线程上执行，因为 CEF 的输入事件必须在 UI 线程处理。
	if (!CefCurrentlyOn(TID_UI)) {
		CefPostTask(TID_UI, base::BindOnce(&CEFjihe::HandleInputPacketOnUI, this, pkt));
		return;
	}
	// 没有浏览器实例就退出
	if (browser_list_.empty()) {
		return;
	}
	// 获取浏览器对象列表
	CefRefPtr<CefBrowser> browser = browser_list_.front();
	if (!browser) {
		return;
	}
	// 获取浏览器窗口列表
	CefRefPtr<CefBrowserHost> host = browser->GetHost();
	if (!host) {
		return;
	}

	// 构造鼠标事件
	CefMouseEvent mouse_event;
	mouse_event.x = pkt.x;
	mouse_event.y = pkt.y;
	mouse_event.modifiers = pkt.modifiers;

	// 根据事件类型分发处理
	switch (static_cast<InputEventType>(pkt.type)) {
	case InputEventType::MouseMove:
		InputMouseMoveEvent(host, mouse_event);
		break;

	case InputEventType::MouseButton:
		InputMouseButtonEvent(host, mouse_event, pkt);
		break;

	case InputEventType::MouseWheel:
		InputMouseWheelEvent(host, mouse_event, pkt);
		break;

	case InputEventType::Key: {
		InputKeyEvent(host, pkt);
		break;
	}

	case InputEventType::Text: {
		InputTextEvent(host, pkt);
		break;
	}

	case InputEventType::Composition: {
		InputCompositionEvent(host, pkt);
		break;
	}

	case InputEventType::Navigate: {
		InputNavigateEvent(host, pkt, browser);
		break;
	}

	case InputEventType::ExecuteJs: {
		InputExecuteJsEvent(host, pkt, browser);
		break;
	}

	case InputEventType::Resize:
		InputResizeEvent(host, pkt);
		break;

	case InputEventType::Focus:
		InputFocusEvent(host, pkt);
		break;

	default:
		break;
	}
}

#pragma endregion

int CEFjihe::CEFMain(int argc, char* argv[]) {
	void* sandbox_info = nullptr;
#if defined(CEF_USE_SANDBOX)
	CefScopedSandboxInfo scoped_sandbox;
	sandbox_info = scoped_sandbox.sandbox_info();
#endif
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
	app->bridge_demo_shm_name_ = ReadWStringSwitch(cmd, "bridge-demo-shm-name", L"");

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
		app->OpenSharedMemory(
			app->shm_name_,
			shm_bytes,
			app->shm_name_,
			app->shm_handle_,
			app->shm_view_,
			app->shm_size_,
			L" "
		);
	}
	else {
		std::cout << "[CEF] 未收到 --shm-name，OnPaint 数据将不会输出到共享内存" << std::endl;
	}
	if (!app->ime_shm_name_.empty()) {
		app->OpenSharedMemory(
			app->ime_shm_name_, 
			sizeof(ImeUiState), 
			app->ime_shm_name_,
			app->ime_shm_handle_, 
			app->ime_shm_view_,
			app->ime_shm_size_,
			L"IME"
		);
	}
	else {
		std::cout << "[CEF] 未收到 --ime-shm-name，IME 候选框位置不会同步到 SDL" << std::endl;
	}
	if (!app->bridge_demo_shm_name_.empty()) {
		if (app->OpenSharedMemory(
			app->bridge_demo_shm_name_,
			sizeof(BridgeDemoState),
			app->bridge_demo_shm_name_,
			app->bridge_demo_shm_handle_,
			app->bridge_demo_shm_view_,
			app->bridge_demo_shm_size_,
			L"JS演示"
		)) {
			app->WriteBridgeDemoStatus("桥接状态已连接，等待页面与按钮交互");
		}
	}
	else {
		std::cout << "[CEF] 未收到 --bridge-demo-shm-name，SDL 不会显示 JS 桥接状态" << std::endl;
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
	app->CloseSharedMemory(app->bridge_demo_shm_view_, app->bridge_demo_shm_handle_, app->bridge_demo_shm_size_);
	app->CloseSharedMemory(app->ime_shm_view_, app->ime_shm_handle_, app->ime_shm_size_);
	app->CloseSharedMemory(app->shm_view_, app->shm_handle_, app->shm_size_);
	CefShutdown();
	return 0;
}

#pragma region 输入事件_

void CEFjihe::InputMouseMoveEvent(CefRefPtr<CefBrowserHost>& host, CefMouseEvent& mouse_event) {
	// 强制让浏览器窗口获得焦点
	host->SetFocus(true);
	// 发送鼠标移动事件给浏览器
	host->SendMouseMoveEvent(mouse_event, false);
}

void CEFjihe::InputMouseButtonEvent(CefRefPtr<CefBrowserHost>& host, CefMouseEvent& mouse_event, const InputEventPacket& pkt) {
	// 强制让浏览器窗口获得焦点
	host->SetFocus(true);
	// 发送鼠标按键事件给浏览器
	host->SendMouseClickEvent(
		mouse_event,
		ToCefMouseButton(pkt.mouse_button),
		pkt.mouse_up != 0,
		static_cast<int>(pkt.click_count));
}

void CEFjihe::InputMouseWheelEvent(CefRefPtr<CefBrowserHost>& host, CefMouseEvent& mouse_event, const InputEventPacket& pkt) {
	// 强制让浏览器窗口获得焦点
	host->SetFocus(true);
	// 发送滚轮事件给浏览器
	host->SendMouseWheelEvent(mouse_event, pkt.delta_x, pkt.delta_y);
}

void CEFjihe::InputKeyEvent(CefRefPtr<CefBrowserHost>& host, const InputEventPacket& pkt) {
	// 创建 CEF 键盘事件
	CefKeyEvent key_event{};
	// Windows 虚拟键码
	key_event.windows_key_code = static_cast<int>(pkt.key_code);
	// 系统原生键码
	key_event.native_key_code = static_cast<int>(pkt.native_key_code);
	// 按键修饰符
	key_event.modifiers = pkt.modifiers;
	// 事件类型：按下或抬起
	key_event.type = pkt.key_up ? KEYEVENT_KEYUP : KEYEVENT_RAWKEYDOWN;
	// 发送事件给浏览器
	host->SendKeyEvent(key_event);
}

void CEFjihe::InputTextEvent(CefRefPtr<CefBrowserHost>& host, const InputEventPacket& pkt) {
	// 编码转换：UTF-8 → UTF-16（宽字符串）
	const std::wstring wide_text = Utf8ToWide(pkt.text, sizeof(pkt.text));
	if (wide_text.empty()) {
		return;
	}
	// 输入法组合提交后的文本需要走 ImeCommitText，避免 Chromium 的组合态与宿主脱节。
	if (ime_composition_active_) {
		host->ImeCommitText(CefString(wide_text), CefRange(), 0);
		ime_composition_active_ = false;
		ime_last_rect_valid_ = false;
		ClearImeUiState();
		return;
	}
	// 逐个字符模拟键盘输入
	for (wchar_t ch : wide_text) {
		// 创建 CEF 键盘事件
		CefKeyEvent char_event{};
		// 设置事件类型：字符输入事件
		char_event.type = KEYEVENT_CHAR;

		//把完整字符赋值给事件，浏览器能正确识别
		char_event.windows_key_code = static_cast<int>(ch);
		char_event.native_key_code = char_event.windows_key_code;
		char_event.character = static_cast<char16_t>(ch);
		char_event.unmodified_character = static_cast<char16_t>(ch);

		// 携带按键修饰符
		char_event.modifiers = pkt.modifiers;
		// 发送事件给浏览器
		host->SendKeyEvent(char_event);
	}
}

void CEFjihe::InputCompositionEvent(CefRefPtr<CefBrowserHost>& host, const InputEventPacket& pkt) {
	// SDL_EVENT_TEXT_EDITING 对应 OSR 场景下的组合串变化，必须显式同步给 Chromium。
	const std::wstring wide_text = Utf8ToWide(pkt.text, sizeof(pkt.text));
	if (wide_text.empty()) {
		if (ime_composition_active_) {
			host->ImeCancelComposition();
			ime_composition_active_ = false;
			ime_last_rect_valid_ = false;
			ClearImeUiState();
		}
		return;
	}

	const int text_length = static_cast<int>(wide_text.size());
	const int selection_start = std::clamp(pkt.composition_start, 0, text_length);
	const int selection_length = pkt.composition_length < 0 ? 0 : pkt.composition_length;
	const int selection_end = std::clamp(selection_start + selection_length, selection_start, text_length);
	const CefRange selection_range(selection_start, selection_end);

	host->ImeSetComposition(CefString(wide_text), {}, CefRange(), selection_range);
	ime_composition_active_ = true;
}

void CEFjihe::InputNavigateEvent(CefRefPtr<CefBrowserHost>& host, const InputEventPacket& pkt, CefRefPtr<CefBrowser>& browser) {
	// 强制让浏览器窗口获得焦点
	host->SetFocus(true);
	// 判断浏览器的主框架是否存在。
	if (browser->GetMainFrame()) {
		// 加载到新的地址。
		browser->GetMainFrame()->LoadURL(pkt.text);
	}
}

void CEFjihe::InputExecuteJsEvent(CefRefPtr<CefBrowserHost>& host, const InputEventPacket& pkt, CefRefPtr<CefBrowser>& browser) {
	// 强制让浏览器窗口获得焦点
	host->SetFocus(true);
	// 判断浏览器的主框架是否存在。
	if (browser->GetMainFrame()) {
		// 创建一条CEF 进程间消息
		auto message = CefProcessMessage::Create(kMsgEvalJs);
		// 把要传给 JS 的文本数据放进消息里
		message->GetArgumentList()->SetString(0, pkt.text);
		// 把消息从 C++ 主进程 发送给 JS 渲染进程
		browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, message);
		WriteBridgeDemoStatus("已从 SDL 发起 C++ -> JS 调用");
	}
}

void CEFjihe::InputResizeEvent(CefRefPtr<CefBrowserHost>& host, const InputEventPacket& pkt) {
	view_w_ = static_cast<int>(pkt.width);
	view_h_ = static_cast<int>(pkt.height);

	// 强制让浏览器窗口获得焦点
	host->WasResized();
}

void CEFjihe::InputFocusEvent(CefRefPtr<CefBrowserHost>& host, const InputEventPacket& pkt) {
	// 焦点更变
	host->SetFocus(pkt.key_up == 0);
	// 如果是失去焦点，顺便隐藏 IME 候选框，避免残留在屏幕上。
	if (pkt.key_up != 0 && ime_shm_view_ && ime_shm_size_ >= sizeof(ImeUiState)) {
		if (ime_composition_active_) {
			host->ImeCancelComposition();
			ime_composition_active_ = false;
		}
		ime_last_rect_valid_ = false;
		ClearImeUiState();
	}
}

#pragma endregion

#pragma region 共享内存_

bool CEFjihe::OpenSharedMemory(const std::wstring& shm_name,
	size_t total_size,
	std::wstring& out_shm_name,
	HANDLE& out_shm_handle,
	uint8_t*& out_shm_view,
	size_t& out_shm_size,
	const std::wstring& log_tag
) {
	// OpenFileMappingW = Windows API：通过【名称】打开一个共享内存
	out_shm_handle = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, shm_name.c_str());
	if (!out_shm_handle) {
		std::wcerr << L"[CEF] " << log_tag << L" OpenFileMappingW 失败, name=" << shm_name
			<< L", err=" << GetLastError() << std::endl;
		return false;
	}

	// MapViewOfFile = 将共享内存映射到当前进程的地址空间
	// 返回一个指针，进程可以直接用这个指针读写共享内存
	out_shm_view = static_cast<uint8_t*>(MapViewOfFile(
		out_shm_handle,  // 刚才打开的共享内存句柄
		FILE_MAP_ALL_ACCESS,      // 读写权限
		0,                        // 偏移高位
		0,                        // 偏移低位
		total_size                // 要映射的大小
	));
	// 映射失败就清理关闭退出
	if (!out_shm_view) {
		std::wcerr << L"[CEF] " << log_tag << L" MapViewOfFile 失败, err=" << GetLastError() << std::endl;
		// Windows API：关闭内核对象句柄
		CloseHandle(out_shm_handle);
		out_shm_handle = nullptr;
		return false;
	}
	// 保存共享内存名称
	out_shm_name = shm_name;
	// 保存共享内存大小
	out_shm_size = total_size;
	std::wcout << L"[CEF] " << log_tag << L"共享内存已连接: " << out_shm_name << L", size = " << out_shm_size << std::endl;
	return true;
}

void CEFjihe::CloseSharedMemory(uint8_t*& shm_view, HANDLE& shm_handle, size_t& shm_size) {
	// 卸载虚拟地址映射
	if (shm_view) {
		// Windows API：解除文件映射（把共享内存从进程地址空间卸载）
		UnmapViewOfFile(shm_view);
		shm_view = nullptr;
	}
	// 关闭内核句柄
	if (shm_handle) {
		// Windows API：关闭内核对象句柄
		CloseHandle(shm_handle);
		shm_handle = nullptr;
	}
	// 大小清零
	shm_size = 0;
}

void CEFjihe::PublishImeUiState(
	int x,
	int y,
	int w,
	int h,
	int cursor_x,
	int cursor_y
) {
	if (!ime_shm_view_ || ime_shm_size_ < sizeof(ImeUiState)) {
		return;
	}

	auto* state = reinterpret_cast<ImeUiState*>(ime_shm_view_);
	state->magic = kImeUiMagic;
	state->version = kImeUiVersion;
	state->x = x;
	state->y = y;
	state->w = std::max(1, w);
	state->h = std::max(1, h);
	state->cursor_x = cursor_x;
	state->cursor_y = cursor_y;
	state->visible = 1;
	MemoryBarrier();
	state->seq = ++ime_seq_;
}

void CEFjihe::ClearImeUiState() {
	if (!ime_shm_view_ || ime_shm_size_ < sizeof(ImeUiState)) {
		return;
	}

	auto* state = reinterpret_cast<ImeUiState*>(ime_shm_view_);
	state->magic = kImeUiMagic;
	state->version = kImeUiVersion;
	state->visible = 0;
	MemoryBarrier();
	state->seq = ++ime_seq_;
}

#pragma endregion

#pragma region JS演示共享内存_

void CEFjihe::WriteBridgeDemoStatus(const std::string& text) {
	// 检查：共享内存视图是否为空 或 共享内存大小不足，不满足条件直接退出，避免越界/空指针崩溃
	if (!bridge_demo_shm_view_ || bridge_demo_shm_size_ < sizeof(BridgeDemoState)) {
		return;
	}
	// 将共享内存的裸指针 强转为 自定义结构体指针
	auto* state = reinterpret_cast<BridgeDemoState*>(bridge_demo_shm_view_);
	// 写入标识号
	state->magic = kBridgeDemoMagic;
	// 写入版本号
	state->version = kBridgeDemoVersion;
	// 复制字符串到共享内存结构体的字符数组，最大长度为数组大小-1
	std::strncpy(state->status_text, text.c_str(), sizeof(state->status_text) - 1);
	// 强制手动添加字符串结束符，防止strncpy未填充结束符导致乱码
	state->status_text[sizeof(state->status_text) - 1] = '\0';
	// 内存屏障
	MemoryBarrier();
	// 更新序号递增，确保状态更新的可见性；读端据此判断状态变化。
	state->seq = ++bridge_demo_seq_;
}

#pragma endregion

#pragma region 输入事件_

void CEFjihe::InputPipeLoop() {
	while (input_pipe_running_) {
		// 检查是否收到停止信号，如果收到立即退出循环
		if (WaitForSingleObject(input_pipe_stop_event_, 0) == WAIT_OBJECT_0) {
			break;
		}
		// 创建输入事件的命名管道
		HANDLE pipe = CreateNamedPipeW(
			input_pipe_name_.c_str(),  // 管道名字
			PIPE_ACCESS_INBOUND,        // 只读：外部写，这里读
			PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,  // 消息模式、阻塞等待
			1,                          // 只允许 1 个实例
			sizeof(InputEventPacket),   // 读缓冲区 = 数据包大小
			sizeof(InputEventPacket),   // 写缓冲区
			1000,                       // 超时毫秒
			nullptr                     // 安全属性
		);
		// 创建失败立刻退出循环
		if (pipe == INVALID_HANDLE_VALUE) {
			break;
		}
		// 阻塞式等待输入事件管道被连接
		BOOL connected = ConnectNamedPipe(pipe, nullptr)
			? TRUE
			: (GetLastError() == ERROR_PIPE_CONNECTED);
		// 如果连接失败，关闭管道句柄并结束当前循环
		if (!connected) {
			CloseHandle(pipe);
			continue;
		}
		// 循环读取输入事件数据包，直到管道断开或收到停止信号
		while (input_pipe_running_) {
			InputEventPacket pkt{};
			DWORD read_bytes = 0;
			// 读取一个完整的数据包，如果读取失败或字节数不匹配，退出当前循环
			BOOL ok = ReadFile(pipe, &pkt, sizeof(pkt), &read_bytes, nullptr);
			if (!ok || read_bytes != sizeof(pkt)) {
				break;
			}
			// 验证数据包的标识号和版本，确保数据格式正确；如果不匹配，忽略该数据包继续等待下一个
			if (pkt.magic != kInputMagic || pkt.version != kInputVersion) {
				continue;
			}

			// 管道线程不能直接调 BrowserHost，必须切 UI 线程
			CefPostTask(TID_UI, base::BindOnce(&CEFjihe::HandleInputPacketOnUI, this, pkt));
		}
		// 刷新管道缓冲区，确保数据完全传输
		FlushFileBuffers(pipe);
		// 关闭命名通道
		DisconnectNamedPipe(pipe);
		// 关闭管线句柄
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

#pragma endregion
