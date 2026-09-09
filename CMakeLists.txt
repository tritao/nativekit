cmake_minimum_required(VERSION 3.21)

project(nativekit VERSION 0.1.0 LANGUAGES C CXX)

include(GNUInstallDirs)
include(CTest)
include(CMakePackageConfigHelpers)

option(NK_BUILD_SHARED "Build NativeKit as a shared library" ON)
option(NK_BUILD_TESTS "Build NativeKit tests" ${BUILD_TESTING})
option(NK_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

set(NK_LIBRARY_TYPE STATIC)
if(NK_BUILD_SHARED)
    set(NK_LIBRARY_TYPE SHARED)
endif()

add_library(nativekit ${NK_LIBRARY_TYPE}
    src/core/api.cpp
    src/core/error.cpp
    src/core/event_queue.cpp
    src/core/handle_registry.cpp
)
add_library(NativeKit::nativekit ALIAS nativekit)

target_compile_features(nativekit PRIVATE cxx_std_17)
target_include_directories(nativekit
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE src
)
target_compile_definitions(nativekit PRIVATE NK_BUILDING_LIBRARY)
set_target_properties(nativekit PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
)

if(UNIX AND NOT APPLE)
    target_link_options(nativekit PRIVATE
        "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/cmake/nativekit.exports"
    )
endif()

if(MSVC)
    target_compile_options(nativekit PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(nativekit PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(NK_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(nativekit PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(nativekit PRIVATE -fsanitize=address,undefined)
endif()

if(NK_BUILD_TESTS)
    add_subdirectory(tests)
endif()

install(TARGETS nativekit EXPORT NativeKitTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
configure_package_config_file(
    cmake/NativeKitConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/NativeKitConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/NativeKit
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/NativeKitConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(EXPORT NativeKitTargets
    FILE NativeKitTargets.cmake
    NAMESPACE NativeKit::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/NativeKit
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/NativeKitConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/NativeKitConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/NativeKit
)
