# REQUIRED：找不到会直接终止配置，避免后续半配置状态。
find_package(CEF REQUIRED)

# 收集 CEF 子目录下所有 .cpp（新增文件后会自动触发重新配置）。
file(GLOB CEF_SRC CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

# 定义 CEF 可执行目标。
add_executable(CEF ${CEF_SRC})
# 生成文件名固定为 CEF.exe（避免受 target 名变化影响）。
set_target_properties(CEF PROPERTIES OUTPUT_NAME "CEF")
# 加入当前源码目录与父工程统一 include 目录（common + CEF include）。
target_include_directories(CEF PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    ${PROJECT_SHARED_INCLUDE_DIRS}
)
# 使用父工程统一维护的 CEF 链接库集合。
target_link_libraries(CEF PRIVATE ${PROJECT_LINK_LIBS})

# 统一应用 MSVC 运行时和编译选项（父工程函数）。
project_apply_msvc_runtime(CEF)
