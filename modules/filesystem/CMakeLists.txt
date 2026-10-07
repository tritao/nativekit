add_library(nativekit_filesystem ${NK_LIBRARY_TYPE})
add_library(NativeKit::filesystem ALIAS nativekit_filesystem)
target_compile_features(nativekit_filesystem PRIVATE cxx_std_17)
target_compile_definitions(nativekit_filesystem PRIVATE NKFS_BUILDING_LIBRARY)
if(NK_ENABLE_NO_EXCEPTIONS)
    target_compile_definitions(nativekit_filesystem PRIVATE NK_ENABLE_NO_EXCEPTIONS=1)
    if(MSVC)
        target_compile_options(nativekit_filesystem PRIVATE /EHs-c-)
    else()
        target_compile_options(nativekit_filesystem PRIVATE -fno-exceptions)
    endif()
endif()
target_include_directories(nativekit_filesystem
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(nativekit_filesystem PRIVATE src/linux.cpp)
else()
    target_sources(nativekit_filesystem PRIVATE src/unsupported.cpp)
endif()

if(NOT NK_BUILD_SHARED OR EMSCRIPTEN)
    target_compile_definitions(nativekit_filesystem PUBLIC NKFS_STATIC)
endif()

set_target_properties(nativekit_filesystem PROPERTIES
    EXPORT_NAME filesystem
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    VERSION ${PROJECT_VERSION}
    SOVERSION 0)

install(TARGETS nativekit_filesystem EXPORT NativeKitTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if(NK_BUILD_TESTS)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_executable(nativekit_filesystem_tests tests/filesystem.cpp)
    else()
        add_executable(nativekit_filesystem_tests tests/unsupported.cpp)
    endif()
    target_compile_features(nativekit_filesystem_tests PRIVATE cxx_std_17)
    target_link_libraries(nativekit_filesystem_tests PRIVATE NativeKit::filesystem)
    add_test(NAME filesystem COMMAND nativekit_filesystem_tests)
    set_tests_properties(filesystem PROPERTIES TIMEOUT 30 LABELS "filesystem")
endif()
