# 限制最低 CMake 版本，避免较老版本缺少当前脚本依赖的功能。
cmake_minimum_required(VERSION 3.20)

# 定义顶层工程，只启用 C 与 C++ 两种语言。
project(LiuRanqi VERSION 0.0.1 LANGUAGES C CXX)

# 让 include(...) 可以直接找到 cmake 目录下的模块文件。
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# 统一全局编译选项、架构限制和生成器配置。
include(ProjectOptions)

# 统一最终产物的输出目录。
include(OutputDirs)

# 拉取并接入 SDL3 / SDL3_ttf / CEF 依赖。
include(Submodule)

# 定义主程序目标及其链接关系。
include(OutputRelease)
