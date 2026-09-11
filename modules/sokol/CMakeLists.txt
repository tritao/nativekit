add_library(nativekit_sokol SHARED src/adapter.cpp)
add_library(NativeKit::sokol ALIAS nativekit_sokol)
target_compile_features(nativekit_sokol PRIVATE cxx_std_17)
target_include_directories(nativekit_sokol
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE "${NK_VENDOR_DIR}/sokol" ${NK_SOKOL_GRAPHICS_INCLUDE_DIRS})
target_compile_definitions(nativekit_sokol PUBLIC ${NK_SOKOL_COMPILE_DEFINITION})
target_link_libraries(nativekit_sokol PRIVATE NativeKit::nativekit nativekit_sokol_runtime
    ${NK_SOKOL_GRAPHICS_LIBRARIES})
set_target_properties(nativekit_sokol PROPERTIES
    EXPORT_NAME sokol
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    VERSION ${PROJECT_VERSION}
    SOVERSION 0)

if(NK_BUILD_TESTS)
    add_library(nativekit_sokol_benchmark SHARED bench/benchmark.c)
    target_include_directories(nativekit_sokol_benchmark PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/bench")
endif()

if(NK_BUILD_SOKOL_BACKEND_MATRIX AND NK_BUILD_TESTS)
    add_executable(nativekit_sokol_runtime_matrix_test tests/runtime_matrix.c)
    target_include_directories(nativekit_sokol_runtime_matrix_test PRIVATE
        "${NK_VENDOR_DIR}/sokol" "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(nativekit_sokol_runtime_matrix_test PRIVATE
        nativekit_sokol_runtime_glcore nativekit_sokol_runtime_gles3)
    add_test(NAME nativekit_sokol_runtime_matrix
        COMMAND nativekit_sokol_runtime_matrix_test)
    set_tests_properties(nativekit_sokol_runtime_matrix PROPERTIES LABELS "graphics;sokol")
endif()

if(NK_BUILD_EXAMPLES)
    add_executable(nativekit_sokol_triangle examples/triangle.c)
    target_include_directories(nativekit_sokol_triangle PRIVATE
        "${NK_VENDOR_DIR}/sokol" "${CMAKE_CURRENT_SOURCE_DIR}/src"
        ${NK_SOKOL_GRAPHICS_INCLUDE_DIRS})
    target_compile_definitions(nativekit_sokol_triangle PRIVATE ${NK_SOKOL_COMPILE_DEFINITION})
    target_link_libraries(nativekit_sokol_triangle PRIVATE NativeKit::nativekit
        nativekit_sokol_runtime ${NK_SOKOL_GRAPHICS_LIBRARIES})
    set_target_properties(nativekit_sokol_triangle PROPERTIES LINKER_LANGUAGE CXX)
endif()

install(TARGETS nativekit_sokol EXPORT NativeKitTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
