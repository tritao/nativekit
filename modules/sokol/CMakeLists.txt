include(FetchContent)

if(NOT UNIX OR APPLE OR ANDROID)
    message(FATAL_ERROR "NativeKit Sokol currently supports desktop Linux/OpenGL only")
endif()

FetchContent_Declare(sokol
    GIT_REPOSITORY https://github.com/floooh/sokol.git
    GIT_TAG 5e7dd21eacac60aedc7ea6d80218280d442ce50f
    GIT_SHALLOW FALSE)
FetchContent_MakeAvailable(sokol)

find_package(OpenGL REQUIRED)

add_library(nativekit_sokol SHARED src/adapter.cpp)
add_library(NativeKit::sokol ALIAS nativekit_sokol)
target_compile_features(nativekit_sokol PRIVATE cxx_std_17)
target_include_directories(nativekit_sokol
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE "${sokol_SOURCE_DIR}")
target_link_libraries(nativekit_sokol PRIVATE NativeKit::nativekit OpenGL::GL)
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

if(NK_BUILD_EXAMPLES)
    add_executable(nativekit_sokol_triangle examples/triangle.c)
    target_include_directories(nativekit_sokol_triangle PRIVATE "${sokol_SOURCE_DIR}")
    target_link_libraries(nativekit_sokol_triangle PRIVATE NativeKit::nativekit OpenGL::GL)
    set_target_properties(nativekit_sokol_triangle PROPERTIES LINKER_LANGUAGE CXX)
endif()

install(TARGETS nativekit_sokol EXPORT NativeKitTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
