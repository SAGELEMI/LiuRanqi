cmake_minimum_required(VERSION 3.20)

project(LiuRanqi VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 仅支持 x64 + Release。
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "只支持 x64 构建")
endif()
if(CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_CONFIGURATION_TYPES "Release" CACHE STRING "" FORCE)
endif()
set(CMAKE_BUILD_TYPE "Release" CACHE STRING "" FORCE)

if(MSVC)
    add_compile_options(
            /utf-8 
            /EHsc 
            /wd4003
        )
    add_compile_definitions(NOMINMAX)
endif()

# 所有目标输出到根目录“生成”。
set(BIN_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/生成")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${BIN_OUTPUT_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${BIN_OUTPUT_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${BIN_OUTPUT_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${BIN_OUTPUT_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE "${BIN_OUTPUT_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${BIN_OUTPUT_DIR}")

# 父工程只负责定义三方路径，供子目录复用。
set(SDL3_ROOT "${CMAKE_SOURCE_DIR}/../SDL3" CACHE PATH "SDL3路径")
set(CEF_ROOT "${CMAKE_SOURCE_DIR}/../CEF" CACHE PATH "CEF路径")
get_filename_component(SDL3_ROOT "${SDL3_ROOT}" ABSOLUTE)
get_filename_component(CEF_ROOT "${CEF_ROOT}" ABSOLUTE)

if(NOT EXISTS "${SDL3_ROOT}/include/SDL3/SDL.h")
    message(FATAL_ERROR "未找到SDL3头文件: ${SDL3_ROOT}/include/SDL3/SDL.h")
endif()
if(NOT EXISTS "${CEF_ROOT}/include/cef_app.h")
    message(FATAL_ERROR "未找到CEF头文件: ${CEF_ROOT}/include/cef_app.h")
endif()

add_subdirectory(CEF)
add_subdirectory(浏览器)

# 父工程启动项：可执行文件。运行后启动子浏览器的 浏览器.exe
add_executable(run_browser run_browser.cpp)
add_dependencies(run_browser CEF SDL)

if(CMAKE_GENERATOR MATCHES "Visual Studio")
    set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT run_browser)
endif()
