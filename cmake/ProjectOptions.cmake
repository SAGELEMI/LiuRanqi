# 统一启用 C++20。
set(CMAKE_CXX_STANDARD 20)

# 若编译器不支持 C++20，则直接在配置阶段失败。
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 关闭自动重生成，避免每次构建前重复扫描大目录。
set(CMAKE_SUPPRESS_REGENERATION ON)

# 本工程只接受 x64 构建。
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "只支持 x64 构建")
endif()

# 多配置生成器只保留 Release。
if(CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_CONFIGURATION_TYPES "Release" CACHE STRING "" FORCE)
endif()

# 单配置生成器也统一为 Release。
set(CMAKE_BUILD_TYPE "Release" CACHE STRING "" FORCE)

# MSVC 下统一常用编译选项。
if(MSVC)
    add_compile_options(
        /utf-8
        /EHsc
        /wd4003
    )

    # 避免 Windows 头文件污染 min/max 宏。
    add_compile_definitions(NOMINMAX)
endif()

# 在 Visual Studio 中把主程序设为默认启动目标。
if(CMAKE_GENERATOR MATCHES "Visual Studio")
    set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT LiuRanqi)
endif()
