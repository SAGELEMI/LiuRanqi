set(SDLTTF_ROOT_REL "third_party/SDL_ttf") # 供子工程使用的相对路径。
set(SDLTTF_ROOT "${CMAKE_SOURCE_DIR}/${SDLTTF_ROOT_REL}" CACHE PATH "SDL_ttf源码目录") # 完整路径，可外部覆盖。
get_filename_component(SDLTTF_ROOT "${SDLTTF_ROOT}" ABSOLUTE) # 绝对路径。

set(SDLTTF_HEADER_FILE "${SDLTTF_ROOT}/include/SDL3_ttf/SDL_ttf.h") # 用 SDL_ttf 主头文件判断是否可用。
set(SDLTTF_GIT_URL "https://github.com/libsdl-org/SDL_ttf.git") # SDL_ttf 官方 Git 仓库 URL。

# 函数名：SDLTTF_GIT_EXEC
# 参数：RC_VAR 接收返回码的变量名，OUT_VAR 接收标准输出的变量名，ERR_VAR 接收标准错误的变量名，ARGN 是传递给 git 的参数列表。
# 作用：执行 git 命令并捕获结果，设置指定变量以供调用者使用。
function(SDLTTF_GIT_EXEC RC_VAR OUT_VAR ERR_VAR)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            "GIT_TERMINAL_PROMPT=0"
            "GCM_INTERACTIVE=Never"
            "${GIT_EXECUTABLE}" ${ARGN} # 执行控制台命令 git。
        RESULT_VARIABLE _RC
        OUTPUT_VARIABLE _OUT
        ERROR_VARIABLE _ERR
        OUTPUT_STRIP_TRAILING_WHITESPACE # 去掉输出末尾多余换行 / 空格，让字符串干净。
        ERROR_STRIP_TRAILING_WHITESPACE
    )
    set(${RC_VAR} "${_RC}" PARENT_SCOPE) # CMake 函数向外传值的标准写法。
    set(${OUT_VAR} "${_OUT}" PARENT_SCOPE)
    set(${ERR_VAR} "${_ERR}" PARENT_SCOPE)
endfunction()

function(SDLTTF_ENSURE_EXTERNAL_DEP dep_name dep_url dep_branch)
    set(_dep_dir "${SDLTTF_ROOT}/external/${dep_name}")
    set(_dep_cmake "${_dep_dir}/CMakeLists.txt")

    if(NOT EXISTS "${_dep_cmake}")
        if(EXISTS "${_dep_dir}")
            file(REMOVE_RECURSE "${_dep_dir}") # 清理空目录或不完整目录。
        endif()

        message(STATUS "SDL_ttf external/${dep_name} 不存在，尝试 git 拉取...")
        SDLTTF_GIT_EXEC(
            _dep_clone_rc _dep_clone_out _dep_clone_err
            -c http.version=HTTP/1.1
            -C "${CMAKE_SOURCE_DIR}"
            -c http.sslVerify=false clone --depth 1 --branch "${dep_branch}" "${dep_url}" "${_dep_dir}"
        )
        if(NOT _dep_clone_rc EQUAL 0)
            message(FATAL_ERROR "SDL_ttf 外部依赖 ${dep_name} 拉取失败。clone stderr: ${_dep_clone_err}")
        endif()

        if(NOT EXISTS "${_dep_cmake}")
            message(FATAL_ERROR "SDL_ttf 外部依赖 ${dep_name} 不完整：缺少 ${_dep_cmake}")
        endif()
    endif()
endfunction()

if(NOT GIT_EXECUTABLE)
    find_package(Git REQUIRED) # 后续 external 依赖补齐需要 git。
endif()

if(NOT EXISTS "${SDLTTF_HEADER_FILE}") # 判断是否有 SDL_ttf 头文件。
    # 清理 SDL_ttf 残留。
    if(EXISTS "${SDLTTF_ROOT}")
        message(STATUS "检测到 SDL_ttf 残留，自动清理: ${SDLTTF_ROOT}")
        file(REMOVE_RECURSE "${SDLTTF_ROOT}") # 删除整个目录，避免后续 git clone 失败。
    endif()
    # 清理 SDL_ttf 子模块元数据。
    if(EXISTS "${CMAKE_SOURCE_DIR}/.git/modules/${SDLTTF_ROOT_REL}")
        message(STATUS "检测到 SDL_ttf 模块元数据，自动清理: .git/modules/${SDLTTF_ROOT_REL}")
        file(REMOVE_RECURSE "${CMAKE_SOURCE_DIR}/.git/modules/${SDLTTF_ROOT_REL}") # 删除子模块元数据，避免 clone 失败。
    endif()

    message(STATUS "SDL_ttf 不存在，尝试 git 拉取...")
    SDLTTF_GIT_EXEC(
        _clone_rc _clone_out _clone_err
        -c http.version=HTTP/1.1
        -C "${CMAKE_SOURCE_DIR}"
        -c http.sslVerify=false clone --depth 1 "${SDLTTF_GIT_URL}" "${SDLTTF_ROOT_REL}"
    )
    # git clone 失败时，直接报错终止（SDL_ttf 是必选依赖）。
    if(NOT _clone_rc EQUAL 0)
        message(FATAL_ERROR "SDL_ttf 拉取失败。clone stderr: ${_clone_err}")
    endif()
endif()

if(NOT EXISTS "${SDLTTF_HEADER_FILE}") # 再次校验 SDL_ttf 头文件，防止假成功。
    message(FATAL_ERROR "SDL_ttf 仍不可用：${SDLTTF_HEADER_FILE}")
endif()

# SDL_ttf 为必选：强制使用 vendored freetype；关闭可选增强依赖，避免额外网络拉取失败。
set(SDLTTF_VENDORED ON CACHE BOOL "SDL_ttf use vendored dependencies" FORCE)
set(SDLTTF_HARFBUZZ OFF CACHE BOOL "Disable harfbuzz text shaping for minimal required integration" FORCE)
set(SDLTTF_PLUTOSVG OFF CACHE BOOL "Disable plutosvg color emoji backend for minimal required integration" FORCE)
set(SDLTTF_SAMPLES OFF CACHE BOOL "Disable SDL_ttf samples" FORCE)

# 基础文字渲染依赖（必需）。
SDLTTF_ENSURE_EXTERNAL_DEP("freetype" "https://github.com/libsdl-org/freetype.git" "VER-2-13-3-SDL")

# 可选依赖按开关拉取（当前默认关闭）。
if(SDLTTF_HARFBUZZ)
    SDLTTF_ENSURE_EXTERNAL_DEP("harfbuzz" "https://github.com/libsdl-org/harfbuzz.git" "8.5.0-SDL")
endif()
if(SDLTTF_PLUTOSVG)
    SDLTTF_ENSURE_EXTERNAL_DEP("plutosvg" "https://github.com/libsdl-org/plutosvg.git" "v0.0.7-SDL")
    SDLTTF_ENSURE_EXTERNAL_DEP("plutovg" "https://github.com/libsdl-org/plutovg.git" "v1.1.0-SDL")
endif()

if(NOT EXISTS "${SDLTTF_ROOT}/CMakeLists.txt") # SDL_ttf 源码目录必须有自己的 CMakeLists。
    message(FATAL_ERROR "SDL_ttf 子模块/源码不完整：缺少 ${SDLTTF_ROOT}/CMakeLists.txt")
endif()

add_subdirectory("${SDLTTF_ROOT}" "${CMAKE_BINARY_DIR}/${SDLTTF_ROOT_REL}" EXCLUDE_FROM_ALL) # 把 SDL_ttf 源码接入本工程。

set(SDLTTF_LINK_TARGET "") # 初始化 SDL_ttf 链接目标变量。
set(SDLTTF_RUNTIME_TARGET "") # 初始化 SDL_ttf 运行时目标变量（动态库时用于复制 DLL）。

if(TARGET SDL3_ttf::SDL3_ttf) # 优先匹配最常见目标名。
    set(SDLTTF_LINK_TARGET SDL3_ttf::SDL3_ttf)
elseif(TARGET SDL3_ttf::SDL3_ttf-shared) # 次优先：命名空间 shared 目标。
    set(SDLTTF_LINK_TARGET SDL3_ttf::SDL3_ttf-shared)
    set(SDLTTF_RUNTIME_TARGET SDL3_ttf::SDL3_ttf-shared)
elseif(TARGET SDL3_ttf-shared) # 兼容无命名空间 shared 目标。
    set(SDLTTF_LINK_TARGET SDL3_ttf-shared)
    set(SDLTTF_RUNTIME_TARGET SDL3_ttf-shared)
elseif(TARGET SDL3_ttf::SDL3_ttf-static) # 兼容命名空间 static 目标。
    set(SDLTTF_LINK_TARGET SDL3_ttf::SDL3_ttf-static)
elseif(TARGET SDL3_ttf-static) # 兼容无命名空间 static 目标。
    set(SDLTTF_LINK_TARGET SDL3_ttf-static)
endif()

if(NOT SDLTTF_LINK_TARGET) # 没有识别到 SDL_ttf target，直接报错（必选依赖）。
    message(FATAL_ERROR "未识别 SDL3_ttf CMake 目标名，请检查 SDL_ttf 子模块版本。")
endif()

# 函数名：PROJECT_SDLTTF_COPY
# 参数：
# - target_name：要附加 POST_BUILD 拷贝命令的目标名。
# 作用：
# - 构建后把 SDL_ttf 运行依赖复制到目标输出目录。
function(PROJECT_SDLTTF_COPY target_name)
    if(SDLTTF_RUNTIME_TARGET) # 动态链接 SDL_ttf 时才需要复制 SDL3_ttf.dll。
        add_custom_command(
            TARGET ${target_name}
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:${SDLTTF_RUNTIME_TARGET}>"
                "$<TARGET_FILE_DIR:${target_name}>"
            COMMENT "复制 SDL3_ttf 运行库"
        )
    endif()
endfunction()
