add_executable(nativekit_hello hello.c)
target_link_libraries(nativekit_hello PRIVATE NativeKit::nativekit)
set_target_properties(nativekit_hello PROPERTIES LINKER_LANGUAGE CXX)

add_executable(nativekit_showcase showcase.c)
target_link_libraries(nativekit_showcase PRIVATE NativeKit::nativekit)
set_target_properties(nativekit_showcase PROPERTIES LINKER_LANGUAGE CXX)

add_executable(nativekit_opengl opengl.c)
target_link_libraries(nativekit_opengl PRIVATE NativeKit::nativekit)
set_target_properties(nativekit_opengl PROPERTIES LINKER_LANGUAGE CXX)

find_package(Vulkan QUIET)
find_program(NATIVEKIT_GLSLC glslc)
if(Vulkan_FOUND AND NATIVEKIT_GLSLC)
    set(NATIVEKIT_SHADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
    set(NATIVEKIT_VERTEX_SPV "${NATIVEKIT_SHADER_DIR}/triangle.vert.spv")
    set(NATIVEKIT_FRAGMENT_SPV "${NATIVEKIT_SHADER_DIR}/triangle.frag.spv")
    add_custom_command(
        OUTPUT "${NATIVEKIT_VERTEX_SPV}" "${NATIVEKIT_FRAGMENT_SPV}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${NATIVEKIT_SHADER_DIR}"
        COMMAND "${NATIVEKIT_GLSLC}" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/triangle.vert"
                -o "${NATIVEKIT_VERTEX_SPV}"
        COMMAND "${NATIVEKIT_GLSLC}" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/triangle.frag"
                -o "${NATIVEKIT_FRAGMENT_SPV}"
        DEPENDS shaders/triangle.vert shaders/triangle.frag
        VERBATIM)
    add_executable(nativekit_vulkan vulkan.c
        "${NATIVEKIT_VERTEX_SPV}" "${NATIVEKIT_FRAGMENT_SPV}")
    target_compile_definitions(nativekit_vulkan PRIVATE
        NATIVEKIT_VERTEX_SPV="${NATIVEKIT_VERTEX_SPV}"
        NATIVEKIT_FRAGMENT_SPV="${NATIVEKIT_FRAGMENT_SPV}")
    target_link_libraries(nativekit_vulkan PRIVATE NativeKit::nativekit Vulkan::Vulkan)
    set_target_properties(nativekit_vulkan PROPERTIES LINKER_LANGUAGE CXX)
endif()

add_executable(nativekit_browser browser.c)
target_link_libraries(nativekit_browser PRIVATE NativeKit::nativekit)
set_target_properties(nativekit_browser PROPERTIES LINKER_LANGUAGE CXX)
