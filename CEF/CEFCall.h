#pragma once

#include "include/cef_app.h"
#include "include/cef_client.h"
#include "include/cef_render_handler.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_render_process_handler.h"
#include "include/cef_load_handler.h"
#include "include/cef_display_handler.h"
#include "include/cef_v8.h"

#include "common/InputProtocol.h"
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
	/// <param name="process_type">进程类型</param>
	/// <param name="command_line">进程参数</param>
	void OnBeforeCommandLineProcessing(
		const CefString& process_type,
		CefRefPtr<CefCommandLine> command_line
	) override;
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
	/// <param name="browser">浏览器对象</param>
	/// <param name="frame">网页框架</param>
	/// <param name="source_process">消息来源</param>
	/// <param name="message">收到的消息</param>
	bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, 
		CefRefPtr<CefFrame> frame, 
		CefProcessId source_process, 
		CefRefPtr<CefProcessMessage> message
	) override;
#pragma endregion

#pragma region CefLifeSpanHandler
	/// <summary>
	/// 浏览器创建后调用
	/// </summary>
	/// <param name="browser">浏览器对象</param>
	void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
	/// <summary>
	/// 浏览器真正关闭后调用
	/// </summary>
	/// <param name="browser">浏览器对象</param>
	void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;
	/// <summary>
	/// 关闭浏览器窗口
	/// </summary>
	/// <param name="force_close">是否强制关闭</param>
	void CloseAllBrowsers(bool force_close);
	/// <summary>
	/// 判断是否所有浏览器窗口都已关闭
	/// </summary>
	/// <returns></returns>
	bool IsClosing() const { return is_closing_; }
#pragma endregion

#pragma region CefLoadHandler
	/// <summary>
	/// 当网页开始加载、加载完成、加载停止时，CEF 都会自动调用这个函数通知你。
	/// </summary>
	/// <param name="browser">浏览器对象</param>
	/// <param name="isLoading">是否正在加载</param>
	/// <param name="canGoBack">是否能后退</param>
	/// <param name="canGoForward">是否能前进</param>
	void OnLoadingStateChange(CefRefPtr<CefBrowser> browser,
		bool isLoading,
		bool canGoBack,
		bool canGoForward
	) override;
#pragma endregion

#pragma region CefRenderProcessHandler
	/// <summary>
	/// 这是 CEF 渲染进程的核心回调函数，专门用来给网页 JS 注入 C++ 方法、建立双向通信
	/// </summary>
	/// <param name="browser">浏览器对象</param>
	/// <param name="frame">网页框架</param>
	/// <param name="context">JS 执行上下文</param>
	void OnContextCreated(CefRefPtr<CefBrowser> browser,
		CefRefPtr<CefFrame> frame,
		CefRefPtr<CefV8Context> context
	) override;
#pragma endregion

#pragma region CefDisplayHandler
	/// <summary>
	/// 这是 CEF 提供的虚函数重写，专门用来同步网页鼠标光标样式到你的 SDL / 外部窗口。
	/// </summary>
	/// <param name="browser">浏览器对象</param>
	/// <param name="cursor">系统光标句柄</param>
	/// <param name="type">光标类型</param>
	/// <param name="custom_cursor_info">自定义图片光标</param>
	bool OnCursorChange(CefRefPtr<CefBrowser> browser,
		CefCursorHandle cursor,
		cef_cursor_type_t type,
		const CefCursorInfo& custom_cursor_info
	) override;
#pragma endregion

#pragma region CefRenderHandler（OSR 核心）
	/// <summary>
	/// 设置渲染区域大小
	/// </summary>
	/// <param name="browser">浏览器对象</param>
	/// <param name="rect">渲染区域大小</param>
	void GetViewRect(CefRefPtr<CefBrowser> browser, CefRect& rect) override;
	/// <summary>
	/// 获取CEF的渲染像素数据,对外提供接口，供外部调用获取渲染数据
	/// </summary>
	/// <param name="browser">浏览器对象</param>
	/// <param name="type">渲染类型</param>
	/// <param name="dirtyRects">需要刷新的脏区域</param>
	/// <param name="buffer">渲染好的图片像素数据</param>
	/// <param name="width">图片宽度</param>
	/// <param name="height">图片高度</param>
	void OnPaint(CefRefPtr<CefBrowser> browser, 
		PaintElementType type, 
		const RectList& dirtyRects, 
		const void* buffer, 
		int width, 
		int height
	) override;
	/// <summary>
	/// CEF 的输入法（IME）候选框跟随核心函数
	/// </summary>
	/// <param name="browser">浏览器对象</param>
	/// <param name="selected_range">拼写区</param>
	/// <param name="character_bounds">光标坐标</param>
	void OnImeCompositionRangeChanged(CefRefPtr<CefBrowser> browser,
		const CefRange& selected_range,
		const RectList& character_bounds
	) override;
#pragma endregion

#pragma region 输入事件
	/// <summary>
	/// 启动输入命名管道
	/// </summary>
	/// <param name="pipe_name">管道名称</param>
	bool StartInputPipeServer(const std::wstring& pipe_name);
	// 停止输入命名管道
	void StopInputPipeServer();
	/// <summary>
	/// 在UI上处理输入数据包
	/// </summary>
	/// <param name="pkt">数据包</param>
	void HandleInputPacketOnUI(const InputEventPacket& pkt);

#pragma endregion

	// CEF入口函数
	static int CEFMain(int argc, char* argv[]);

private:

#pragma region 输入事件_
	/// <summary>
	/// 鼠标移动事件处理
	/// </summary>
	/// <param name="host">浏览器窗口</param>
	/// <param name="mouse_event">鼠标事件</param>
	void InputMouseMoveEvent(CefRefPtr<CefBrowserHost>& host, CefMouseEvent& mouse_event);
	/// <summary>
	/// 鼠标按下事件处理
	/// </summary>
	/// <param name="host">浏览器窗口</param>
	/// <param name="mouse_event">鼠标事件</param>
	/// <param name="pkt">数据包</param>
	void InputMouseButtonEvent(CefRefPtr<CefBrowserHost>& host, CefMouseEvent& mouse_event, const InputEventPacket& pkt);
	/// <summary>
	/// 鼠标滚轮事件处理
	/// </summary>
	/// <param name="host">浏览器窗口</param>
	/// <param name="mouse_event">鼠标事件</param>
	/// <param name="pkt">数据包</param>
	void InputMouseWheelEvent(CefRefPtr<CefBrowserHost>& host, CefMouseEvent& mouse_event, const InputEventPacket& pkt);
	/// <summary>
	/// 键盘事件处理
	/// </summary>
	/// <param name="host">浏览器窗口</param>
	/// <param name="pkt">数据包</param>
	void InputKeyEvent(CefRefPtr<CefBrowserHost>& host, const InputEventPacket& pkt);
	/// <summary>
	/// 文本输入事件处理
	/// </summary>
	/// <param name="host">浏览器窗口</param>
	/// <param name="pkt">数据包</param>
	void InputTextEvent(CefRefPtr<CefBrowserHost>& host, const InputEventPacket& pkt);
	/// <summary>
	/// 输入法组合串事件处理
	/// </summary>
	/// <param name="host">浏览器窗口</param>
	/// <param name="pkt">数据包</param>
	void InputCompositionEvent(CefRefPtr<CefBrowserHost>& host, const InputEventPacket& pkt);
	/// <summary>
	/// 地址导航事件处理
	/// </summary>
	/// <param name="host">浏览器窗口</param>
	/// <param name="pkt">数据包</param>
	/// <param name="browser">浏览器对象</param>
	void InputNavigateEvent(CefRefPtr<CefBrowserHost>& host, const InputEventPacket& pkt, CefRefPtr<CefBrowser>& browser);
	/// <summary>
	/// js执行事件处理
	/// </summary>
	/// <param name="host">浏览器窗口</param>
	/// <param name="pkt">数据包</param>
	/// <param name="browser">浏览器对象</param>
	void InputExecuteJsEvent(CefRefPtr<CefBrowserHost>& host, const InputEventPacket& pkt, CefRefPtr<CefBrowser>& browser);
	/// <summary>
	/// 视图尺寸变化事件处理
	/// </summary>
	/// <param name="host">浏览器窗口</param>
	/// <param name="pkt">数据包</param>
	void InputResizeEvent(CefRefPtr<CefBrowserHost>& host, const InputEventPacket& pkt);
	/// <summary>
	/// 焦点变化事件处理
	/// </summary>
	/// <param name="host">浏览器窗口</param>
	/// <param name="pkt">数据包</param>
	void InputFocusEvent(CefRefPtr<CefBrowserHost>& host, const InputEventPacket& pkt);
#pragma endregion

#pragma region 共享内存
	/// <summary>
	/// 开启共享内存
	/// </summary>
	/// <param name="shm_name">共享内存名称</param>
	/// <param name="total_size">共享内存大小</param>
	/// <param name="out_shm_name">记录内存名称</param>
	/// <param name="out_shm_handle">记录内存句柄</param>
	/// <param name="out_shm_view">记录内存视图指针</param>
	/// <param name="out_shm_size">记录内存大小</param>
	/// <param name="log_tag">共享内存标签</param>
	bool OpenSharedMemory(const std::wstring& shm_name,
		size_t total_size,
		std::wstring& out_shm_name,
		HANDLE& out_shm_handle,
		uint8_t*& out_shm_view,
		size_t& out_shm_size,
		const std::wstring& log_tag
	);
	/// <summary>
	/// 关闭共享内存并释放相关资源
	/// </summary>
	/// <param name="shm_view">共享内存的视图指针</param>
	/// <param name="shm_handle">共享内存的句柄</param>
	/// <param name="shm_size">共享内存的大小</param>
	void CloseSharedMemory(uint8_t*& shm_view, HANDLE& shm_handle, size_t& shm_size);
	/// <summary>
	/// 更新 IME 候选框锚点状态
	/// </summary>
	/// <param name="x">锚点矩形 x</param>
	/// <param name="y">锚点矩形 y</param>
	/// <param name="w">锚点矩形宽</param>
	/// <param name="h">锚点矩形高</param>
	/// <param name="cursor_x">光标 x</param>
	/// <param name="cursor_y">光标 y</param>
	void PublishImeUiState(
		int x,
		int y,
		int w,
		int h,
		int cursor_x,
		int cursor_y
	);
	/// <summary>
	/// 清空 IME 候选框锚点状态
	/// </summary>
	void ClearImeUiState();
#pragma endregion

#pragma region JS演示共享内存
	/// <summary>
    /// 将文本写入js桥接演示的共享内存。
    /// </summary>
    /// <param name="text">要写入的文本。</param>
	void WriteBridgeDemoStatus(const std::string& text);
#pragma endregion

#pragma region 输入事件
	// 输入事件管道循环，等待连接并处理输入事件包
	void InputPipeLoop();
	/// <summary>
	/// 鼠标类型转为CEF鼠标类型
	/// </summary>
	/// <param name="btn">鼠标类型</param>
	static CefBrowserHost::MouseButtonType ToCefMouseButton(uint32_t btn);
#pragma endregion

private:
	//存储所有浏览器窗口的列表
	std::list<CefRefPtr<CefBrowser>> browser_list_;
	// 关闭标志，表示正在关闭所有浏览器窗口
	bool is_closing_ = false;

	// 离屏尺寸宽
	int view_w_ = 1280;
	// 离屏尺寸高
	int view_h_ = 720;
	// 默认网页
	CefString startup_url_ = "https://www.baidu.com";

#pragma region 浏览器视图共享内存
	// 浏览器视图共享内存的名称
	std::wstring shm_name_;
	// 浏览器视图共享内存的句柄
	HANDLE shm_handle_ = nullptr;
	// 浏览器视图共享内存的视图指针
	uint8_t* shm_view_ = nullptr;
	// 浏览器视图共享内存的大小
	size_t shm_size_ = 0;
	// 浏览器视图帧计数器，用于记录判断帧的更新
	uint64_t frame_counter_ = 0;
#pragma endregion

#pragma region UI操作状态共享内存
	// UI操作状态共享内存的名称
	std::wstring ime_shm_name_;
	// UI操作状态共享内存的句柄
	HANDLE ime_shm_handle_ = nullptr;
	// UI操作状态共享内存的虚拟地址指针
	uint8_t* ime_shm_view_ = nullptr;
	// UI操作状态共享内存的大小
	size_t ime_shm_size_ = 0;
	// UI操作状态共享内存的更新序号
	uint64_t ime_seq_ = 0;
	// 当前是否处于输入法组合态
	bool ime_composition_active_ = false;
	// 上一次有效的 IME 锚点矩形
	CefRect ime_last_rect_;
	// 上一次有效的 IME 锚点是否可用
	bool ime_last_rect_valid_ = false;
#pragma endregion

#pragma region JS演示共享内存
	// js桥接演示共享内存的名称
	std::wstring bridge_demo_shm_name_;
	// js桥接演示共享内存的句柄
	HANDLE bridge_demo_shm_handle_ = nullptr;
	// js桥接演示共享内存的虚拟地址指针
	uint8_t* bridge_demo_shm_view_ = nullptr;
	// js桥接演示共享内存的大小
	size_t bridge_demo_shm_size_ = 0;
	// js桥接演示共享内存的更新序号
	uint64_t bridge_demo_seq_ = 0;
#pragma endregion

#pragma region 输入事件
    // 传递输入事件的管道的名称
	std::wstring input_pipe_name_;
	// 传递输入事件的管道的线程
	std::thread input_pipe_thread_;
	// 传递输入事件的管道的关闭事件
	HANDLE input_pipe_stop_event_ = nullptr;
	// 传递输入事件的管道的运行控制
	std::atomic<bool> input_pipe_running_{ false };
#pragma endregion
    //Chrome特有的引用计数宏
	IMPLEMENT_REFCOUNTING(CEFjihe);
};
