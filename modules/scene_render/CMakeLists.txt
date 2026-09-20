add_library(nativekit_scene_render ${NK_LIBRARY_TYPE}
    src/gpu_executor.cpp
    src/instance_batches.cpp
    src/picking.cpp
    src/render_compiler.cpp
    src/render_items.cpp
    src/render_plan.cpp
    src/resource_cache.cpp)
add_library(NativeKit::scene_render ALIAS nativekit_scene_render)
target_compile_features(nativekit_scene_render PRIVATE cxx_std_20)
target_compile_definitions(nativekit_scene_render PRIVATE NKSRENDER_BUILDING_LIBRARY)
target_include_directories(nativekit_scene_render
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/modules/scene/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE src)
target_link_libraries(nativekit_scene_render PUBLIC NativeKit::scene NativeKit::gpu)
set_target_properties(nativekit_scene_render PROPERTIES
    EXPORT_NAME scene_render
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    VERSION ${PROJECT_VERSION}
    SOVERSION 0)

if(NK_BUILD_TESTS)
    add_executable(nativekit_scene_render_benchmark bench/benchmark.cpp)
    target_compile_features(nativekit_scene_render_benchmark PRIVATE cxx_std_20)
    target_include_directories(nativekit_scene_render_benchmark PRIVATE
        src "${PROJECT_SOURCE_DIR}/modules/scene/src")
    target_link_libraries(nativekit_scene_render_benchmark PRIVATE NativeKit::scene_render
        NativeKit::scene)
    add_test(NAME scene_render_benchmark COMMAND nativekit_scene_render_benchmark)
    set_tests_properties(scene_render_benchmark PROPERTIES LABELS "scene;render;benchmark"
        TIMEOUT 30)

    add_executable(nativekit_scene_render_tests tests/compiler.cpp)
    target_compile_features(nativekit_scene_render_tests PRIVATE cxx_std_20)
    target_include_directories(nativekit_scene_render_tests PRIVATE
        src "${PROJECT_SOURCE_DIR}/modules/scene/src")
    target_link_libraries(nativekit_scene_render_tests PRIVATE NativeKit::scene_render
        NativeKit::scene)
    add_test(NAME scene_render_compiler COMMAND nativekit_scene_render_tests)
    set_tests_properties(scene_render_compiler PROPERTIES LABELS "scene;render" TIMEOUT 30)

    if(UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN AND NK_BACKEND_GTK_AVAILABLE)
        find_program(NKSRENDER_XVFB_RUN xvfb-run)
        if(NKSRENDER_XVFB_RUN)
            add_executable(nativekit_scene_render_gpu_tests tests/gpu.cpp)
            target_compile_features(nativekit_scene_render_gpu_tests PRIVATE cxx_std_20)
            target_include_directories(nativekit_scene_render_gpu_tests PRIVATE
                src "${PROJECT_SOURCE_DIR}/modules/scene/src")
            target_link_libraries(nativekit_scene_render_gpu_tests PRIVATE NativeKit::scene_render
                NativeKit::scene NativeKit::gpu NativeKit::nativekit)
            add_test(NAME scene_render_gpu
                COMMAND ${NKSRENDER_XVFB_RUN} -a $<TARGET_FILE:nativekit_scene_render_gpu_tests>)
            set_tests_properties(scene_render_gpu PROPERTIES
                LABELS "scene;render;graphics;gpu"
                TIMEOUT 30
                ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1")

            add_executable(nativekit_scene_render_gpu_benchmark tests/gpu_benchmark.cpp)
            target_compile_features(nativekit_scene_render_gpu_benchmark PRIVATE cxx_std_20)
            target_include_directories(nativekit_scene_render_gpu_benchmark PRIVATE
                src "${PROJECT_SOURCE_DIR}/modules/scene/src")
            target_link_libraries(nativekit_scene_render_gpu_benchmark PRIVATE NativeKit::scene_render
                NativeKit::scene NativeKit::gpu NativeKit::nativekit)
            add_test(NAME scene_render_gpu_benchmark
                COMMAND ${NKSRENDER_XVFB_RUN} -a $<TARGET_FILE:nativekit_scene_render_gpu_benchmark>)
            set_tests_properties(scene_render_gpu_benchmark PROPERTIES
                LABELS "scene;render;graphics;gpu;benchmark"
                TIMEOUT 60
                ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1")
        endif()
    endif()
endif()

install(TARGETS nativekit_scene_render EXPORT NativeKitTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
