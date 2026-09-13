add_library(nativekit_gpu ${NK_LIBRARY_TYPE} src/adapter.cpp)
add_library(NativeKit::gpu ALIAS nativekit_gpu)
target_compile_features(nativekit_gpu PRIVATE cxx_std_17)
target_include_directories(nativekit_gpu
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE "${PROJECT_SOURCE_DIR}/src" "${NK_VENDOR_DIR}/sokol"
        "${PROJECT_SOURCE_DIR}/src/graphics/sokol"
        ${NK_SOKOL_GRAPHICS_INCLUDE_DIRS})
target_compile_definitions(nativekit_gpu PRIVATE ${NK_SOKOL_COMPILE_DEFINITION})
target_link_libraries(nativekit_gpu PRIVATE NativeKit::nativekit)
if(NK_BUILD_GPU_BACKEND_MATRIX)
    target_compile_definitions(nativekit_gpu PRIVATE NK_SOKOL_RUNTIME_MATRIX=1)
    target_link_libraries(nativekit_gpu PRIVATE
        nativekit_sokol_runtime_glcore nativekit_sokol_runtime_gles3)
else()
    target_link_libraries(nativekit_gpu PRIVATE nativekit_sokol_runtime
        ${NK_SOKOL_GRAPHICS_LIBRARIES})
endif()
set_target_properties(nativekit_gpu PROPERTIES
    EXPORT_NAME gpu
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    VERSION ${PROJECT_VERSION}
    SOVERSION 0)

if(NK_BUILD_TESTS)
    add_library(nativekit_gpu_benchmark SHARED bench/benchmark.c)
    target_include_directories(nativekit_gpu_benchmark PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/bench")

    if(UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN AND NK_BACKEND_GTK_AVAILABLE)
        find_program(NKGPU_XVFB_RUN xvfb-run)
        if(NKGPU_XVFB_RUN)
            add_executable(nativekit_gpu_contract_smoke_test tests/contract_smoke.cpp)
            target_compile_features(nativekit_gpu_contract_smoke_test PRIVATE cxx_std_17)
            target_link_libraries(nativekit_gpu_contract_smoke_test PRIVATE NativeKit::gpu
                NativeKit::nativekit)
            add_test(NAME nativekit_gpu_contract_smoke
                COMMAND ${NKGPU_XVFB_RUN} -a $<TARGET_FILE:nativekit_gpu_contract_smoke_test>)
            set_tests_properties(nativekit_gpu_contract_smoke PROPERTIES
                LABELS "graphics;gpu"
                TIMEOUT 15
                ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1")
        endif()
    endif()
endif()

if(NK_BUILD_GPU_BACKEND_MATRIX AND NK_BUILD_TESTS)
    add_executable(nativekit_sokol_runtime_matrix_test tests/runtime_matrix.c)
    target_include_directories(nativekit_sokol_runtime_matrix_test PRIVATE
        "${PROJECT_SOURCE_DIR}/include" "${NK_VENDOR_DIR}/sokol"
        "${PROJECT_SOURCE_DIR}/src/graphics/sokol")
    target_link_libraries(nativekit_sokol_runtime_matrix_test PRIVATE
        nativekit_sokol_runtime_glcore nativekit_sokol_runtime_gles3)
    add_test(NAME nativekit_sokol_runtime_matrix
        COMMAND nativekit_sokol_runtime_matrix_test)
    set_tests_properties(nativekit_sokol_runtime_matrix PROPERTIES LABELS "graphics;sokol")
endif()

if(NK_BUILD_EXAMPLES)
    add_executable(nativekit_gpu_triangle examples/triangle.c)
    target_include_directories(nativekit_gpu_triangle PRIVATE
        "${NK_VENDOR_DIR}/sokol" "${PROJECT_SOURCE_DIR}/src/graphics/sokol"
        ${NK_SOKOL_GRAPHICS_INCLUDE_DIRS})
    target_compile_definitions(nativekit_gpu_triangle PRIVATE ${NK_SOKOL_COMPILE_DEFINITION})
    target_link_libraries(nativekit_gpu_triangle PRIVATE NativeKit::nativekit
        nativekit_sokol_runtime ${NK_SOKOL_GRAPHICS_LIBRARIES})
    set_target_properties(nativekit_gpu_triangle PROPERTIES LINKER_LANGUAGE CXX)
endif()

install(TARGETS nativekit_gpu EXPORT NativeKitTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
