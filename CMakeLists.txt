cmake_minimum_required(VERSION 3.21)

project(nativekit VERSION 0.1.0 LANGUAGES C CXX)

# Keep Windows DLLs and the executables that load them together.  Targets are
# declared from several subdirectories, whose default runtime directories would
# otherwise differ and make shared-build tests fail before reaching main().
if(WIN32)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()

if(APPLE)
    enable_language(OBJCXX)
endif()

include(GNUInstallDirs)
include(CTest)
include(CMakePackageConfigHelpers)

option(NK_BUILD_SHARED "Build NativeKit as a shared library" ON)
option(NK_BUILD_TESTS "Build NativeKit tests" ${BUILD_TESTING})
option(NK_BUILD_EXAMPLES "Build NativeKit examples" ON)
option(NK_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(NK_ENABLE_WEBVIEW2 "Enable the Windows WebView2 backend" ON)

set(NK_LIBRARY_TYPE STATIC)
if(NK_BUILD_SHARED)
    set(NK_LIBRARY_TYPE SHARED)
endif()

add_library(nativekit ${NK_LIBRARY_TYPE}
    src/core/api.cpp
    src/core/clipboard.cpp
    src/core/dialog.cpp
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
if(NOT NK_BUILD_SHARED)
    target_compile_definitions(nativekit PUBLIC NK_STATIC)
endif()

if(UNIX AND NOT APPLE)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(GTK3 QUIET IMPORTED_TARGET gtk+-3.0)
        pkg_check_modules(WEBKITGTK QUIET IMPORTED_TARGET webkit2gtk-4.1)
    endif()
endif()

if(WIN32)
    if(NK_ENABLE_WEBVIEW2)
        include(FetchContent)
        FetchContent_Declare(nativekit_webview2
            URL https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/1.0.4191.47
            URL_HASH SHA256=f492bbf547d0da329553b6727435b677579b1e9f91cc9e4a1ad029366d5f23d0
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        FetchContent_MakeAvailable(nativekit_webview2)
        target_include_directories(nativekit PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/compat/webview2")
        target_include_directories(nativekit SYSTEM PRIVATE
            "${nativekit_webview2_SOURCE_DIR}/build/native/include")
        target_compile_definitions(nativekit PRIVATE NK_HAS_WEBVIEW2=1)
        if(CMAKE_GENERATOR_PLATFORM MATCHES "^[Aa][Rr][Mm]64$" OR
           CMAKE_SYSTEM_PROCESSOR MATCHES "^[Aa][Rr][Mm]64$")
            set(NK_WEBVIEW2_ARCH arm64)
        elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
            set(NK_WEBVIEW2_ARCH x86)
        else()
            set(NK_WEBVIEW2_ARCH x64)
        endif()
        set(NK_WEBVIEW2_LOADER_DLL
            "${nativekit_webview2_SOURCE_DIR}/build/native/${NK_WEBVIEW2_ARCH}/WebView2Loader.dll")
        set(NK_WEBVIEW2_LICENSE "${nativekit_webview2_SOURCE_DIR}/LICENSE.txt")
        set(NK_WEBVIEW2_NOTICE "${nativekit_webview2_SOURCE_DIR}/NOTICE.txt")
        add_custom_command(TARGET nativekit POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${NK_WEBVIEW2_LOADER_DLL}" "$<TARGET_FILE_DIR:nativekit>")
    endif()
    target_sources(nativekit PRIVATE src/windows/nativekit_win.cpp)
    target_link_libraries(nativekit PRIVATE user32 shell32 ole32 advapi32)
elseif(APPLE)
    target_sources(nativekit PRIVATE src/macos/nativekit_osx.mm)
    set_target_properties(nativekit PROPERTIES
        OBJCXX_STANDARD 17
        OBJCXX_STANDARD_REQUIRED YES)
    target_compile_options(nativekit PRIVATE
        $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>
        $<$<COMPILE_LANGUAGE:OBJCXX>:-fblocks>)
    target_link_libraries(nativekit PRIVATE "-framework Cocoa" "-framework WebKit"
        "-framework UserNotifications")
elseif(TARGET PkgConfig::GTK3 AND TARGET PkgConfig::WEBKITGTK)
    set(NK_BACKEND_GTK_AVAILABLE TRUE)
    target_sources(nativekit PRIVATE src/linux/nativekit_gtk.cpp)
    target_link_libraries(nativekit PRIVATE PkgConfig::GTK3 PkgConfig::WEBKITGTK)
    target_compile_definitions(nativekit PRIVATE NK_BACKEND_GTK=1)
else()
    target_sources(nativekit PRIVATE src/platform/nativekit_stub.cpp)
endif()
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
if(NK_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

install(TARGETS nativekit EXPORT NativeKitTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY licenses/ DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/NativeKit)
if(WIN32 AND NK_WEBVIEW2_LOADER_DLL)
    install(FILES "${NK_WEBVIEW2_LOADER_DLL}" DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES "${NK_WEBVIEW2_LICENSE}" "${NK_WEBVIEW2_NOTICE}"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/NativeKit/WebView2)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/wxWidgets/docs/licence.txt")
    install(FILES vendor/wxWidgets/docs/lgpl.txt
        DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/NativeKit
        RENAME LGPL-2.0.txt)
endif()
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
