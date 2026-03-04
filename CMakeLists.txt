# 要求 CMake 最低版本为 3.20；低于此版本会直接报错并停止配置。
cmake_minimum_required(VERSION 3.20)

# 定义工程：
# - 工程名：LiuRanqi（会影响 VS 里显示名、一些默认变量前缀等）
# - 版本号：0.0.1（可用于打包/输出版本信息）
# - 语言：C 与 CXX（仅启用 C/C++ 编译器，不启用其他语言）
project(LiuRanqi VERSION 0.0.1 LANGUAGES C CXX)

# 统一 C++ 标准为 C++20（作用于当前目录及其子目录目标，除非子目标显式覆盖）。
set(CMAKE_CXX_STANDARD 20)
# 强制要求必须支持 C++20；若编译器不支持则配置阶段失败，避免“降级编译”带来的隐性问题。
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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

# =========================
# 输出目录统一
# =========================

# BIN_OUTPUT_DIR：所有目标（exe/lib/dll）统一输出目录，避免分散到各子目录。
set(BIN_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/生成")
# CMAKE_RUNTIME_OUTPUT_DIRECTORY：可执行文件（.exe）默认输出目录。
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${BIN_OUTPUT_DIR}")
# CMAKE_LIBRARY_OUTPUT_DIRECTORY：动态库（.dll/.so）默认输出目录。
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${BIN_OUTPUT_DIR}")
# CMAKE_ARCHIVE_OUTPUT_DIRECTORY：静态库（.lib/.a）默认输出目录。
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/静态库")
# 以下 3 项是 Release 专用输出目录，进一步确保 Release 一定落到同一路径。
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${BIN_OUTPUT_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE "${BIN_OUTPUT_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/静态库")

# =========================
# 第三方路径（父工程统一定义）
# =========================

# SDL3_ROOT：
# - 默认值：源码根目录同级 ../SDL3
# - CACHE PATH：允许在 CMake GUI/VS 中覆盖路径
# - "SDL3路径"：缓存项说明文本
set(SDL3_ROOT "${CMAKE_SOURCE_DIR}/../SDL3" CACHE PATH "SDL3路径")

# CEF_ROOT：
# - 默认值：源码根目录同级 ../CEF
# - CACHE PATH：允许外部覆盖
set(CEF_ROOT "${CMAKE_SOURCE_DIR}/../CEF" CACHE PATH "CEF路径")

# 转绝对路径，防止相对路径导致 include/link 行为不一致。
get_filename_component(SDL3_ROOT "${SDL3_ROOT}" ABSOLUTE)
get_filename_component(CEF_ROOT "${CEF_ROOT}" ABSOLUTE)

# # 路径健壮性检查：用“关键头文件是否存在”来确认路径有效。
# if(NOT EXISTS "${SDL3_ROOT}/include/SDL3/SDL.h")
#     message(FATAL_ERROR "未找到SDL3头文件: ${SDL3_ROOT}/include/SDL3/SDL.h")
# endif()
# if(NOT EXISTS "${CEF_ROOT}/include/cef_app.h")
#     message(FATAL_ERROR "未找到CEF头文件: ${CEF_ROOT}/include/cef_app.h")
# endif()

# =========================
# find_package 搜索路径（仅父工程设置）
# =========================

# CMAKE_MODULE_PATH：用于 find_package(CEF) 这种 Module 模式搜索 FindXXX.cmake。
list(APPEND CMAKE_MODULE_PATH "${CEF_ROOT}/cmake")
# CMAKE_PREFIX_PATH：用于 find_package(SDL3 CONFIG) 的 Config 模式搜索。
list(APPEND CMAKE_PREFIX_PATH "${SDL3_ROOT}/cmake")

# =========================
# 子工程复用的 include / link 变量
# =========================

# # 链接前置检查：主库和 wrapper 库都必须存在。
# if(NOT EXISTS "${CEF_ROOT}/Release/libcef.lib")
#     message(FATAL_ERROR "未找到 libcef: ${CEF_ROOT}/Release/libcef.lib")
# endif()
# if(NOT EXISTS "${CEF_WRAPPER_LIB}")
#     message(FATAL_ERROR "未找到 libcef_dll_wrapper: ${CEF_WRAPPER_LIB}")
# endif()

# PROJECT_SHARED_INCLUDE_DIRS：子工程统一复用的 include 列表。
set(PROJECT_SHARED_INCLUDE_DIRS 
    "${CEF_ROOT}"
    "${CMAKE_SOURCE_DIR}/common"
    "${SDL3_ROOT}/include"
)
# 链接库集合。
set(PROJECT_LINK_LIBS
    "${SDL3_ROOT}/lib/x64/SDL3.lib"
    "${SDL3_ROOT}/lib/x64/SDL3_test.lib"
    opengl32
    "${CEF_ROOT}/Release/libcef.lib"
    "${CEF_ROOT}/out/build/x64-Release/libcef_dll_wrapper/libcef_dll_wrapper.lib"
)

# =========================
# 可复用函数：统一 MSVC 运行时参数
# =========================

# 函数名：project_apply_msvc_runtime
# 参数：
# - target_name：目标名（例如 CEF、SDL）
# 作用：
# - 统一设置 MSVC 运行时和宏定义，避免不同目标 ABI 不一致。
function(project_apply_msvc_runtime target_name)
    if(MSVC)
        # MSVC_RUNTIME_LIBRARY = MultiThreaded 对应 /MT（静态 CRT）。
        set_property(TARGET ${target_name} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
        # NDEBUG：关闭断言；_ITERATOR_DEBUG_LEVEL=0：与 release 三方库对齐迭代器级别。
        target_compile_definitions(${target_name} PRIVATE NDEBUG _ITERATOR_DEBUG_LEVEL=0)
        # /U_DEBUG：取消可能由外部环境带入的 _DEBUG 定义。
        target_compile_options(${target_name} PRIVATE /U_DEBUG)
    endif()
endfunction()

# =========================
# 运行时文件清单（拷贝到生成目录）
# =========================
#必须依赖的文件
set(PROJECT_RUNTIME_FILES
    "${SDL3_ROOT}/lib/x64/SDL3.dll"
    "${CEF_ROOT}/Release/chrome_elf.dll"
    "${CEF_ROOT}/Release/d3dcompiler_47.dll"
    "${CEF_ROOT}/Release/dxcompiler.dll"
    "${CEF_ROOT}/Release/dxil.dll"
    "${CEF_ROOT}/Release/libcef.dll"
    "${CEF_ROOT}/Release/libEGL.dll"
    "${CEF_ROOT}/Release/libGLESv2.dll"
    "${CEF_ROOT}/Release/v8_context_snapshot.bin"
    "${CEF_ROOT}/Release/vk_swiftshader.dll"
    "${CEF_ROOT}/Release/vk_swiftshader_icd.json"
    "${CEF_ROOT}/Release/vulkan-1.dll"
)

# =========================
# 可复用函数：拷贝 CEF 运行时
# =========================



# 函数名：project_copy_runtime
# 参数：
# - target_name：要附加 POST_BUILD 拷贝命令的目标名
# 作用：
# - 构建后把 CEF 运行依赖复制到目标输出目录。

function(project_copy_runtime target_name)
    # 遍历运行时文件列表；仅当文件存在时才添加拷贝命令，避免无意义报错。
    foreach(cp_file IN LISTS PROJECT_RUNTIME_FILES)
        if(EXISTS "${cp_file}")
            add_custom_command(
                # TARGET：把命令绑定到哪个目标。
                TARGET ${target_name}
                # POST_BUILD：目标构建完成后执行。
                POST_BUILD
                # COMMAND：使用 cmake -E copy_if_different，只有文件变化才拷贝，减少 I/O。
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    # 源文件（当前循环项）。
                    "${cp_file}"
                    # 目标目录（生成表达式，按目标名动态解析输出目录）。
                    "$<TARGET_FILE_DIR:${target_name}>"
                # COMMENT：构建输出里的提示文本。
                COMMENT "复制CEF运行库: ${cp_file}"
            )
        endif()
    endforeach()

    # CEF 的 Resources 目录通常也需要和 exe 同目录。
    if(IS_DIRECTORY "${CEF_ROOT}/Resources")
        add_custom_command(
            TARGET ${target_name}
            POST_BUILD
            # copy_directory：目录整体复制。
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CEF_ROOT}/Resources"
                "$<TARGET_FILE_DIR:${target_name}>"
            COMMENT "复制CEF资源"
        )
    endif()
endfunction()

# =========================
# 子目录接入
# =========================

# 进入 CEF 子目录执行其 CMakeLists.txt（父变量会被子目录读取并复用）。
add_subdirectory(CEF)
# 进入 浏览器 子目录执行其 CMakeLists.txt。
add_subdirectory(SDL)

# 若生成器是 Visual Studio，则把默认启动目标设为 SDL（即 浏览器.exe 对应目标）。
if(CMAKE_GENERATOR MATCHES "Visual Studio")
    set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT SDL)
endif()
