# 主工程源码列表直接显式维护，避免源码变动时依赖 GLOB 重检。
set(LRQ_SRC
    "${CMAKE_SOURCE_DIR}/main.cpp"
    "${CMAKE_SOURCE_DIR}/SDL/SdlRuntime.cpp"
    "${CMAKE_SOURCE_DIR}/SDL/SdlUi.cpp"
    "${CMAKE_SOURCE_DIR}/CEF/CEFCall.cpp"
)

# 定义可执行目标。
add_executable(LiuRanqi ${LRQ_SRC})
# 输出文件名固定为 LiuRanQi.exe。
set_target_properties(LiuRanqi PROPERTIES OUTPUT_NAME "LiuRanQi")
# 复用 CEF 官方目标属性，补齐编译选项、包含目录和 Windows 链接参数。
SET_EXECUTABLE_TARGET_PROPERTIES(LiuRanqi)

# 加入 include 目录。
target_include_directories(LiuRanqi PRIVATE
    "${SDL_ROOT}/include"
    "${CEF_ROOT}"
    "${CMAKE_SOURCE_DIR}/SDL"
    "${CMAKE_SOURCE_DIR}/CEF"
    "${CMAKE_SOURCE_DIR}/common"
)

# 链接库集合。
# 优先使用 CEF 配置导出的库路径（由 find_package(CEF) 提供），不存在时回退到默认 Release 路径。
set(_LRQ_CEF_LIB "${CEF_ROOT}/Release/libcef.lib")
if(DEFINED CEF_LIB_RELEASE AND EXISTS "${CEF_LIB_RELEASE}")
    set(_LRQ_CEF_LIB "${CEF_LIB_RELEASE}")
endif()

# SDL_ttf 由 SDLTtfSubmodule.cmake 保证可用，这里直接强制校验。
if(NOT SDLTTF_LINK_TARGET)
    message(FATAL_ERROR "SDL_ttf 为必选依赖，但未配置出 SDLTTF_LINK_TARGET。")
endif()

target_link_libraries(LiuRanqi PRIVATE 
    ${SDL_LINK_TARGET} # SDL3 使用 target 方式链接，不要硬编码 SDL3.lib 路径。
    ${SDLTTF_LINK_TARGET} # SDL3_ttf 必选依赖。
    opengl32  # 你的 OpenGL 系统库。
    "${_LRQ_CEF_LIB}" # CEF 主库。
    libcef_dll_wrapper # 由当前工程自动构建的 CEF wrapper 静态库。
    ${CEF_STANDARD_LIBS} # CEF 官方导出的 Windows 标准库与 delayimp 依赖。
)

# MSVC 运行时参数
# 统一设置 MSVC 运行时和宏定义，避免不同目标 ABI 不一致。
if(MSVC)
    # MSVC_RUNTIME_LIBRARY = MultiThreaded 对应 /MT（静态 CRT）。
    set_property(TARGET LiuRanqi PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
    # NDEBUG：关闭断言；_ITERATOR_DEBUG_LEVEL=0：与 release 三方库对齐迭代器级别。
    target_compile_definitions(LiuRanqi PRIVATE NDEBUG _ITERATOR_DEBUG_LEVEL=0)
    # /U_DEBUG：取消可能由外部环境带入的 _DEBUG 定义。
    target_compile_options(LiuRanqi PRIVATE /U_DEBUG)
endif()
# 拷贝SDL 运行时文件（如果是动态链接的话）。
PROJECT_SDL_COPY(LiuRanqi)
# 拷贝 SDL_ttf 运行时文件（如果是动态链接的话）。
PROJECT_SDLTTF_COPY(LiuRanqi)
# 拷贝CEF 运行时文件（如果是动态链接的话）。
PROJECT_CEF_COPY(LiuRanqi)

# 拷贝演示用 HTML，方便 SDL 直接加载本地双向通信示例页。
if(EXISTS "${CMAKE_SOURCE_DIR}/demo")
    add_custom_command(
        TARGET LiuRanqi
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/demo"
            "$<TARGET_FILE_DIR:LiuRanqi>/demo"
        COMMENT "复制 JS 桥接演示页"
    )
endif()
