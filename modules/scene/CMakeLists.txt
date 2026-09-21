add_library(nativekit_scene ${NK_LIBRARY_TYPE} src/scene.cpp)
add_library(NativeKit::scene ALIAS nativekit_scene)
target_compile_features(nativekit_scene PRIVATE cxx_std_20)
target_compile_definitions(nativekit_scene PRIVATE NKSCENE_BUILDING_LIBRARY)
if(NK_SCENE_SANITIZER)
    target_compile_options(nativekit_scene PRIVATE ${NK_SCENE_SANITIZER_COMPILE_OPTIONS})
    target_link_options(nativekit_scene PRIVATE ${NK_SCENE_SANITIZER_LINK_OPTIONS})
endif()
target_include_directories(nativekit_scene
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE src)
set_target_properties(nativekit_scene PROPERTIES
    EXPORT_NAME scene
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    VERSION ${PROJECT_VERSION}
    SOVERSION 0)

if(NK_BUILD_TESTS)
    add_executable(nativekit_scene_foundation_tests tests/foundation.cpp)
    target_compile_features(nativekit_scene_foundation_tests PRIVATE cxx_std_20)
    target_link_libraries(nativekit_scene_foundation_tests PRIVATE NativeKit::scene)
    add_test(NAME scene_foundation COMMAND nativekit_scene_foundation_tests)
    set_tests_properties(scene_foundation PROPERTIES LABELS "scene" TIMEOUT 30)

    add_executable(nativekit_scene_transaction_tests tests/transactional.cpp)
    target_compile_features(nativekit_scene_transaction_tests PRIVATE cxx_std_20)
    target_include_directories(nativekit_scene_transaction_tests PRIVATE src)
    target_link_libraries(nativekit_scene_transaction_tests PRIVATE NativeKit::scene)
    add_test(NAME scene_transactional COMMAND nativekit_scene_transaction_tests)
    set_tests_properties(scene_transactional PROPERTIES LABELS "scene" TIMEOUT 30)

    add_executable(nativekit_scene_versioned_state_tests tests/versioned_state.cpp)
    target_compile_features(nativekit_scene_versioned_state_tests PRIVATE cxx_std_20)
    target_include_directories(nativekit_scene_versioned_state_tests PRIVATE src)
    target_link_libraries(nativekit_scene_versioned_state_tests PRIVATE NativeKit::scene)
    add_test(NAME scene_versioned_state COMMAND nativekit_scene_versioned_state_tests)
    set_tests_properties(scene_versioned_state PROPERTIES LABELS "scene" TIMEOUT 30)

    add_executable(nativekit_scene_concurrency_tests tests/concurrency.cpp)
    target_compile_features(nativekit_scene_concurrency_tests PRIVATE cxx_std_20)
    target_include_directories(nativekit_scene_concurrency_tests PRIVATE src)
    target_link_libraries(nativekit_scene_concurrency_tests PRIVATE NativeKit::scene)
    add_test(NAME scene_concurrency COMMAND nativekit_scene_concurrency_tests)
    set_tests_properties(scene_concurrency PROPERTIES LABELS "scene;concurrency" TIMEOUT 30)
    if(NK_SCENE_SANITIZER STREQUAL "thread")
        set_tests_properties(scene_concurrency PROPERTIES
            ENVIRONMENT "TSAN_OPTIONS=protect_shadow_gap=0")
    endif()

    add_executable(nativekit_scene_c_api_tests tests/c_api.c)
    set_target_properties(nativekit_scene_c_api_tests PROPERTIES LINKER_LANGUAGE CXX)
    target_include_directories(nativekit_scene_c_api_tests PRIVATE include)
    target_link_libraries(nativekit_scene_c_api_tests PRIVATE NativeKit::scene)
    add_test(NAME scene_c_api COMMAND nativekit_scene_c_api_tests)
    set_tests_properties(scene_c_api PROPERTIES LABELS "scene;abi" TIMEOUT 30)

    add_executable(nativekit_scene_benchmark bench/benchmark.cpp)
    target_compile_features(nativekit_scene_benchmark PRIVATE cxx_std_20)
    target_include_directories(nativekit_scene_benchmark PRIVATE src)
    target_link_libraries(nativekit_scene_benchmark PRIVATE NativeKit::scene)
    add_test(NAME scene_benchmark COMMAND nativekit_scene_benchmark)
    set_tests_properties(scene_benchmark PROPERTIES LABELS "scene;benchmark" TIMEOUT 30)

    add_executable(nativekit_scene_snapshot_benchmark bench/snapshot_benchmark.cpp)
    target_compile_features(nativekit_scene_snapshot_benchmark PRIVATE cxx_std_20)
    target_include_directories(nativekit_scene_snapshot_benchmark PRIVATE src)
    target_link_libraries(nativekit_scene_snapshot_benchmark PRIVATE NativeKit::scene)
    add_test(NAME scene_snapshot_benchmark COMMAND nativekit_scene_snapshot_benchmark)
    set_tests_properties(scene_snapshot_benchmark PROPERTIES LABELS "scene;benchmark" TIMEOUT 30)

    add_executable(nativekit_scene_scale_benchmark bench/scale_benchmark.cpp)
    target_compile_features(nativekit_scene_scale_benchmark PRIVATE cxx_std_20)
    target_include_directories(nativekit_scene_scale_benchmark PRIVATE src)
    target_link_libraries(nativekit_scene_scale_benchmark PRIVATE NativeKit::scene)
    add_test(NAME scene_scale_benchmark COMMAND nativekit_scene_scale_benchmark)
    set_tests_properties(scene_scale_benchmark PROPERTIES LABELS "scene;benchmark;scale"
                                                          TIMEOUT 120)

    if(NK_SCENE_SANITIZER)
        foreach(scene_target
                nativekit_scene_foundation_tests
                nativekit_scene_transaction_tests
                nativekit_scene_versioned_state_tests
                nativekit_scene_concurrency_tests
                nativekit_scene_c_api_tests
                nativekit_scene_benchmark
                nativekit_scene_snapshot_benchmark
                nativekit_scene_scale_benchmark)
            target_compile_options(${scene_target} PRIVATE ${NK_SCENE_SANITIZER_COMPILE_OPTIONS})
            target_link_options(${scene_target} PRIVATE ${NK_SCENE_SANITIZER_LINK_OPTIONS})
            if(NK_SCENE_SANITIZER_EXECUTABLE_COMPILE_OPTIONS)
                target_compile_options(${scene_target} PRIVATE
                    ${NK_SCENE_SANITIZER_EXECUTABLE_COMPILE_OPTIONS})
                target_link_options(${scene_target} PRIVATE
                    ${NK_SCENE_SANITIZER_EXECUTABLE_LINK_OPTIONS})
            endif()
        endforeach()
    endif()
endif()

install(TARGETS nativekit_scene EXPORT NativeKitTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
