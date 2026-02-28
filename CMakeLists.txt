# 3.20 CMake版本
cmake_minimum_required(VERSION 3.10)

# 项目名称
project(LiuRanqi
    VERSION 0.0.1
    LANGUAGES CXX
)

#设置C++标准
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 使用UTF-8编码 + 启用C++异常处理 + 关闭max宏警告
if(MSVC)
    add_compile_options(
        "/utf-8"          # 原有编码配置
        /EHsc             # 启用C++异常处理，解决C4530警告
        /wd4003           # 关闭max宏参数不足的警告
        /DNDEBUG
    )
endif()

# 定义NOMINMAX宏，禁用Windows的max/min宏，解决和std::max的冲突
add_compile_definitions(NOMINMAX)

#宏定义
set(SDL_ARCH "x64")

# 定义 SDL3路径宏
set(SDL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../SDL3")

# 定义 CEF路径宏
set(CEF_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../CEF")

# 规范化路径转为绝对路径
get_filename_component(SDL_DIR "${SDL_DIR}" ABSOLUTE)
get_filename_component(CEF_ROOT "${CEF_ROOT}" ABSOLUTE)

# 设置CEF模块路径
set(CMAKE_MODULE_PATH  ${CMAKE_MODULE_PATH} "${CEF_ROOT}/cmake")

# 加载 CEF 包
find_package(CEF REQUIRED)

# 设置输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_SOURCE_DIR}/生成/Debug")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/生成/Release")

#批量匹配所有 .cpp
file(GLOB ALL_SRC "*.cpp")
# 添加可执行文件
add_executable(${PROJECT_NAME} ${ALL_SRC})

# 设置可执行文件名称
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "浏览器")

# 链接静态库目录
target_link_directories(${PROJECT_NAME} PRIVATE
    "${SDL_DIR}/lib/${SDL_ARCH}"
    "${CEF_ROOT}/Release"
    "${CEF_ROOT}/out/build/x64-Release/libcef_dll_wrapper"
)

# 链接静态库
target_link_libraries(${PROJECT_NAME} PRIVATE
    SDL3.lib
    SDL3_test.lib
    opengl32.lib
    libcef.lib
    libcef_dll_wrapper.lib
)

# 包含头文件目录
target_include_directories(${PROJECT_NAME} PRIVATE
    ${SDL_DIR}/include
    ${CEF_ROOT}
)

# 需要拷贝的文件列表
set(COPY_WS
    "${SDL_DIR}/lib/${SDL_ARCH}/SDL3.dll" 
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

# 拷贝文件到输出目录
foreach(W_FILE ${COPY_WS})
    if(EXISTS "${W_FILE}")
        add_custom_command(
            TARGET ${PROJECT_NAME}
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${W_FILE}"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
            COMMENT "📄 复制文件: ${W_FILE}"
        )
        message(STATUS "✅ 已注册文件复制: ${W_FILE}")
    else()
        message(WARNING "❌ 文件不存在，跳过复制：${W_FILE}")
    endif()
endforeach()

# 要拷贝的目录列表                                                                                                                                                            
set(COPY_DS                                                                                                                                                                   
    "${CEF_ROOT}/Resources"                                                                                                                                                   
)                                                                                                                                                                             
                                                                                                                                                                                
# 拷贝 Resources 目录内容到 exe 同级目录                                                                                                                                      
foreach(D_FILE ${COPY_DS})                                                                                                                                                    
    if(IS_DIRECTORY "${D_FILE}")                                                                                                                                              
        add_custom_command(                                                                                                                                                   
            TARGET ${PROJECT_NAME}                                                                                                                                            
            POST_BUILD                                                                                                                                                        
            COMMAND ${CMAKE_COMMAND} -E copy_directory                                                                                                                        
                "${D_FILE}"                                                                                                                                                   
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>"                                                                                                                          
            COMMENT "复制目录内容: ${D_FILE} -> $<TARGET_FILE_DIR:${PROJECT_NAME}>"                                                                                           
        )                                                                                                                                                                     
        message(STATUS "已注册目录复制: ${D_FILE} -> $<TARGET_FILE_DIR:${PROJECT_NAME}>")                                                                                     
    else()                                                                                                                                                                    
        message(WARNING "目录不存在，跳过复制: ${D_FILE}")                                                                                                                    
    endif()                                                                                                                                                                   
endforeach()            