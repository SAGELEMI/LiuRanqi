# CEF_ROOT：
# - 默认值：源码根目录下的 third_party/CEF
# - CACHE PATH：允许外部覆盖
set(CEF_ROOT "${CMAKE_SOURCE_DIR}/third_party/CEF" CACHE PATH "CEF路径")

# 转绝对路径，防止相对路径导致 include/link 行为不一致。
get_filename_component(CEF_ROOT "${CEF_ROOT}" ABSOLUTE)

# 预期的 CEF 关键文件（用它判断 CEF 是否已经可用）。
set(CEF_REQUIRED_FILE "${CEF_ROOT}/Release/libcef.dll")

# 固定下载地址（用户指定的 minimal 包）。
set(CEF_DOWNLOAD_URL "https://cef-builds.spotifycdn.com/cef_binary_145.0.26%2Bg6ed7554%2Bchromium-145.0.7632.110_windows64_minimal.tar.bz2")
set(CEF_DOWNLOAD_DIR "${CMAKE_BINARY_DIR}/downloads")
set(CEF_ARCHIVE_PATH "${CEF_DOWNLOAD_DIR}/cef_windows64_minimal.tar.bz2")
set(CEF_EXTRACT_DIR "${CMAKE_BINARY_DIR}/cef_extract")

# 若 CEF 不存在则自动下载并解压到 third_party/CEF。
if(NOT EXISTS "${CEF_REQUIRED_FILE}")
    message(STATUS "未检测到 CEF，开始自动下载并解压到: ${CEF_ROOT}")

    file(MAKE_DIRECTORY "${CEF_DOWNLOAD_DIR}")
    # CMake内置下载指令， 下载地址、保存路径、显示进度、开启 TLS 验证等。
    file(DOWNLOAD
        "${CEF_DOWNLOAD_URL}"
        "${CEF_ARCHIVE_PATH}"
        SHOW_PROGRESS
        STATUS _cef_download_status
        TLS_VERIFY ON
    )
    list(GET _cef_download_status 0 _cef_download_code)
    list(GET _cef_download_status 1 _cef_download_msg)
    if(NOT _cef_download_code EQUAL 0)# 下载失败则报错并停止配置。
        message(FATAL_ERROR "CEF 下载失败，code=${_cef_download_code}, msg=${_cef_download_msg}")
    endif()

    if(EXISTS "${CEF_EXTRACT_DIR}")# 解压前先清理旧目录，避免残留文件干扰。
        file(REMOVE_RECURSE "${CEF_EXTRACT_DIR}")
    endif()
    file(MAKE_DIRECTORY "${CEF_EXTRACT_DIR}")# 创建解压目录，准备解压。

    # CMake内置解压指令 .tar.bz2 压缩包，解压命令、工作目录、结果变量、输出变量、错误变量等。
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E tar xjf "${CEF_ARCHIVE_PATH}"
        WORKING_DIRECTORY "${CEF_EXTRACT_DIR}"
        RESULT_VARIABLE _cef_extract_code
        OUTPUT_VARIABLE _cef_extract_out
        ERROR_VARIABLE _cef_extract_err
    )
    if(NOT _cef_extract_code EQUAL 0)# 解压失败则报错并停止配置，输出错误信息以便排查。
        message(FATAL_ERROR "CEF 解压失败，code=${_cef_extract_code}, err=${_cef_extract_err}")
    endif()

    # 包内通常只有一个根目录（cef_binary_xxx），将其内容复制到 third_party/CEF。
    file(GLOB _cef_extract_children RELATIVE "${CEF_EXTRACT_DIR}" "${CEF_EXTRACT_DIR}/*")
    set(_cef_payload_dir "")
    # 通过循环检查解压目录下的子项，找到第一个目录作为 CEF 根目录。
    foreach(_child IN LISTS _cef_extract_children)
        if(IS_DIRECTORY "${CEF_EXTRACT_DIR}/${_child}")
            set(_cef_payload_dir "${CEF_EXTRACT_DIR}/${_child}")
            break()
        endif()
    endforeach()
    if(_cef_payload_dir STREQUAL "")# 如果没有找到任何目录，则说明解压后的结构不符合预期，报错提示检查压缩包内容。
        message(FATAL_ERROR "未找到解压后的 CEF 根目录，请检查压缩包内容。")
    endif()

    if(EXISTS "${CEF_ROOT}")# 如果目标目录已存在，先删除它以避免旧文件干扰，再创建一个干净的目录准备复制。
        file(REMOVE_RECURSE "${CEF_ROOT}")
    endif()
    file(MAKE_DIRECTORY "${CEF_ROOT}")# 创建目标目录，准备复制解压后的 CEF 文件。
    # 通过 CMake 内置的复制指令，递归复制整个 CEF payload 目录到目标路径，结果变量、错误变量等用于检查复制是否成功。
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${_cef_payload_dir}" "${CEF_ROOT}"
        RESULT_VARIABLE _cef_copy_code
        ERROR_VARIABLE _cef_copy_err
    )
    if(NOT _cef_copy_code EQUAL 0)# 复制失败则报错并停止配置，输出错误信息以便排查。
        message(FATAL_ERROR "CEF 复制到 third_party/CEF 失败，code=${_cef_copy_code}, err=${_cef_copy_err}")
    endif()
endif()

# 再次校验，确保 CEF 真正可用。
if(NOT EXISTS "${CEF_REQUIRED_FILE}")
    message(FATAL_ERROR "CEF 仍不可用，缺少文件: ${CEF_REQUIRED_FILE}")
endif()

# CMAKE_MODULE_PATH：用于 find_package(CEF) 这种 Module 模式搜索 FindXXX.cmake。
list(APPEND CMAKE_MODULE_PATH "${CEF_ROOT}/cmake")

# 加载 CEF 官方 CMake 配置，导出 CEF_LIB_RELEASE/CEF_LIBCEF_DLL_WRAPPER_PATH 等变量。
find_package(CEF REQUIRED)

# 在当前工程里直接构建 libcef_dll_wrapper 静态库，避免依赖 third_party/CEF/out/build 下的预编译产物。
if(NOT TARGET libcef_dll_wrapper)
    # 只在引入 wrapper 子目录时临时关闭 BUILD_SHARED_LIBS，避免它被错误生成为 DLL。
    set(_LRQ_PREV_BUILD_SHARED_LIBS "${BUILD_SHARED_LIBS}")
    set(BUILD_SHARED_LIBS OFF)
    add_subdirectory("${CEF_LIBCEF_DLL_WRAPPER_PATH}" "${CMAKE_BINARY_DIR}/cef_libcef_dll_wrapper")
    set(BUILD_SHARED_LIBS "${_LRQ_PREV_BUILD_SHARED_LIBS}")
    unset(_LRQ_PREV_BUILD_SHARED_LIBS)
endif()


# 函数名：PROJECT_CEF_COPY
# 参数：
# - target_name：要附加 POST_BUILD 拷贝命令的目标名
# 作用：
# - 构建后把 CEF 运行依赖复制到目标输出目录。

function(PROJECT_CEF_COPY target_name)
    # 关键原因：这里使用通配符筛选，而不是固定文件白名单。
    # 通过 GLOB 收集 Release 下所有条目，再过滤掉 .lib/.exe，保留其余运行时文件。
    if(IS_DIRECTORY "${CEF_ROOT}/Release")
        # 注意参数顺序：第一个参数必须是输出变量名，CONFIGURE_DEPENDS 只能放在变量名之后。
        file(GLOB _cef_release_entries CONFIGURE_DEPENDS "${CEF_ROOT}/Release/*")
        foreach(_entry IN LISTS _cef_release_entries)
            if(NOT IS_DIRECTORY "${_entry}" AND NOT _entry MATCHES "\\.(lib|exe)$")
                add_custom_command(
                    TARGET ${target_name}
                    POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${_entry}"
                        "$<TARGET_FILE_DIR:${target_name}>"
                    COMMENT "复制CEF运行时文件: ${_entry}"
                )
            endif()
        endforeach()
    endif()

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
