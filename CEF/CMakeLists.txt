list(APPEND CMAKE_MODULE_PATH "${CEF_ROOT}/cmake")
find_package(CEF REQUIRED)

file(GLOB CEF_SRC CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

add_executable(CEF ${CEF_SRC})
set_target_properties(CEF PROPERTIES OUTPUT_NAME "CEF")

target_include_directories(CEF PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CEF_ROOT}"
)
target_link_libraries(CEF PRIVATE
    "${CEF_ROOT}/Release/libcef.lib"
    "${CEF_ROOT}/out/build/x64-Release/libcef_dll_wrapper/libcef_dll_wrapper.lib"
)

if(MSVC)
    # 对齐 CEF 预编译库(/MT + 非调试迭代器)。
    set_property(TARGET CEF PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
    target_compile_definitions(CEF PRIVATE NDEBUG _ITERATOR_DEBUG_LEVEL=0)
    target_compile_options(CEF PRIVATE /U_DEBUG)
endif()

set(CEF_RUNTIME_FILES
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

foreach(cef_file IN LISTS CEF_RUNTIME_FILES)
    if(EXISTS "${cef_file}")
        add_custom_command(
            TARGET CEF
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${cef_file}"
                "$<TARGET_FILE_DIR:CEF>"
            COMMENT "Copy CEF runtime: ${cef_file}"
        )
    endif()
endforeach()

if(IS_DIRECTORY "${CEF_ROOT}/Resources")
    add_custom_command(
        TARGET CEF
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CEF_ROOT}/Resources"
            "$<TARGET_FILE_DIR:CEF>"
        COMMENT "Copy CEF resources"
    )
endif()
