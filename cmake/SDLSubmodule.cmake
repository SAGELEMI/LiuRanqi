set(SDL_ROOT_REL "third_party/SDL") # 供子工程使用的相对路径。
set(SDL_ROOT "${CMAKE_SOURCE_DIR}/${SDL_ROOT_REL}" CACHE PATH "SDL源码目录") # 完整路径，可外部覆盖。
get_filename_component(SDL_ROOT "${SDL_ROOT}" ABSOLUTE) # 绝对路径。

set(SDL_HEADER_FILE "${SDL_ROOT}/include/SDL3/SDL.h") # 用 SDL 主头文件判断是否可用。
set(SDL_GIT_URL "https://github.com/libsdl-org/SDL.git") # SDL 官方 Git 仓库 URL。
#set(SDL_GIT_TIMEOUT_SEC 120) # Git 命令超时时间（秒），避免网络问题导致的长时间卡住。

# 函数名：SDL_GIT_EXEC
# 参数：RC_VAR 接收返回码的变量名，OUT_VAR 接收标准输出的变量名，ERR_VAR 接收标准错误的变量名，ARGN 是传递给 git 的参数列表。
# 作用：执行 git 命令并捕获结果，设置指定变量以供调用者使用。
function(SDL_GIT_EXEC RC_VAR OUT_VAR ERR_VAR)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            "GIT_TERMINAL_PROMPT=0"
            "GCM_INTERACTIVE=Never"
            "${GIT_EXECUTABLE}" ${ARGN} # 执行控制台命令 git。
        RESULT_VARIABLE _RC
        OUTPUT_VARIABLE _OUT
        ERROR_VARIABLE _ERR
        OUTPUT_STRIP_TRAILING_WHITESPACE #去掉输出末尾多余换行 / 空格，让字符串干净。
        ERROR_STRIP_TRAILING_WHITESPACE
        #TIMEOUT ${SDL_GIT_TIMEOUT_SEC} # 取消计时器
    )
    set(${RC_VAR} "${_RC}" PARENT_SCOPE) #CMake 函数向外传值的标准写法
    set(${OUT_VAR} "${_OUT}" PARENT_SCOPE)
    set(${ERR_VAR} "${_ERR}" PARENT_SCOPE)
endfunction()

if(NOT EXISTS "${SDL_HEADER_FILE}") # 判断是否有SDL头文件
    find_package(Git REQUIRED) # 自动查找 Git 程序。

    # 清理SDL残留。
    if(EXISTS "${SDL_ROOT}")
        message(STATUS "检测到 SDL 残留，自动清理: ${SDL_ROOT}")
        file(REMOVE_RECURSE "${SDL_ROOT}") # 删除整个目录（无论是半初始化还是完整的旧版本），避免后续 git clone 失败。
    endif()
    # 清理SDL模块
    if(EXISTS "${CMAKE_SOURCE_DIR}/.git/modules/${SDL_ROOT_REL}")
        message(STATUS "检测到SDL模块元数据，自动清理: .git/modules/${SDL_ROOT_REL}")
        file(REMOVE_RECURSE "${CMAKE_SOURCE_DIR}/.git/modules/${SDL_ROOT_REL}") # 删除子模块元数据，避免 git clone 失败。
    endif()

    message(STATUS "SDL 不存在，尝试 git拉取...")
    SDL_GIT_EXEC(
        _clone_rc _clone_out _clone_err
        -c http.version=HTTP/1.1
        -C "${CMAKE_SOURCE_DIR}"
        -c http.sslVerify=false clone --depth 1 "${SDL_GIT_URL}" "${SDL_ROOT_REL}"
    )
    # git clone 失败时，可能是网络问题、Git 版本过旧不支持 --depth，或者其他原因。
    if(NOT _clone_rc EQUAL 0)
        message(FATAL_ERROR "SDL 拉取失败。clone stderr: ${_clone_err}")
    endif()

endif()

if(NOT EXISTS "${SDL_HEADER_FILE}") # 再次校验 SDL 头文件，防止假成功。
    message(FATAL_ERROR "SDL 仍不可用：${SDL_HEADER_FILE}")
endif()

if(NOT EXISTS "${SDL_ROOT}/CMakeLists.txt") # SDL3 源码目录必须有自己的 CMakeLists。
    message(FATAL_ERROR "SDL 子模块/源码不完整：缺少 ${SDL_ROOT}/CMakeLists.txt")
endif()

add_subdirectory("${SDL_ROOT}" "${CMAKE_BINARY_DIR}/${SDL_ROOT_REL}" EXCLUDE_FROM_ALL) # 把 SDL3 源码接入本工程（配置阶段就生效，VS 可提示）。

set(SDL_LINK_TARGET "") # 初始化 SDL3 链接目标变量。
set(SDL_RUNTIME_TARGET "") # 初始化 SDL3 运行时目标变量（动态库时用于复制 DLL）。

if(TARGET SDL3::SDL3) # 优先匹配最常见目标名。
    set(SDL_LINK_TARGET SDL3::SDL3) # 设定最终链接目标。
elseif(TARGET SDL3::SDL3-shared) # 次优先：命名空间 shared 目标。
    set(SDL_LINK_TARGET SDL3::SDL3-shared) # 链接 shared 目标。
    set(SDL_RUNTIME_TARGET SDL3::SDL3-shared) # 记录运行时目标。
elseif(TARGET SDL3-shared) # 兼容无命名空间 shared 目标。
    set(SDL_LINK_TARGET SDL3-shared) # 设定链接目标。
    set(SDL_RUNTIME_TARGET SDL3-shared) # 设定运行时目标。
elseif(TARGET SDL3::SDL3-static) # 兼容命名空间 static 目标。
    set(SDL_LINK_TARGET SDL3::SDL3-static) # 设定静态链接目标。
elseif(TARGET SDL3-static) # 兼容无命名空间 static 目标。
    set(SDL_LINK_TARGET SDL3-static) # 设定静态链接目标。
endif() # 结束目标名探测。

if(NOT SDL_LINK_TARGET) # 如果一个都没探测到，说明 SDL3 版本或选项不匹配。
    message(FATAL_ERROR "未识别 SDL3 CMake 目标名，请检查 SDL3 子模块版本。") # 直接失败，避免后续链接错误。
endif() # 结束 SDL3 target 可用性检查。

# 函数名：PROJECT_SDL_COPY
# 参数：
# - target_name：要附加 POST_BUILD 拷贝命令的目标名
# 作用：
# - 构建后把 SDL 运行依赖复制到目标输出目录。

function(PROJECT_SDL_COPY target_name)
    if(SDL_RUNTIME_TARGET) # 动态链接 SDL3 时才需要复制 SDL3.dll。
        add_custom_command( # 添加构建后命令。
            TARGET ${target_name} # 绑定目标。
            POST_BUILD # 构建后执行。
            COMMAND ${CMAKE_COMMAND} -E copy_if_different # 差异复制。
                "$<TARGET_FILE:${SDL_RUNTIME_TARGET}>" # 源：SDL3.dll。
                "$<TARGET_FILE_DIR:${target_name}>" # 目标：程序输出目录。
            COMMENT "复制 SDL3 运行库" # 构建日志注释。
        ) # 自定义命令结束。
    endif() # 结束 SDL3 运行时分支。
endfunction()