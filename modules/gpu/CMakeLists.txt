add_library(nativekit_gpu ${NK_LIBRARY_TYPE} src/adapter.cpp)
add_library(NativeKit::gpu ALIAS nativekit_gpu)
target_compile_features(nativekit_gpu PRIVATE cxx_std_20)
target_compile_definitions(nativekit_gpu PRIVATE NKGPU_BUILDING_LIBRARY)
target_include_directories(nativekit_gpu
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE "${PROJECT_SOURCE_DIR}/src" "${NK_VENDOR_DIR}/sokol"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/sokol"
        ${NK_SOKOL_GRAPHICS_INCLUDE_DIRS})
target_compile_definitions(nativekit_gpu PRIVATE ${NK_SOKOL_COMPILE_DEFINITION})
target_link_libraries(nativekit_gpu PUBLIC NativeKit::nativekit)
if(NK_BUILD_GPU_BACKEND_MATRIX)
    function(nk_add_gpu_sokol_backend_objects target backend symbol_prefix graphics_include_dirs)
        add_library(${target} OBJECT src/sokol/runtime.c src/sokol/api.c)
        target_include_directories(${target} PRIVATE
            "${PROJECT_SOURCE_DIR}/include" "${NK_VENDOR_DIR}/sokol"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/sokol" ${graphics_include_dirs})
        target_compile_definitions(${target} PRIVATE ${backend}
            SOKOL_GFX_SYMBOL_PREFIX=${symbol_prefix}
            NK_SOKOL_API_PREFIX=${symbol_prefix}
            NK_SOKOL_RUNTIME_PREFIX=${symbol_prefix}runtime_)
    set_target_properties(${target} PROPERTIES
            POSITION_INDEPENDENT_CODE ON
            C_VISIBILITY_PRESET hidden)
        # OpenGL contexts share textures and programs, but not vertex-array
        # state. Keep an independent Sokol state cache for each surface
        # context while retaining the existing backend API contract.
        set(secondary_prefix "${symbol_prefix}secondary_")
        add_library(${target}_secondary OBJECT src/sokol/runtime.c src/sokol/api.c)
        target_include_directories(${target}_secondary PRIVATE
            "${PROJECT_SOURCE_DIR}/include" "${NK_VENDOR_DIR}/sokol"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/sokol" ${graphics_include_dirs})
        target_compile_definitions(${target}_secondary PRIVATE ${backend}
            SOKOL_GFX_SYMBOL_PREFIX=${secondary_prefix}
            NK_SOKOL_API_PREFIX=${secondary_prefix}
            NK_SOKOL_RUNTIME_PREFIX=${secondary_prefix}runtime_)
        set_target_properties(${target}_secondary PROPERTIES
            POSITION_INDEPENDENT_CODE ON
            C_VISIBILITY_PRESET hidden)
    endfunction()

    nk_add_gpu_sokol_backend_objects(nativekit_gpu_sokol_glcore_objects
        NK_SOKOL_BACKEND_GLCORE=1 nk_sokol_glcore_ "")
    nk_add_gpu_sokol_backend_objects(nativekit_gpu_sokol_gles3_objects
        NK_SOKOL_BACKEND_GLES3=1 nk_sokol_gles3_ "${NK_SOKOL_MATRIX_GLES3_INCLUDE_DIR}")
    target_sources(nativekit_gpu PRIVATE
        $<TARGET_OBJECTS:nativekit_gpu_sokol_glcore_objects>
        $<TARGET_OBJECTS:nativekit_gpu_sokol_glcore_objects_secondary>
        $<TARGET_OBJECTS:nativekit_gpu_sokol_gles3_objects>
        $<TARGET_OBJECTS:nativekit_gpu_sokol_gles3_objects_secondary>)
    target_compile_definitions(nativekit_gpu PRIVATE NK_SOKOL_RUNTIME_MATRIX=1)
    target_compile_definitions(nativekit_gpu PRIVATE NK_SOKOL_MULTI_CONTEXT=1)
    target_link_libraries(nativekit_gpu PRIVATE OpenGL::GL
        "${NK_SOKOL_MATRIX_GLES3_LIBRARY}")
else()
    if(NK_SOKOL_BACKEND STREQUAL "metal")
        target_sources(nativekit_gpu PRIVATE src/sokol/runtime_metal.mm src/sokol/api.c)
        set_target_properties(nativekit_gpu PROPERTIES
            OBJCXX_STANDARD 17
            OBJCXX_STANDARD_REQUIRED YES)
        target_compile_options(nativekit_gpu PRIVATE
            $<$<COMPILE_LANGUAGE:OBJCXX>:-fblocks>)
    else()
        target_sources(nativekit_gpu PRIVATE src/sokol/runtime.c src/sokol/api.c)
        # OpenGL contexts share textures and programs, but not vertex-array
        # state. Keep an independent Sokol state cache for each surface
        # context while retaining the existing backend API contract.
        if(NK_SOKOL_BACKEND STREQUAL "glcore" OR NK_SOKOL_BACKEND STREQUAL "gles3")
            add_library(nativekit_gpu_sokol_secondary_objects OBJECT
                src/sokol/runtime.c src/sokol/api.c)
            target_include_directories(nativekit_gpu_sokol_secondary_objects PRIVATE
                "${PROJECT_SOURCE_DIR}/include" "${NK_VENDOR_DIR}/sokol"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/sokol" ${NK_SOKOL_GRAPHICS_INCLUDE_DIRS})
            target_compile_definitions(nativekit_gpu_sokol_secondary_objects PRIVATE
                ${NK_SOKOL_COMPILE_DEFINITION}
                SOKOL_GFX_SYMBOL_PREFIX=nk_sokol_secondary_
                NK_SOKOL_API_PREFIX=nk_sokol_secondary_
                NK_SOKOL_RUNTIME_PREFIX=nk_sokol_secondary_runtime_)
            set_target_properties(nativekit_gpu_sokol_secondary_objects PROPERTIES
                POSITION_INDEPENDENT_CODE ON
                C_VISIBILITY_PRESET hidden)
            target_sources(nativekit_gpu PRIVATE
                $<TARGET_OBJECTS:nativekit_gpu_sokol_secondary_objects>)
            target_compile_definitions(nativekit_gpu PRIVATE NK_SOKOL_MULTI_CONTEXT=1)
        endif()
    endif()
    target_link_libraries(nativekit_gpu PRIVATE
        ${NK_SOKOL_GRAPHICS_LIBRARIES})
endif()
set_target_properties(nativekit_gpu PROPERTIES
    EXPORT_NAME gpu
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    VERSION ${PROJECT_VERSION}
    SOVERSION 0)

if(NK_BUILD_TESTS)
    target_compile_definitions(nativekit_gpu PRIVATE NKGPU_TESTING=1)
    add_library(nativekit_gpu_benchmark SHARED bench/benchmark.c)
    target_include_directories(nativekit_gpu_benchmark PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/bench")

    if(UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN AND NK_BACKEND_GTK_AVAILABLE)
        find_program(NKGPU_XVFB_RUN xvfb-run)
        if(NKGPU_XVFB_RUN)
            add_executable(nativekit_gpu_contract_smoke_test tests/contract_smoke.cpp)
            target_compile_features(nativekit_gpu_contract_smoke_test PRIVATE cxx_std_17)
            target_compile_definitions(nativekit_gpu_contract_smoke_test PRIVATE NKGPU_TESTING=1)
            if(NK_BUILD_GPU_BACKEND_MATRIX)
                target_compile_definitions(nativekit_gpu_contract_smoke_test PRIVATE
                    NK_GPU_TEST_BACKEND_MATRIX=1)
            endif()
            target_include_directories(nativekit_gpu_contract_smoke_test PRIVATE src)
            target_link_libraries(nativekit_gpu_contract_smoke_test PRIVATE NativeKit::gpu
                NativeKit::nativekit)
            add_test(NAME nativekit_gpu_contract_smoke
                COMMAND ${NKGPU_XVFB_RUN} -a $<TARGET_FILE:nativekit_gpu_contract_smoke_test>)
            set_tests_properties(nativekit_gpu_contract_smoke PROPERTIES
                LABELS "graphics;gpu"
                TIMEOUT 15
                ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1")
            add_executable(nativekit_gpu_batch_submit_test tests/batch_submit.cpp)
            target_compile_features(nativekit_gpu_batch_submit_test PRIVATE cxx_std_17)
            target_compile_definitions(nativekit_gpu_batch_submit_test PRIVATE NKGPU_TESTING=1)
            target_include_directories(nativekit_gpu_batch_submit_test PRIVATE src)
            target_link_libraries(nativekit_gpu_batch_submit_test PRIVATE NativeKit::gpu
                NativeKit::nativekit)
            if(NOT EMSCRIPTEN AND NOT ANDROID)
                find_package(OpenGL QUIET)
                if(TARGET OpenGL::GL)
                    target_link_libraries(nativekit_gpu_batch_submit_test PRIVATE OpenGL::GL)
                endif()
            endif()
            add_test(NAME nativekit_gpu_batch_submit
                COMMAND ${NKGPU_XVFB_RUN} -a $<TARGET_FILE:nativekit_gpu_batch_submit_test>)
            set_tests_properties(nativekit_gpu_batch_submit PROPERTIES
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
        "${CMAKE_CURRENT_SOURCE_DIR}/src/sokol")
    target_sources(nativekit_sokol_runtime_matrix_test PRIVATE
        $<TARGET_OBJECTS:nativekit_gpu_sokol_glcore_objects>
        $<TARGET_OBJECTS:nativekit_gpu_sokol_gles3_objects>)
    target_link_libraries(nativekit_sokol_runtime_matrix_test PRIVATE OpenGL::GL
        "${NK_SOKOL_MATRIX_GLES3_LIBRARY}")
    add_test(NAME nativekit_sokol_runtime_matrix
        COMMAND nativekit_sokol_runtime_matrix_test)
    set_tests_properties(nativekit_sokol_runtime_matrix PROPERTIES LABELS "graphics;sokol")
endif()

if(NK_BUILD_EXAMPLES)
    add_executable(nativekit_gpu_triangle examples/triangle.c)
    target_link_libraries(nativekit_gpu_triangle PRIVATE NativeKit::gpu)
    set_target_properties(nativekit_gpu_triangle PROPERTIES LINKER_LANGUAGE CXX)
endif()

install(TARGETS nativekit_gpu EXPORT NativeKitTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
