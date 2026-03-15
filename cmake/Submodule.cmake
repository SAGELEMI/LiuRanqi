# 引入 FetchContent 模块，后续所有第三方依赖都通过它接入。
include(FetchContent)

# 允许用空字符串显式表示“不要初始化任何子模块”。
cmake_policy(SET CMP0097 NEW)

# 把 FetchContent 生成物统一放到 third_party，避免污染源码缓存目录。
set(FETCHCONTENT_BASE_DIR "${CMAKE_SOURCE_DIR}/third_party" CACHE PATH "FetchContent缓存目录" FORCE)

# 确保缓存目录存在。
file(MAKE_DIRECTORY "${FETCHCONTENT_BASE_DIR}")

# 打印实际使用的缓存目录，便于排查路径问题。
message(STATUS "FetchContent 缓存目录 = ${FETCHCONTENT_BASE_DIR}")

# 关闭静默模式，让 VS 的 CMake 输出能够看到下载过程。
set(FETCHCONTENT_QUIET OFF CACHE BOOL "显示 FetchContent 下载输出" FORCE)

# 统一 Git 下载参数。
# `http.version=HTTP/1.1` 主要用于缓解部分网络环境下的 TLS 连接重置问题。
set(LRQ_FETCH_GIT_CONFIG
    "advice.detachedHead=false"
    "http.version=HTTP/1.1"
    "http.sslVerify=false"
)

# 如果本地已有可复用源码，则直接指向该源码目录。
function(LRQ_REUSE_FETCHCONTENT_SOURCE dep_name source_dir marker_relpath)
    # FetchContent 的源码覆盖变量名要求是大写依赖名。
    string(TOUPPER "${dep_name}" dep_name_upper)

    # 用一个关键文件判断本地源码是否完整。
    set(reuse_marker "${source_dir}/${marker_relpath}")

    # 源码完整时，强制复用本地目录。
    if(EXISTS "${reuse_marker}")
        set("FETCHCONTENT_SOURCE_DIR_${dep_name_upper}" "${source_dir}" CACHE PATH "复用 ${dep_name} 本地源码目录" FORCE)
        message(STATUS "复用本地依赖源码: ${dep_name} -> ${source_dir}")
    else()
        unset("FETCHCONTENT_SOURCE_DIR_${dep_name_upper}" CACHE)
    endif()
endfunction()

# 声明一个普通的 Git 依赖。
function(LRQ_DECLARE_GIT_DEP dep_name repo_url repo_tag)
    FetchContent_Declare(
        ${dep_name}          # 1. 依赖名字（比如 fmt、spdlog、yaml-cpp）
        GIT_REPOSITORY ${repo_url}  # 2. Git 仓库地址（https/ssh）
        GIT_TAG        ${repo_tag}  # 3. 锁定版本（tag/分支/commit 哈希）
        GIT_SHALLOW TRUE            # 4. 浅克隆：只下载最新代码，不拉历史（超快）
        GIT_PROGRESS TRUE           # 5. 显示下载进度条，看得见在干嘛
        GIT_CONFIG ${LRQ_FETCH_GIT_CONFIG}  # 6. 自定义 Git 配置（代理/加速/证书）
        GIT_REMOTE_UPDATE_STRATEGY CHECKOUT  # 7. 强制切到指定版本，不合并
        UPDATE_DISCONNECTED TRUE    # 8. 离线模式：不联网检查更新，只使用本地已下载
    )
endfunction()

# 声明一个需要子模块的 Git 依赖。
function(LRQ_DECLARE_GIT_DEP_WITH_SUBMODULES dep_name repo_url repo_tag)
    FetchContent_Declare(
        ${dep_name}          # 依赖项名称（变量传入）
        GIT_REPOSITORY ${repo_url}  # Git 仓库地址
        GIT_TAG        ${repo_tag}  # Git 标签/分支/提交哈希
        GIT_SHALLOW TRUE            # 浅克隆（只拉取指定提交，不下载完整历史）
        GIT_PROGRESS TRUE           # 显示克隆进度
        GIT_CONFIG ${LRQ_FETCH_GIT_CONFIG}  # 自定义 Git 配置
        GIT_REMOTE_UPDATE_STRATEGY CHECKOUT # 远程更新策略：强制检出指定标签
        UPDATE_DISCONNECTED TRUE    # 离线模式，禁止自动更新依赖
        GIT_SUBMODULES ${ARGN}      # 仅拉取指定的 Git 子模块
        GIT_SUBMODULES_RECURSE FALSE # 不递归拉取子模块
    )
endfunction()

# 先尝试复用本地 SDL3 源码目录。
LRQ_REUSE_FETCHCONTENT_SOURCE(SDL3 "${CMAKE_SOURCE_DIR}/third_party/sdl3-src" "CMakeLists.txt")

# 声明 SDL3 的拉取方式。
LRQ_DECLARE_GIT_DEP(SDL3 "https://github.com/libsdl-org/SDL.git" "release-3.4.2")

# 接入 SDL3。
FetchContent_MakeAvailable(SDL3)

# 取回 SDL3 实际源码目录，供主工程 include 使用。
FetchContent_GetProperties(SDL3 SOURCE_DIR SDL3_ROOT)

# 主工程明确依赖 SDL3::SDL3 这个目标。
if(NOT TARGET SDL3::SDL3)
    message(FATAL_ERROR "缺少 SDL3::SDL3 目标，请检查 SDL3 拉取或配置结果。")
endif()

# 配置 SDL3_ttf 只使用必需依赖。
set(SDLTTF_VENDORED ON CACHE BOOL "SDL_ttf use vendored dependencies" FORCE)
set(SDLTTF_HARFBUZZ OFF CACHE BOOL "Disable harfbuzz text shaping" FORCE)
set(SDLTTF_PLUTOSVG OFF CACHE BOOL "Disable plutosvg color emoji backend" FORCE)
set(SDLTTF_SAMPLES OFF CACHE BOOL "Disable SDL_ttf samples" FORCE)

# 先尝试复用本地 SDL3_ttf 源码目录，并要求 freetype 子模块已经存在。
LRQ_REUSE_FETCHCONTENT_SOURCE(SDL3_ttf "${CMAKE_SOURCE_DIR}/third_party/sdl3_ttf-src" "external/freetype/CMakeLists.txt")

# 声明 SDL3_ttf 的拉取方式，并只初始化必需的 freetype 子模块。
LRQ_DECLARE_GIT_DEP_WITH_SUBMODULES(SDL3_ttf "https://github.com/libsdl-org/SDL_ttf.git" "release-3.2.2" external/freetype)

# 接入 SDL3_ttf。
FetchContent_MakeAvailable(SDL3_ttf)

# 取回 SDL3_ttf 实际源码目录，供主工程 include 使用。
FetchContent_GetProperties(SDL3_ttf SOURCE_DIR SDL3_TTF_ROOT)

# 主工程明确依赖 SDL3_ttf::SDL3_ttf 这个目标。
if(NOT TARGET SDL3_ttf::SDL3_ttf)
    message(FATAL_ERROR "缺少 SDL3_ttf::SDL3_ttf 目标，请检查 SDL3_ttf 或 freetype 拉取结果。")
endif()

# 先尝试复用本地 CEF 源码目录。
LRQ_REUSE_FETCHCONTENT_SOURCE(CEF "${CMAKE_SOURCE_DIR}/third_party/cef-src" "Release/libcef.dll")

# 声明 CEF 的下载方式。
FetchContent_Declare(
    CEF
    URL https://cef-builds.spotifycdn.com/cef_binary_145.0.26%2Bg6ed7554%2Bchromium-145.0.7632.110_windows64_minimal.tar.bz2
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    DOWNLOAD_NO_PROGRESS FALSE
    TLS_VERIFY ON
    INACTIVITY_TIMEOUT 120
    TIMEOUT 1800
)

# CEF 顶层脚本会顺带创建 libcef_dll_wrapper。
# 这里先临时关闭 BUILD_SHARED_LIBS，强制 wrapper 生成为静态库。
set(LRQ_PREV_BUILD_SHARED_LIBS "${BUILD_SHARED_LIBS}")
set(BUILD_SHARED_LIBS OFF)
FetchContent_MakeAvailable(CEF)
set(BUILD_SHARED_LIBS "${LRQ_PREV_BUILD_SHARED_LIBS}")
unset(LRQ_PREV_BUILD_SHARED_LIBS)

# 取回 CEF 实际源码目录。
FetchContent_GetProperties(CEF SOURCE_DIR CEF_ROOT)

# 打印依赖路径，便于排查是否命中了本地源码复用。
message(STATUS "SDL3 路径 = ${SDL3_ROOT}")
message(STATUS "SDL3_ttf 路径 = ${SDL3_TTF_ROOT}")
message(STATUS "CEF 路径 = ${CEF_ROOT}")

# 让 find_package(CEF) 可以找到 CEF 自带的 FindCEF.cmake。
list(APPEND CMAKE_MODULE_PATH "${CEF_ROOT}/cmake")

# 载入 CEF 官方导出的变量和宏。
find_package(CEF REQUIRED)           

# CEF 必须导出 libcef_dll_wrapper 目标，供主程序静态链接。
if(NOT TARGET libcef_dll_wrapper)
    message(FATAL_ERROR "缺少 libcef_dll_wrapper 目标，请检查 CEF 配置结果。")
endif()

# 构建后复制 CEF 运行时文件到主程序目录。
function(PROJECT_CEF_COPY target_name)
    # 遍历 Release 目录下的运行时文件，排除 .lib 与 .exe。
    if(IS_DIRECTORY "${CEF_ROOT}/Release")
        file(GLOB cef_release_entries CONFIGURE_DEPENDS "${CEF_ROOT}/Release/*")
        foreach(entry IN LISTS cef_release_entries)
            if(NOT IS_DIRECTORY "${entry}" AND NOT entry MATCHES "\\.(lib|exe)$")
                add_custom_command(
                    TARGET ${target_name}
                    POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${entry}" "$<TARGET_FILE_DIR:${target_name}>"
                    COMMENT "复制 CEF 运行时文件: ${entry}"
                )
            endif()
        endforeach()
    endif()

    # 构建后整体复制 CEF 资源目录。
    if(IS_DIRECTORY "${CEF_ROOT}/Resources")
        add_custom_command(
            TARGET ${target_name}
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CEF_ROOT}/Resources" "$<TARGET_FILE_DIR:${target_name}>"
            COMMENT "复制 CEF 资源"
        )
    endif()
endfunction()
