add_library(nativekit_scene ${NK_LIBRARY_TYPE} src/scene.cpp)
add_library(NativeKit::scene ALIAS nativekit_scene)
target_compile_features(nativekit_scene PRIVATE cxx_std_20)
target_compile_definitions(nativekit_scene PRIVATE NKSCENE_BUILDING_LIBRARY)
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
endif()

install(TARGETS nativekit_scene EXPORT NativeKitTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
