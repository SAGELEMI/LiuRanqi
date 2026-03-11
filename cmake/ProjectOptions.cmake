# 统一 C++ 标准为 C++20（作用于当前目录及其子目录目标，除非子目标显式覆盖）。
set(CMAKE_CXX_STANDARD 20)
# 强制要求必须支持 C++20；若编译器不支持则配置阶段失败，避免“降级编译”带来的隐性问题。
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# 关闭 CMake 自动重检生成规则，避免当前工程和三方目录的大量 GLOB 在每次构建前阻塞。
set(CMAKE_SUPPRESS_REGENERATION ON)

# =========================
# 构建维度约束（架构/配置）
# =========================

# CMAKE_SIZEOF_VOID_P 表示指针字节数：
# - 8 表示 64 位
# - 4 表示 32 位
# 这里要求必须 64 位构建（x64）。
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "只支持 x64 构建")
endif()

# CMAKE_CONFIGURATION_TYPES 常见于多配置生成器（如 Visual Studio）：
# - 例如可包含 Debug/Release/RelWithDebInfo 等
# 这里强制只保留 Release，避免误用 Debug 与第三方二进制不匹配。
if(CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_CONFIGURATION_TYPES "Release" CACHE STRING "" FORCE)
endif()

# CMAKE_BUILD_TYPE 常见于单配置生成器（如 Ninja）：
# 这里也强制为 Release，保证单配置与多配置行为一致。
set(CMAKE_BUILD_TYPE "Release" CACHE STRING "" FORCE)

# =========================
# MSVC 编译器通用选项
# =========================
if(MSVC)
    add_compile_options(
        # /utf-8：源文件按 UTF-8 解析，避免中文注释/字符串乱码问题。
        /utf-8
        # /EHsc：启用标准 C++ 异常模型。
        /EHsc
        # /wd4003：关闭 C4003 警告（宏参数数量不匹配相关）。
        /wd4003
    )
    # 定义 NOMINMAX：防止 Windows 头文件定义 min/max 宏污染 std::min/std::max。
    add_compile_definitions(NOMINMAX)
endif()

# 若生成器是 Visual Studio，则把默认启动目标设为 LiuRanqi（即 LiuRanQi.exe 对应目标）。
if(CMAKE_GENERATOR MATCHES "Visual Studio")
    set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT LiuRanqi)
endif()
