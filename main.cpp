#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <iostream>
#include "CEF调用.h"

const int kuandu = 800;
const int gaodu = 600;

int main(int argc, char* argv[]) {
	void* sandbox_info = nullptr;
#if defined(CEF_USE_SANDBOX)
	CefScopedSandboxInfo scoped_sandbox;
	sandbox_info = scoped_sandbox.sandbox_info();
#endif
	return CEFjihe::运行(argc, argv, sandbox_info);
	//初始化SDL
	if (!SDL_Init(SDL_INIT_VIDEO)) {
		std::cerr << "无法初始化SDL: " << SDL_GetError() << std::endl;
		return -1;
	}
	//设置OpenGL版本
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	//启用双缓冲
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	//创建窗口
	SDL_Window* window = SDL_CreateWindow(
		"SDL3 + OpenGL 验证窗口",  // 窗口标题
		kuandu,              // 宽度
		gaodu,             // 高度
		SDL_WINDOW_OPENGL          // 关键：指定窗口使用 OpenGL 上下文
	);
	if (!window) {
		std::cerr << "无法创建窗口: " << SDL_GetError() << std::endl;
		SDL_Quit();
		return -1;
	}
	//创建OpenGL上下文
	SDL_GLContext glContext = SDL_GL_CreateContext(window);
	if (!glContext) {
		std::cerr << "无法创建OpenGL上下文: " << SDL_GetError() << std::endl;
		SDL_DestroyWindow(window);
		SDL_Quit();
		return -1;
	}
	//设置 OpenGL 视口（匹配窗口尺寸）
	glViewport(0, 0, kuandu, gaodu);
	//主循环
	bool running = true;
	SDL_Event event;
	while (running) {
		// 处理窗口事件（如关闭窗口）
		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_EVENT_QUIT) {
				running = false;
			}
		}
		// OpenGL 渲染核心：清空窗口并绘制纯色
		// 设置清屏颜色（红色，RGBA 范围 0.0-1.0）
		glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
		// 清空颜色缓冲区（把窗口填充为上面设置的红色）
		glClear(GL_COLOR_BUFFER_BIT);
		//交换缓冲区
		SDL_GL_SwapWindow(window);
	}
	//清理资源
	SDL_GL_DestroyContext(glContext);
	SDL_DestroyWindow(window);
	SDL_Quit();
	return 0;
}