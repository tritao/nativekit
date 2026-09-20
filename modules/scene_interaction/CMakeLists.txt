add_library(nativekit_scene_interaction ${NK_LIBRARY_TYPE} src/interaction.cpp)
add_library(NativeKit::scene_interaction ALIAS nativekit_scene_interaction)
target_compile_features(nativekit_scene_interaction PRIVATE cxx_std_20)
target_compile_definitions(nativekit_scene_interaction PRIVATE NKSINTERACTION_BUILDING_LIBRARY)
target_include_directories(nativekit_scene_interaction
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/modules/scene/include>
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/modules/scene_render/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE src)
target_link_libraries(nativekit_scene_interaction PUBLIC NativeKit::scene_render)
set_target_properties(nativekit_scene_interaction PROPERTIES
    EXPORT_NAME scene_interaction
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    VERSION ${PROJECT_VERSION}
    SOVERSION 0)

if(NK_BUILD_TESTS)
    add_executable(nativekit_scene_interaction_tests tests/interaction.cpp)
    target_compile_features(nativekit_scene_interaction_tests PRIVATE cxx_std_20)
    target_include_directories(nativekit_scene_interaction_tests PRIVATE
        "${PROJECT_SOURCE_DIR}/modules/scene/src")
    target_link_libraries(nativekit_scene_interaction_tests PRIVATE NativeKit::scene_interaction)
    add_test(NAME scene_interaction COMMAND nativekit_scene_interaction_tests)
    set_tests_properties(scene_interaction PROPERTIES LABELS "scene;interaction" TIMEOUT 30)

    if(UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN AND NK_BACKEND_GTK_AVAILABLE)
        find_program(NKSINTERACTION_XVFB_RUN xvfb-run)
        if(NKSINTERACTION_XVFB_RUN)
            add_executable(nativekit_scene_interaction_gpu_tests tests/gpu.cpp)
            target_compile_features(nativekit_scene_interaction_gpu_tests PRIVATE cxx_std_20)
            target_include_directories(nativekit_scene_interaction_gpu_tests PRIVATE
                "${PROJECT_SOURCE_DIR}/modules/scene/src")
            target_link_libraries(nativekit_scene_interaction_gpu_tests PRIVATE
                NativeKit::scene_interaction NativeKit::scene_render NativeKit::scene
                NativeKit::gpu NativeKit::nativekit)
            add_test(NAME scene_interaction_gpu
                COMMAND ${NKSINTERACTION_XVFB_RUN} -a
                    $<TARGET_FILE:nativekit_scene_interaction_gpu_tests>)
            set_tests_properties(scene_interaction_gpu PROPERTIES
                LABELS "scene;interaction;graphics;gpu"
                TIMEOUT 30
                ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1;NO_AT_BRIDGE=1")
        endif()
    endif()
endif()

install(TARGETS nativekit_scene_interaction EXPORT NativeKitTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
