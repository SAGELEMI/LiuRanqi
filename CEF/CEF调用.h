#pragma once

#include "include/cef_app.h"
#include "include/cef_client.h"
#include "include/cef_render_handler.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_render_process_handler.h"
#include "include/cef_load_handler.h"
#include "include/cef_display_handler.h"

#include "输入事件协议.h"
#include <thread>
#include <atomic>
#include <list>
#include <string>
#include <windows.h>

class CEFjihe :
	public CefApp,
	public CefClient,
	public CefLifeSpanHandler,
	public CefRenderHandler,
	public CefRenderProcessHandler,
	public CefLoadHandler,
	public CefDisplayHandler
{
public:
	CEFjihe();
	~CEFjihe();
#pragma region CefApp
	CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override { return nullptr; }
	CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override { return this; }
	/// <summary>
	/// 配置 CEF 的命令行参数
	/// </summary>
	/// <param name="process_type"></param>
	/// <param name="command_line"></param>
	void OnBeforeCommandLineProcessing(const CefString& process_type, CefRefPtr<CefCommandLine> command_line) override;
#pragma endregion

#pragma region CefClient
	/// <summary>
	/// OSR 回调入口
	/// </summary>
	/// <returns></returns>
	CefRefPtr<CefRenderHandler> GetRenderHandler() override { return this; }
	/// <summary>
    /// 返回生命周期处理程序
    /// </summary>
    /// <returns></returns>
	CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
	CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
	CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
	/// <summary>
	/// CEF 跨进程通信核心回调
	/// </summary>
	/// <param name="browser">浏览器实例</param>
	/// <param name="frame">网页 frame</param>
	/// <param name="source_process">消息来源</param>
	/// <param name="message">消息本体</param>
    /// <returns></returns>
	bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefProcessId source_process, CefRefPtr<CefProcessMessage> message) override;
#pragma endregion

#pragma region CefLifeSpanHandler
	/// <summary>
	/// 浏览器创建后调用
	/// </summary>
	/// <param name="browser"></param>
	void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
	/// <summary>
	/// 浏览器真正关闭后调用
	/// </summary>
	/// <param name="browser"></param>
	void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;
	/// <summary>
	/// 关闭浏览器窗口
	/// </summary>
	/// <param name="force_close"></param>
	void CloseAllBrowsers(bool force_close);
	/// <summary>
	/// 判断是否所有浏览器窗口都已关闭
	/// </summary>
	/// <returns></returns>
	bool IsClosing() const { return is_closing_; }
#pragma endregion

#pragma region CefLoadHandler
	/// <summary>
	/// 当浏览器的加载状态发生变化时调用。重写基类方法以响应加载开始/结束及前进/后退可用性的更改。
	/// </summary>
	/// <param name="browser">发生状态变化的浏览器实例（CefBrowser 的引用）。</param>
	/// <param name="isLoading">如果浏览器当前正在加载资源则为 true，否则为 false。</param>
	/// <param name="canGoBack">如果浏览器历史记录中存在可回退的页面则为 true，否则为 false。</param>
	/// <param name="canGoForward">如果浏览器历史记录中存在可前进的页面则为 true，否则为 false。</param>
	void OnLoadingStateChange(CefRefPtr<CefBrowser> browser,bool isLoading,bool canGoBack,bool canGoForward) override;
#pragma endregion

#pragma region CefDisplayHandler
	bool OnCursorChange(CefRefPtr<CefBrowser> browser,
		CefCursorHandle cursor,
		cef_cursor_type_t type,
		const CefCursorInfo& custom_cursor_info) override;
#pragma endregion

#pragma region CefRenderHandler（OSR 核心）
	/// <summary>
	/// 设置渲染区域大小
	/// </summary>
	/// <param name="browser"></param>
	/// <param name="rect"></param>
	void GetViewRect(CefRefPtr<CefBrowser> browser, CefRect& rect) override;
	bool GetScreenPoint(CefRefPtr<CefBrowser> browser,
		int viewX,
		int viewY,
		int& screenX,
		int& screenY) override;
	/// <summary>
	/// 获取CEF的渲染像素数据,对外提供接口，供外部调用获取渲染数据
	/// </summary>
	/// <param name="browser"></param>
	/// <param name="type"></param>
	/// <param name="dirtyRects"></param>
	/// <param name="buffer"></param>
	/// <param name="width"></param>
	/// <param name="height"></param>
	void OnPaint(CefRefPtr<CefBrowser> browser, PaintElementType type, const RectList& dirtyRects, const void* buffer, int width, int height) override;
	void OnImeCompositionRangeChanged(CefRefPtr<CefBrowser> browser,
		const CefRange& selected_range,
		const RectList& character_bounds) override;
#pragma endregion

#pragma region 输入
	bool StartInputPipeServer(const std::wstring& pipe_name);
	void StopInputPipeServer();
	void HandleInputPacketOnUI(const InputEventPacket& pkt);
#pragma endregion

	static int 运行(int argc, char* argv[], void* sandbox_info);

private:
	/// <summary>
	/// 作为“打开者”连接并映射已经存在的共享内存区域。
	/// </summary>
	/// <param name="shm_name">共享内存的名称（std::wstring），用于 OpenFileMappingW 打开已创建的映射。</param>
	/// <param name="total_size">要映射的总大小（以字节为单位），用于 MapViewOfFile 的长度参数。</param>
	/// <returns>若成功打开并映射共享内存，则返回 true；若任一步骤失败则返回 false（失败时会关闭句柄并进行必要清理）。</returns>
	bool InitSharedMemory(const std::wstring& shm_name, size_t total_size);
	/// <summary>
	/// 关闭共享内存并释放相关资源。
	/// </summary>
	void CloseSharedMemory();
	bool InitImeUiSharedMemory(const std::wstring& shm_name, size_t total_size);
	void CloseImeUiSharedMemory();
#pragma region 输入
	void InputPipeLoop();
	static CefBrowserHost::MouseButtonType ToCefMouseButton(uint32_t btn);
#pragma endregion
private:
	/// <summary>
	/// 跟踪所有打开的浏览器窗口
	/// </summary>
	std::list<CefRefPtr<CefBrowser>> browser_list_;
	bool is_closing_ = false;

	// 离屏尺寸
	int view_w_ = 1280;
	int view_h_ = 720;
	CefString startup_url_ = "https://www.baidu.com";

	// 共享内存句柄与映射地址（写端）
	std::wstring shm_name_;
	HANDLE shm_handle_ = nullptr;
	uint8_t* shm_view_ = nullptr;
	size_t shm_size_ = 0;
	uint64_t frame_counter_ = 0;

	// IME UI 共享内存（CEF -> SDL）
	std::wstring ime_shm_name_;
	HANDLE ime_shm_handle_ = nullptr;
	uint8_t* ime_shm_view_ = nullptr;
	size_t ime_shm_size_ = 0;
	uint64_t ime_seq_ = 0;

#pragma region 输入
	std::wstring input_pipe_name_;
	std::thread input_pipe_thread_;
	HANDLE input_pipe_stop_event_ = nullptr;
	std::atomic<bool> input_pipe_running_{ false };
#pragma endregion
	/// <summary>
	/// 引用计数实现
	/// </summary>
	/// <param name=""></param>
	IMPLEMENT_REFCOUNTING(CEFjihe);
};
