list(APPEND CMAKE_PREFIX_PATH "${SDL3_ROOT}/cmake")
find_package(SDL3 CONFIG REQUIRED)

file(GLOB BROWSER_SRC CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

add_executable(SDL ${BROWSER_SRC})
set_target_properties(SDL PROPERTIES OUTPUT_NAME "浏览器")

target_link_libraries(SDL PRIVATE
    SDL3::SDL3
    opengl32
)

if(MSVC)
    set_property(TARGET SDL PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
    target_compile_definitions(SDL PRIVATE NDEBUG _ITERATOR_DEBUG_LEVEL=0)
    target_compile_options(SDL PRIVATE /U_DEBUG)
endif()

set(SDL3_DLL "${SDL3_ROOT}/lib/x64/SDL3.dll")
if(EXISTS "${SDL3_DLL}")
    add_custom_command(
        TARGET SDL
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SDL3_DLL}"
            "$<TARGET_FILE_DIR:SDL>"
        COMMENT "Copy SDL runtime: ${SDL3_DLL}"
    )
endif()
