cmake_minimum_required(VERSION 3.21)

project(nativekit VERSION 0.1.0 LANGUAGES C CXX)

set(NK_VENDOR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor")

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
option(NK_BUILD_SOKOL "Build the experimental Sokol graphics module" OFF)
option(NK_BUILD_SOKOL_BACKEND_MATRIX
    "Build independent prefixed Sokol runtimes for glcore and gles3" OFF)
option(NK_BUILD_UI "Build the experimental retained UI module" OFF)
option(NK_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(NK_ENABLE_WEBVIEW2 "Enable the Windows WebView2 backend" ON)

set(_nk_default_sokol_backend glcore)
if(ANDROID OR EMSCRIPTEN)
    set(_nk_default_sokol_backend gles3)
endif()
set(NK_SOKOL_BACKEND "${_nk_default_sokol_backend}" CACHE STRING
    "Sokol backend variant for this build (glcore or gles3)")
set_property(CACHE NK_SOKOL_BACKEND PROPERTY STRINGS glcore gles3)

if(ANDROID)
    set(NK_BUILD_SHARED ON)
endif()

find_program(NK_CLANG_FORMAT NAMES clang-format clang-format-20 clang-format-19
    clang-format-18 clang-format-17)
file(GLOB_RECURSE NK_FORMAT_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/*.[ch]"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.[ch]"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.[ch]"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cxx"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hh"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hxx"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.m"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.mm"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.[ch]"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cxx"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.hh"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.hxx"
    "${CMAKE_CURRENT_SOURCE_DIR}/modules/*/examples/*.[ch]"
    "${CMAKE_CURRENT_SOURCE_DIR}/modules/*/include/*.[ch]"
    "${CMAKE_CURRENT_SOURCE_DIR}/modules/*/src/*.[ch]"
    "${CMAKE_CURRENT_SOURCE_DIR}/modules/*/src/*.cpp"
)

if(NK_CLANG_FORMAT)
    add_custom_target(format
        COMMAND ${NK_CLANG_FORMAT} -i ${NK_FORMAT_SOURCES}
        COMMENT "Formatting NativeKit sources"
        VERBATIM)
    add_custom_target(format-check
        COMMAND ${NK_CLANG_FORMAT} --dry-run --Werror ${NK_FORMAT_SOURCES}
        COMMENT "Checking NativeKit source formatting"
        VERBATIM)
endif()

set(NK_LIBRARY_TYPE STATIC)
if(NK_BUILD_SHARED AND NOT EMSCRIPTEN)
    set(NK_LIBRARY_TYPE SHARED)
endif()

add_library(nativekit ${NK_LIBRARY_TYPE}
    src/core/accessibility.cpp
    src/core/api.cpp
    src/core/clipboard.cpp
    src/core/dialog.cpp
    src/core/error.cpp
    src/core/event_queue.cpp
    src/core/graphics.cpp
    src/core/graphics_android_callback.cpp
    src/core/gamepad.cpp
    src/core/gamepad_mapping.cpp
    src/core/handle_registry.cpp
    src/core/input.cpp
    src/core/joystick.cpp
    src/core/mobile.cpp
    src/core/monitor.cpp
    src/core/resource.cpp
    src/core/text_input.cpp
    src/core/time.cpp
    src/core/vulkan.cpp
    src/core/window_extensions.cpp
)
add_library(NativeKit::nativekit ALIAS nativekit)

if(NOT ANDROID)
    target_sources(nativekit PRIVATE src/platform/nativekit_resource_io.cpp)
endif()

target_compile_features(nativekit PRIVATE cxx_std_17)
target_include_directories(nativekit
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE src
)
target_compile_definitions(nativekit PRIVATE NK_BUILDING_LIBRARY)
if(NOT NK_BUILD_SHARED OR EMSCRIPTEN)
    target_compile_definitions(nativekit PUBLIC NK_STATIC)
endif()

if(EMSCRIPTEN)
    set(NK_WEB_CANVAS_SELECTOR "#canvas" CACHE STRING
        "CSS selector for the canvas used by NativeKit's browser backend")
    configure_file(src/platform/web/config.h.in
        "${CMAKE_CURRENT_BINARY_DIR}/nativekit_web_config.h" @ONLY)
    target_include_directories(nativekit PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
    target_compile_definitions(nativekit PRIVATE NK_BACKEND_WEB=1)
    # WebGL2 is part of the first browser backend.  Keep this linker detail on
    # the NativeKit target so consumers inherit it without seeing Emscripten
    # types or headers in the public API.
    target_link_options(nativekit INTERFACE
        "-sUSE_WEBGL2=1"
        "-sEXPORTED_RUNTIME_METHODS=ccall")

    set(NK_WASM_HOST_HEAP_LIMIT "134217728" CACHE STRING
        "Exclusive byte limit of NativeKit's bounded WebAssembly host heap")
    if(NOT NK_WASM_HOST_HEAP_LIMIT MATCHES "^[1-9][0-9]*$")
        message(FATAL_ERROR "NK_WASM_HOST_HEAP_LIMIT must be a positive integer byte offset")
    endif()

    # This process-wide allocator belongs to the WebAssembly platform layer,
    # not to any one NativeKit module. Link it once from the final host module
    # when the application partitions linear memory between host and guest.
    add_library(nativekit_wasm_host_allocator STATIC
        src/platform/wasm/host_allocator.cpp
        src/platform/wasm/host_allocator_wasm.cpp)
    add_library(NativeKit::wasm_host_allocator ALIAS nativekit_wasm_host_allocator)
    target_compile_features(nativekit_wasm_host_allocator PRIVATE cxx_std_17)
    target_include_directories(nativekit_wasm_host_allocator PRIVATE src)
    target_compile_definitions(nativekit_wasm_host_allocator PRIVATE
        NK_WASM_HOST_HEAP_LIMIT=${NK_WASM_HOST_HEAP_LIMIT})
    target_link_options(nativekit_wasm_host_allocator INTERFACE "-sMALLOC=none")
endif()

if(UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(GTK3 QUIET IMPORTED_TARGET gtk+-3.0)
        pkg_check_modules(WEBKITGTK QUIET IMPORTED_TARGET webkit2gtk-4.1)
    endif()
endif()

if(EMSCRIPTEN)
    target_sources(nativekit PRIVATE
        src/platform/nativekit_web.cpp
        src/platform/web/host.cpp
        src/platform/nativekit_stub.cpp)
elseif(ANDROID)
    target_sources(nativekit PRIVATE
        src/android/nativekit_android.cpp
        src/platform/nativekit_stub.cpp)
    target_compile_definitions(nativekit PRIVATE NK_BACKEND_ANDROID=1 NK_STUB_ANDROID=1)
    target_link_libraries(nativekit PRIVATE android EGL GLESv2 log)
elseif(WIN32)
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
    target_sources(nativekit PRIVATE
        src/linux/nativekit_gtk.cpp
        src/linux/nativekit_linux_joystick.cpp)
    target_link_libraries(nativekit PRIVATE PkgConfig::GTK3 PkgConfig::WEBKITGTK
        ${CMAKE_DL_LIBS})
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

if(UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN)
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
if(NK_BUILD_SOKOL OR NK_BUILD_UI)
    if(NOT EXISTS "${NK_VENDOR_DIR}/sokol/sokol_gfx.h")
        message(FATAL_ERROR "Sokol submodule is missing; run git submodule update --init")
    endif()
    if(NK_SOKOL_BACKEND STREQUAL "glcore")
        if(NOT UNIX OR APPLE OR ANDROID)
            message(FATAL_ERROR "NativeKit Sokol glcore requires desktop Linux")
        endif()
        find_package(OpenGL REQUIRED)
        set(NK_SOKOL_COMPILE_DEFINITION NK_SOKOL_BACKEND_GLCORE=1)
        set(NK_SOKOL_GRAPHICS_LIBRARIES OpenGL::GL)
        set(NK_SOKOL_GRAPHICS_INCLUDE_DIRS)
    elseif(NK_SOKOL_BACKEND STREQUAL "gles3")
        if(NOT EMSCRIPTEN AND (NOT UNIX OR APPLE))
            message(FATAL_ERROR "NativeKit Sokol gles3 requires Linux or Android")
        endif()
        if(NOT EMSCRIPTEN)
            find_path(NK_SOKOL_GLES3_INCLUDE_DIR GLES3/gl3.h REQUIRED)
            find_library(NK_SOKOL_GLES3_LIBRARY NAMES GLESv2 REQUIRED)
        endif()
        set(NK_SOKOL_COMPILE_DEFINITION NK_SOKOL_BACKEND_GLES3=1)
        if(EMSCRIPTEN)
            set(NK_SOKOL_GRAPHICS_LIBRARIES)
            set(NK_SOKOL_GRAPHICS_INCLUDE_DIRS)
        else()
            set(NK_SOKOL_GRAPHICS_LIBRARIES "${NK_SOKOL_GLES3_LIBRARY}")
            set(NK_SOKOL_GRAPHICS_INCLUDE_DIRS "${NK_SOKOL_GLES3_INCLUDE_DIR}")
        endif()
    else()
        message(FATAL_ERROR
            "Unsupported NK_SOKOL_BACKEND='${NK_SOKOL_BACKEND}'; choose glcore or gles3")
    endif()
    function(nk_add_sokol_runtime target compile_definition symbol_prefix graphics_libraries
             graphics_include_dirs)
        add_library(${target} ${NK_LIBRARY_TYPE} modules/sokol/src/runtime.c modules/sokol/src/api.c)
        target_include_directories(${target} PRIVATE
            "${NK_VENDOR_DIR}/sokol" "${CMAKE_CURRENT_SOURCE_DIR}/modules/sokol/src"
            ${graphics_include_dirs})
        target_compile_definitions(${target} PRIVATE ${compile_definition})
        if(symbol_prefix)
            target_compile_definitions(${target} PRIVATE
                SOKOL_GFX_SYMBOL_PREFIX=${symbol_prefix}
                NK_SOKOL_API_PREFIX=${symbol_prefix}
                NK_SOKOL_RUNTIME_PREFIX=${symbol_prefix}runtime_)
        endif()
        target_link_libraries(${target} PRIVATE ${graphics_libraries})
        set_target_properties(${target} PROPERTIES
            C_VISIBILITY_PRESET default
            VERSION ${PROJECT_VERSION}
            SOVERSION 0)
    endfunction()

    nk_add_sokol_runtime(nativekit_sokol_runtime ${NK_SOKOL_COMPILE_DEFINITION} ""
        "${NK_SOKOL_GRAPHICS_LIBRARIES}" "${NK_SOKOL_GRAPHICS_INCLUDE_DIRS}")
    target_compile_definitions(nativekit_sokol_runtime PUBLIC ${NK_SOKOL_COMPILE_DEFINITION})
    install(TARGETS nativekit_sokol_runtime EXPORT NativeKitTargets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

    if(NK_BUILD_SOKOL_BACKEND_MATRIX)
        if(EMSCRIPTEN OR NOT UNIX OR APPLE OR ANDROID)
            message(FATAL_ERROR "NativeKit Sokol backend matrix requires desktop Linux")
        endif()
        find_package(OpenGL REQUIRED)
        find_path(NK_SOKOL_MATRIX_GLES3_INCLUDE_DIR GLES3/gl3.h REQUIRED)
        find_library(NK_SOKOL_MATRIX_GLES3_LIBRARY NAMES GLESv2 REQUIRED)
        nk_add_sokol_runtime(nativekit_sokol_runtime_glcore NK_SOKOL_BACKEND_GLCORE=1
            nk_sokol_glcore_ "OpenGL::GL" "")
        nk_add_sokol_runtime(nativekit_sokol_runtime_gles3 NK_SOKOL_BACKEND_GLES3=1
            nk_sokol_gles3_ "${NK_SOKOL_MATRIX_GLES3_LIBRARY}"
            "${NK_SOKOL_MATRIX_GLES3_INCLUDE_DIR}")
        install(TARGETS nativekit_sokol_runtime_glcore nativekit_sokol_runtime_gles3
            EXPORT NativeKitTargets
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
    endif()
endif()
if(NK_BUILD_SOKOL)
    add_subdirectory(modules/sokol)
endif()
if(NK_BUILD_UI)
    add_subdirectory(modules/ui)
endif()

install(TARGETS nativekit EXPORT NativeKitTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/NativeKit)
install(FILES vendor/SDL_GameControllerDB/LICENSE
    DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/NativeKit
    RENAME SDL_GameControllerDB.txt)
if(WIN32 AND NK_WEBVIEW2_LOADER_DLL)
    install(FILES "${NK_WEBVIEW2_LOADER_DLL}" DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES "${NK_WEBVIEW2_LICENSE}" "${NK_WEBVIEW2_NOTICE}"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/NativeKit/WebView2)
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
