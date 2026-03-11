# 要求 CMake 最低版本为 3.20；低于此版本会直接报错并停止配置。
cmake_minimum_required(VERSION 3.20)

# 定义工程：
# - 工程名：LiuRanqi（会影响 VS 里显示名、一些默认变量前缀等）
# - 版本号：0.0.1（可用于打包/输出版本信息）
# - 语言：C 与 CXX（仅启用 C/C++ 编译器，不启用其他语言）
project(LiuRanqi VERSION 0.0.1 LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")    

include(ProjectOptions) # 包含统一选项配置（C++ 标准、构建类型等）。

include(OutputDirs) # 包含统一输出目录设置。

include(SDLSubmodule) # 包含 SDL 子模块管理逻辑（路径定义、自动拉取等）。

include(SDLTtfSubmodule) # 包含 SDL_ttf 子模块管理逻辑（路径定义、自动拉取等）。

include(CEFSubmodule)# 包含 CEF 子模块管理逻辑（路径定义、自动拉取等）。

include(OutputRelease) # 包含生成发布版本的目标定义。
