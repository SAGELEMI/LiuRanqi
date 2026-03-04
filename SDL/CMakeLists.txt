# CONFIG：显式要求 SDL3Config.cmake 配置包模式。
find_package(SDL3 CONFIG REQUIRED)

# 收集 浏览器 子目录下所有 .cpp（新增文件后会自动触发重新配置）。
file(GLOB BROWSER_SRC CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

# 定义 SDL 可执行目标（内部 target 名为 SDL）。
add_executable(SDL ${BROWSER_SRC})
# 输出文件名固定为 浏览器.exe。
set_target_properties(SDL PROPERTIES OUTPUT_NAME "浏览器")
# 加入当前源码目录与父工程统一 include 目录（common + CEF include）。
target_include_directories(SDL PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    ${PROJECT_SHARED_INCLUDE_DIRS}
)
# 使用父工程统一维护的 SDL 链接库集合。
target_link_libraries(SDL PRIVATE ${PROJECT_LINK_LIBS})
# 声明构建顺序：编译 SDL 前先构建 CEF。
add_dependencies(SDL CEF)

# 统一应用 MSVC 运行时和编译选项（父工程函数）。
project_apply_msvc_runtime(SDL)
# 统一拷贝 SDL 运行时文件（父工程函数）。
project_copy_runtime(SDL)
