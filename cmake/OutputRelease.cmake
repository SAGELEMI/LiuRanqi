# 显式列出主程序源码，避免使用 GLOB 带来的隐式重扫。
set(LRQ_SOURCES
    "${CMAKE_SOURCE_DIR}/main.cpp"
    "${CMAKE_SOURCE_DIR}/SDL/SdlRuntime.cpp"
    "${CMAKE_SOURCE_DIR}/SDL/SdlUi.cpp"
    "${CMAKE_SOURCE_DIR}/CEF/CEFCall.cpp"
)

# 创建主程序目标。
add_executable(LiuRanqi ${LRQ_SOURCES})

# 把最终 exe 文件名固定为 LiuRanQi.exe。
set_target_properties(LiuRanqi PROPERTIES OUTPUT_NAME "LiuRanQi")

# 复用 CEF 官方宏补齐 Windows 相关目标属性。
SET_EXECUTABLE_TARGET_PROPERTIES(LiuRanqi)

# 为主程序添加工程内与第三方头文件目录。
target_include_directories(LiuRanqi PRIVATE
    "${SDL3_ROOT}/include"
    "${SDL3_TTF_ROOT}/include"
    "${CEF_ROOT}"
    "${CMAKE_SOURCE_DIR}"
)

# 先给 CEF 主库设置一个默认路径。
set(LRQ_CEF_LIB "${CEF_ROOT}/Release/libcef.lib")

# 如果 FindCEF 导出了更精确的库路径，则优先使用导出的路径。
if(DEFINED CEF_LIB_RELEASE AND EXISTS "${CEF_LIB_RELEASE}")
    set(LRQ_CEF_LIB "${CEF_LIB_RELEASE}")
endif()

# 把主程序链接到 SDL3、SDL3_ttf、CEF 和系统库。
target_link_libraries(LiuRanqi PRIVATE
    SDL3::SDL3
    SDL3_ttf::SDL3_ttf
    opengl32
    "${LRQ_CEF_LIB}"
    libcef_dll_wrapper
    ${CEF_STANDARD_LIBS}
)

# MSVC 下固定运行时与发布宏，避免 ABI 不一致。
if(MSVC)
    set_property(TARGET LiuRanqi PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
    target_compile_definitions(LiuRanqi PRIVATE NDEBUG _ITERATOR_DEBUG_LEVEL=0)
    target_compile_options(LiuRanqi PRIVATE /U_DEBUG)
endif()

# 构建后复制 CEF 运行时文件。
PROJECT_CEF_COPY(LiuRanqi)

# 构建后复制演示页面目录。
if(EXISTS "${CMAKE_SOURCE_DIR}/demo")
    add_custom_command(
        TARGET LiuRanqi
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/demo" "$<TARGET_FILE_DIR:LiuRanqi>/demo"
        COMMENT "复制 JS 桥接演示页"
    )
endif()
