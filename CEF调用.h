#pragma once
#ifndef SIMPLE_CEFjihe_H
#define SIMPLE_CEFjihe_H

#include "include/cef_app.h"
#include "include/cef_client.h"
#include "include/cef_base.h"
#include <list>

class CEFjihe : public CefApp,public CefClient,public CefLifeSpanHandler
{
public:
	CEFjihe();
	~CEFjihe();

	virtual CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
		return nullptr;
	}
	virtual CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override {
		return nullptr;
	}
	/// <summary>
	/// 返回生命周期处理程序
	/// </summary>
	/// <returns></returns>
	virtual CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override {
		return this;
	}
	/// <summary>
	/// 浏览器创建后调用
	/// </summary>
	/// <param name="browser"></param>
	virtual void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
	/// <summary>
	/// 关闭浏览器窗口
	/// </summary>
	/// <param name="force_close"></param>
	void CloseAllBrowsers(bool force_close);
	/// <summary>
	/// 浏览器真正关闭后调用
	/// </summary>
	/// <param name="browser"></param>
	void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;
	/// <summary>
	/// 判断是否所有浏览器窗口都已关闭
	/// </summary>
	/// <returns></returns>
	bool IsClosing() const { return is_closing_; }
	static int 运行(int argc, char* argv[], void* sandbox_info);
private:
	/// <summary>
	/// 跟踪所有打开的浏览器窗口
	/// </summary>
	std::list<CefRefPtr<CefBrowser>> browser_list_;
	bool is_closing_;
	/// <summary>
	/// 引用计数实现
	/// </summary>
	/// <param name=""></param>
	IMPLEMENT_REFCOUNTING(CEFjihe);
};
#endif
