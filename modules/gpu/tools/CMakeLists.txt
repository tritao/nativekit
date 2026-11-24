cmake_minimum_required(VERSION 3.20)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(nativekit_shader_tools LANGUAGES CXX)
    include(CTest)
endif()

add_executable(nkgpu-shaders shader_driver.cpp shader_reflection.cpp)
target_compile_features(nkgpu-shaders PRIVATE cxx_std_17)
set_target_properties(nkgpu-shaders PROPERTIES CXX_EXTENSIONS OFF)
if(MINGW)
    target_link_options(nkgpu-shaders PRIVATE -municode)
endif()
install(TARGETS nkgpu-shaders RUNTIME DESTINATION bin)

if(BUILD_TESTING OR NK_BUILD_TESTS)
    add_test(NAME nkgpu_shader_tool
        COMMAND "${CMAKE_COMMAND}" "-DTOOL=$<TARGET_FILE:nkgpu-shaders>"
            "-DFIXTURES=${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
            "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/fixture-output"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/test_tool.cmake")
    set_tests_properties(nkgpu_shader_tool PROPERTIES LABELS "gpu;shaders")
endif()
