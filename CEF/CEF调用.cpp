#include "CEF调用.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/cef_browser.h"
#include <iostream>

CEFjihe::CEFjihe() : is_closing_(false) {}

CEFjihe::~CEFjihe() {}

void CEFjihe::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
	// 将新创建的浏览器添加到列表中
	browser_list_.push_back(browser);
	std::cout << "浏览器创建成功，ID: " << browser->GetIdentifier()<< std::endl;
	CEFjihe::IsRun = true;
}

void CEFjihe::CloseAllBrowsers(bool force_close) {
	is_closing_ = true;
	if (!CefCurrentlyOn(TID_UI)) {
		//确保在UI线程上执行
		CefPostTask(TID_UI,base::BindOnce(&CEFjihe::CloseAllBrowsers,this, force_close));
		return;
	}
	if(browser_list_.empty()) {
		return;
	}
	for (auto& browser : browser_list_) {
		if (browser.get()) {
			browser->GetHost()->CloseBrowser(force_close);
		}
	}
}
void CEFjihe::OnBeforeClose(CefRefPtr<CefBrowser> browser)
{
	//// 从列表中移除关闭的浏览器
	//std::cout << "当前浏览器数量: " << browser_list_.size() << std::endl;
	//browser_list_.remove(browser);
	//std::cout << "浏览器关闭，ID: " << browser->GetIdentifier() << std::endl;
	//if (browser_list_.empty()) {
	//	std::cout << "所有浏览器窗口已关闭，退出消息循环" << std::endl;
	//	CefQuitMessageLoop();
	//}
	const int closing_id = browser ? browser->GetIdentifier() : -1;
	const size_t before = browser_list_.size();

	// 比较 ID 删除，比直接 remove(browser) 更稳
	browser_list_.remove_if([closing_id](const CefRefPtr<CefBrowser>& b) {
		return b && b->GetIdentifier() == closing_id;
	});

	const size_t after = browser_list_.size();
	std::cout << "关闭ID: " << closing_id
		      << "，删除前: " << before
		      << "，删除后: " << after << std::endl;
	if (after == 0) {
		std::cout << "所有浏览器窗口已关闭，退出消息循环" << std::endl;
		CefQuitMessageLoop();
		CEFjihe::IsRun = false;
	}                                                             
}
int CEFjihe::运行(int argc, char* argv[], void* sandbox_info) {
	int exit_code;
#if CEF_API_VERSION != CEF_EXPERIMENTAL
	printf("使用已配置的CEF API版本 %d 运行\n", CEF_API_VERSION);
#endif

	// 创建CEF主参数
	CefMainArgs main_args(GetModuleHandle(nullptr));
	// 执行CEF子进程
	exit_code = CefExecuteProcess(main_args, nullptr, sandbox_info);
	if (exit_code >= 0) {
		return exit_code;
	}
	// 创建命令行对象并初始化
	CefRefPtr<CefCommandLine> command_line = CefCommandLine::CreateCommandLine();
	command_line->InitFromString(::GetCommandLineW());

	// 浏览器主进程逻辑
	CefSettings settings;
	if(!sandbox_info) {
		settings.no_sandbox = true; // 禁用沙箱（根据需要调整）
	}
	settings.windowless_rendering_enabled = false; // 启用窗口渲染（根据需要调整）

	// 创建CEF应用程序实例
	CefRefPtr<CEFjihe> client(new CEFjihe);
	// 初始化CEF
	if (!CefInitialize(main_args, settings, client.get(), sandbox_info))
	{
		return CefGetExitCode();
	}

	// 创建浏览器窗口
	CefWindowInfo window_info;
	CefBrowserSettings browser_settings;

	//设置窗口信息
	window_info.SetAsPopup(nullptr, "CEF示例");
	std::cout << "启动浏览器窗口" << std::endl;
	CefBrowserHost::CreateBrowserSync(window_info, client.get(), "https://www.baidu.com", browser_settings, nullptr, nullptr);
	std::cout << "启动成功，等待关闭" << std::endl;
	// 运行消息循环
	CefRunMessageLoop();
	std::cout << "正在退出浏览器" << std::endl;
	// 关闭所有浏览器窗口
	//CefShutdown();
	std::cout << "退出浏览器" << std::endl;
	return 0;
}