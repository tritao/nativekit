option(NKUI_ENABLE_SECTION_GC
    "Compile UI code into discardable sections and garbage-collect unused sections"
    ON)
option(NKUI_ENABLE_CLAY_DEBUG
    "Keep Clay's internal debug view in the NativeKit UI build"
    OFF)
option(NKUI_ENABLE_BUDOUX
    "Enable Skribidi's language-specific BudouX word-break models"
    OFF)

include(cmake/SkribidiSubmodules.cmake)
include(cmake/NanoVGSubmodules.cmake)

add_library(nativekit_ui ${NK_LIBRARY_TYPE}
    src/api.cpp
    src/image_decode.cpp
    src/compositor/compositor.cpp
    src/compositor/render_plan.cpp
    src/display_list/display_list.cpp
    src/layout/clay_layout_backend.cpp
    src/layout/layout_render_compiler.cpp
    src/prepare/nanovg_path.cpp
    src/prepare/skribidi_adapter.cpp
    src/prepare/system_fonts.cpp
    src/render/frame_resources.cpp
    src/render/render_plan_executor.cpp
    src/render/sealed_render_plan.cpp
    src/render/ui_renderer.cpp)
add_library(NativeKit::ui ALIAS nativekit_ui)
target_compile_features(nativekit_ui PRIVATE cxx_std_20)
if(NK_ENABLE_NO_EXCEPTIONS)
    target_compile_definitions(nativekit_ui PRIVATE NK_ENABLE_NO_EXCEPTIONS=1)
    if(MSVC)
        target_compile_options(nativekit_ui PRIVATE /EHs-c-)
    else()
        target_compile_options(nativekit_ui PRIVATE -fno-exceptions)
    endif()
endif()
target_include_directories(nativekit_ui
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE src)
target_include_directories(nativekit_ui PRIVATE "${NK_VENDOR_DIR}/clay")
target_include_directories(nativekit_ui PRIVATE "${NK_VENDOR_DIR}/nanovg/src")
target_include_directories(nativekit_ui PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_include_directories(nativekit_ui PRIVATE "${PROJECT_SOURCE_DIR}/modules/gpu/src")
target_link_libraries(nativekit_ui PUBLIC NativeKit::gpu
    PRIVATE nkui_skribidi nkui_nanovg_core)
if(UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(FONTCONFIG QUIET IMPORTED_TARGET fontconfig)
    endif()
    if(TARGET PkgConfig::FONTCONFIG)
        target_link_libraries(nativekit_ui PRIVATE PkgConfig::FONTCONFIG)
        target_compile_definitions(nativekit_ui PRIVATE NKUI_HAS_FONTCONFIG=1)
        set(NKUI_HAS_FONTCONFIG ON PARENT_SCOPE)
    endif()
elseif(APPLE)
    target_link_libraries(nativekit_ui PRIVATE "-framework CoreText" "-framework CoreFoundation")
endif()
target_compile_definitions(nativekit_ui PRIVATE NKUI_BUILDING_LIBRARY)
if(NOT NKUI_ENABLE_CLAY_DEBUG)
    target_compile_definitions(nativekit_ui PRIVATE CLAY_DISABLE_DEBUG)
endif()
if(NK_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(nativekit_ui PRIVATE
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(nativekit_ui PRIVATE -fsanitize=address,undefined)
endif()
set_target_properties(nativekit_ui PROPERTIES
    EXPORT_NAME ui
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    VERSION ${PROJECT_VERSION}
    SOVERSION 0)

if(NKUI_ENABLE_SECTION_GC AND
   CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang" AND
   CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    set(NKUI_SIZE_TARGETS
        nativekit_ui harfbuzz nkui_sheenbidi nkui_libunibreak
        nkui_skribidi nkui_nanovg_core)
    if(TARGET nkui_budouxc)
        list(APPEND NKUI_SIZE_TARGETS nkui_budouxc)
    endif()
    foreach(NKUI_SIZE_TARGET IN LISTS NKUI_SIZE_TARGETS)
        target_compile_options(${NKUI_SIZE_TARGET} PRIVATE
            -ffunction-sections -fdata-sections)
    endforeach()
    if(NK_LIBRARY_TYPE STREQUAL "SHARED" AND NOT APPLE)
        target_link_options(nativekit_ui PRIVATE -Wl,--gc-sections)
    endif()
endif()

if(NK_BUILD_TESTS)
    add_test(NAME nativekit_ui_dependencies
        COMMAND "${CMAKE_COMMAND}"
            "-DNK_REPO_DIR=${PROJECT_SOURCE_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tools/check-dependencies.cmake")
    if(WIN32 OR APPLE OR (UNIX AND NOT ANDROID AND NOT EMSCRIPTEN))
        add_executable(nativekit_ui_backend_smoke_test
            tests/backend_renderer_smoke.cpp)
        target_link_libraries(nativekit_ui_backend_smoke_test PRIVATE
            NativeKit::ui NativeKit::gpu NativeKit::nativekit)
        set(NKUI_BACKEND_SMOKE_REGISTERED OFF)
        if(WIN32 OR APPLE)
            add_test(NAME nativekit_ui_backend_smoke
                COMMAND nativekit_ui_backend_smoke_test)
            set(NKUI_BACKEND_SMOKE_REGISTERED ON)
        else()
            find_program(NKUI_BACKEND_XVFB_RUN xvfb-run)
            if(NKUI_BACKEND_XVFB_RUN)
                add_test(NAME nativekit_ui_backend_smoke
                    COMMAND ${NKUI_BACKEND_XVFB_RUN} -a
                        $<TARGET_FILE:nativekit_ui_backend_smoke_test>)
                set_tests_properties(nativekit_ui_backend_smoke PROPERTIES
                    ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1")
                set(NKUI_BACKEND_SMOKE_REGISTERED ON)
            endif()
        endif()
        if(NKUI_BACKEND_SMOKE_REGISTERED)
            set_tests_properties(nativekit_ui_backend_smoke PROPERTIES
                LABELS "graphics;gpu;ui"
                TIMEOUT 15)
        endif()
    endif()
    add_executable(nativekit_ui_abi_test tests/abi.c)
    target_compile_definitions(nativekit_ui_abi_test PRIVATE
        NKUI_TEST_FONT_PATH="${NK_VENDOR_DIR}/skribidi/example/data/IBMPlexSans-Regular.ttf")
    target_link_libraries(nativekit_ui_abi_test PRIVATE NativeKit::ui)
    add_test(NAME nativekit_ui_abi COMMAND nativekit_ui_abi_test)
    add_executable(nativekit_ui_display_list_test
        tests/display_list.cpp
        src/display_list/display_list.cpp)
    target_include_directories(nativekit_ui_display_list_test PRIVATE src)
    add_test(NAME nativekit_ui_display_list COMMAND nativekit_ui_display_list_test)
    add_executable(nativekit_ui_image_pixels_test tests/image_pixels.cpp)
    target_include_directories(nativekit_ui_image_pixels_test PRIVATE src)
    add_test(NAME nativekit_ui_image_pixels COMMAND nativekit_ui_image_pixels_test)
    add_executable(nativekit_ui_nanovg_recorder_test
        tests/nanovg_recorder.cpp
        src/prepare/nanovg_recorder.cpp)
    target_include_directories(nativekit_ui_nanovg_recorder_test PRIVATE src)
    target_link_libraries(nativekit_ui_nanovg_recorder_test PRIVATE nkui_nanovg_core)
    add_test(NAME nativekit_ui_nanovg_recorder COMMAND nativekit_ui_nanovg_recorder_test)
    add_executable(nativekit_ui_nanovg_path_test
        tests/nanovg_path.cpp
        src/prepare/nanovg_recorder.cpp
        src/prepare/nanovg_path.cpp)
    target_include_directories(nativekit_ui_nanovg_path_test PRIVATE src)
    target_link_libraries(nativekit_ui_nanovg_path_test PRIVATE nkui_nanovg_core)
    add_test(NAME nativekit_ui_nanovg_path COMMAND nativekit_ui_nanovg_path_test)
    add_executable(nativekit_ui_skribidi_adapter_test
        tests/skribidi_adapter.cpp
        src/prepare/skribidi_adapter.cpp
        src/prepare/system_fonts.cpp)
    target_include_directories(nativekit_ui_skribidi_adapter_test PRIVATE src)
    target_compile_definitions(nativekit_ui_skribidi_adapter_test PRIVATE
        NKUI_TEST_FONT_PATH="${NK_VENDOR_DIR}/skribidi/example/data/IBMPlexSans-Regular.ttf"
        NKUI_TEST_COLOR_FONT_PATH="${NK_VENDOR_DIR}/skribidi/example/data/NotoColorEmoji-Regular.ttf")
    target_link_libraries(nativekit_ui_skribidi_adapter_test PRIVATE nkui_skribidi)
    add_test(NAME nativekit_ui_skribidi_adapter COMMAND nativekit_ui_skribidi_adapter_test)
    add_executable(nativekit_ui_compositor_test
        tests/compositor.cpp
        src/compositor/compositor.cpp
        src/compositor/render_plan.cpp
        src/display_list/display_list.cpp)
    target_include_directories(nativekit_ui_compositor_test PRIVATE src)
    add_test(NAME nativekit_ui_compositor COMMAND nativekit_ui_compositor_test)
    add_executable(nativekit_ui_frame_resources_test
        tests/frame_resources.cpp
        src/display_list/display_list.cpp
        src/prepare/nanovg_recorder.cpp
        src/prepare/nanovg_path.cpp
        src/render/frame_resources.cpp)
    target_include_directories(nativekit_ui_frame_resources_test PRIVATE
        src "${PROJECT_SOURCE_DIR}/include")
    target_link_libraries(nativekit_ui_frame_resources_test PRIVATE
        nkui_nanovg_core NativeKit::nativekit)
    add_test(NAME nativekit_ui_frame_resources COMMAND nativekit_ui_frame_resources_test)
    add_executable(nativekit_ui_layout_engine_test
        tests/layout_engine.cpp
        src/layout/clay_layout_backend.cpp
        src/prepare/skribidi_adapter.cpp
        src/prepare/system_fonts.cpp)
    target_include_directories(nativekit_ui_layout_engine_test PRIVATE
        src "${NK_VENDOR_DIR}/clay")
    target_compile_definitions(nativekit_ui_layout_engine_test PRIVATE
        NKUI_TEST_FONT_PATH="${NK_VENDOR_DIR}/skribidi/example/data/IBMPlexSans-Regular.ttf")
    target_link_libraries(nativekit_ui_layout_engine_test PRIVATE nkui_skribidi)
    add_test(NAME nativekit_ui_layout_engine COMMAND nativekit_ui_layout_engine_test)
    add_executable(nativekit_ui_clay_measure_element_test
        "${NK_VENDOR_DIR}/clay/tests/measure_element.cpp")
    target_compile_features(nativekit_ui_clay_measure_element_test PRIVATE cxx_std_20)
    target_include_directories(nativekit_ui_clay_measure_element_test PRIVATE
        "${NK_VENDOR_DIR}/clay")
    add_test(NAME nativekit_ui_clay_measure_element
        COMMAND nativekit_ui_clay_measure_element_test)
    add_executable(nativekit_ui_clay_layout_benchmark
        "${NK_VENDOR_DIR}/clay/tests/layout_benchmark.cpp")
    target_compile_features(nativekit_ui_clay_layout_benchmark PRIVATE cxx_std_20)
    target_include_directories(nativekit_ui_clay_layout_benchmark PRIVATE
        "${NK_VENDOR_DIR}/clay")
    add_test(NAME nativekit_ui_clay_layout_benchmark
        COMMAND nativekit_ui_clay_layout_benchmark)
    add_executable(nativekit_ui_layout_invariants_test
        tests/layout_invariants.cpp
        src/layout/clay_layout_backend.cpp
        src/prepare/skribidi_adapter.cpp
        src/prepare/system_fonts.cpp)
    target_include_directories(nativekit_ui_layout_invariants_test PRIVATE
        src "${NK_VENDOR_DIR}/clay")
    target_compile_definitions(nativekit_ui_layout_invariants_test PRIVATE
        NKUI_TEST_FONT_PATH="${NK_VENDOR_DIR}/skribidi/example/data/IBMPlexSans-Regular.ttf")
    target_link_libraries(nativekit_ui_layout_invariants_test PRIVATE nkui_skribidi)
    add_test(NAME nativekit_ui_layout_invariants COMMAND nativekit_ui_layout_invariants_test)
    add_executable(nativekit_ui_layout_render_compiler_test
        tests/layout_render_compiler.cpp
        src/layout/clay_layout_backend.cpp
        src/layout/layout_render_compiler.cpp
        src/display_list/display_list.cpp
        src/prepare/nanovg_path.cpp
        src/prepare/skribidi_adapter.cpp
        src/prepare/system_fonts.cpp
    src/render/frame_resources.cpp
    src/render/render_plan_executor.cpp
    src/render/sealed_render_plan.cpp
    src/compositor/render_plan.cpp)
    target_include_directories(nativekit_ui_layout_render_compiler_test PRIVATE
        src "${PROJECT_SOURCE_DIR}/include" "${NK_VENDOR_DIR}/clay")
    target_compile_definitions(nativekit_ui_layout_render_compiler_test PRIVATE
        NKUI_TEST_FONT_PATH="${NK_VENDOR_DIR}/skribidi/example/data/IBMPlexSans-Regular.ttf")
    target_link_libraries(nativekit_ui_layout_render_compiler_test PRIVATE
        nkui_nanovg_core nkui_skribidi NativeKit::nativekit)
    add_test(NAME nativekit_ui_layout_render_compiler
        COMMAND nativekit_ui_layout_render_compiler_test)
    add_executable(nativekit_ui_layout_session_test
        tests/layout_session.cpp)
    target_compile_definitions(nativekit_ui_layout_session_test PRIVATE
        NKUI_TEST_FONT_PATH="${NK_VENDOR_DIR}/skribidi/example/data/IBMPlexSans-Regular.ttf")
    target_link_libraries(nativekit_ui_layout_session_test PRIVATE NativeKit::ui)
    add_test(NAME nativekit_ui_layout_session COMMAND nativekit_ui_layout_session_test)
    if(TARGET NativeKit::gpu)
        add_executable(nativekit_ui_public_renderer_smoke_test
            tests/public_renderer_smoke.cpp)
        target_compile_definitions(nativekit_ui_public_renderer_smoke_test PRIVATE
            NKGPU_TESTING=1)
        target_include_directories(nativekit_ui_public_renderer_smoke_test PRIVATE
            "${CMAKE_SOURCE_DIR}/modules/gpu/src")
        target_compile_definitions(nativekit_ui_public_renderer_smoke_test PRIVATE
            NKUI_TEST_FONT_PATH="${NK_VENDOR_DIR}/skribidi/example/data/IBMPlexSans-Regular.ttf")
        target_link_libraries(nativekit_ui_public_renderer_smoke_test PRIVATE
            NativeKit::ui)
        if(NOT EMSCRIPTEN AND NOT ANDROID)
            find_package(OpenGL QUIET)
            if(TARGET OpenGL::GL)
                target_link_libraries(nativekit_ui_public_renderer_smoke_test PRIVATE
                    OpenGL::GL)
            endif()
        endif()
        add_executable(nativekit_ui_session_render_smoke_test
            tests/session_render_smoke.cpp)
        target_compile_definitions(nativekit_ui_session_render_smoke_test PRIVATE
            NKGPU_TESTING=1)
        target_include_directories(nativekit_ui_session_render_smoke_test PRIVATE
            "${CMAKE_SOURCE_DIR}/modules/gpu/src")
        target_compile_definitions(nativekit_ui_session_render_smoke_test PRIVATE
            NKUI_TEST_FONT_PATH="${NK_VENDOR_DIR}/skribidi/example/data/IBMPlexSans-Regular.ttf")
        target_link_libraries(nativekit_ui_session_render_smoke_test PRIVATE
            NativeKit::ui)
        if(NOT EMSCRIPTEN AND NOT ANDROID)
            find_package(OpenGL QUIET)
            if(TARGET OpenGL::GL)
                target_link_libraries(nativekit_ui_session_render_smoke_test PRIVATE
                    OpenGL::GL)
            endif()
        endif()
    endif()
    find_program(NKUI_BACKEND_XVFB_RUN xvfb-run)
    if(NKUI_BACKEND_XVFB_RUN AND TARGET nativekit_ui_public_renderer_smoke_test)
        add_test(NAME nativekit_ui_public_renderer_smoke
            COMMAND ${NKUI_BACKEND_XVFB_RUN} -a
                $<TARGET_FILE:nativekit_ui_public_renderer_smoke_test>)
        add_test(NAME nativekit_ui_stress
            COMMAND ${NKUI_BACKEND_XVFB_RUN} -a
                $<TARGET_FILE:nativekit_ui_public_renderer_smoke_test> --stress)
        set_tests_properties(nativekit_ui_public_renderer_smoke nativekit_ui_stress PROPERTIES
            LABELS "graphics;ui"
            TIMEOUT 45
            ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1")
    endif()
    if(NKUI_BACKEND_XVFB_RUN AND TARGET nativekit_ui_session_render_smoke_test)
        add_test(NAME nativekit_ui_session_render_smoke
            COMMAND ${NKUI_BACKEND_XVFB_RUN} -a
                $<TARGET_FILE:nativekit_ui_session_render_smoke_test>)
        set_tests_properties(nativekit_ui_session_render_smoke PROPERTIES
            LABELS "graphics;ui"
            TIMEOUT 45
            ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1")
    endif()
    foreach(target IN ITEMS
        nativekit_ui_backend_smoke_test
        nativekit_ui_display_list_test
        nativekit_ui_image_pixels_test
        nativekit_ui_nanovg_recorder_test
        nativekit_ui_nanovg_path_test
        nativekit_ui_skribidi_adapter_test
        nativekit_ui_compositor_test
        nativekit_ui_frame_resources_test
        nativekit_ui_layout_engine_test
        nativekit_ui_layout_invariants_test
        nativekit_ui_layout_render_compiler_test
        nativekit_ui_layout_session_test
        nativekit_ui_public_renderer_smoke_test
        nativekit_ui_session_render_smoke_test)
        if(TARGET ${target})
            target_compile_features(${target} PRIVATE cxx_std_20)
        endif()
    endforeach()
    if(NK_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        foreach(target IN ITEMS
            nativekit_ui_layout_engine_test
            nativekit_ui_layout_invariants_test
            nativekit_ui_layout_session_test)
            if(TARGET ${target})
                target_compile_options(${target} PRIVATE
                    -fsanitize=address,undefined -fno-omit-frame-pointer)
                target_link_options(${target} PRIVATE -fsanitize=address,undefined)
            endif()
        endforeach()
    endif()
endif()

if(NK_BUILD_EXAMPLES)
    target_sources(nativekit_ui PRIVATE src/render/cube_surface_producer.cpp)
    target_compile_definitions(nativekit_ui PRIVATE NKUI_ENABLE_SHOWCASE_PRODUCER=1)
    add_executable(nativekit_ui_c_api examples/ui_c_api/showcase.cpp)
    set(NKUI_C_API_FONT_PATH
        "${NK_VENDOR_DIR}/skribidi/example/data/IBMPlexSans-Regular.ttf")
    target_compile_definitions(nativekit_ui_c_api PRIVATE
        NKUI_TEST_FONT_PATH="${NKUI_C_API_FONT_PATH}")
    target_link_libraries(nativekit_ui_c_api PRIVATE NativeKit::ui NativeKit::nativekit
        NativeKit::gpu)
    if(EMSCRIPTEN)
        add_custom_command(TARGET nativekit_ui_c_api POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:nativekit_ui_c_api>/assets"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${NKUI_C_API_FONT_PATH}"
                "$<TARGET_FILE_DIR:nativekit_ui_c_api>/assets/IBMPlexSans-Regular.ttf"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/examples/ui_c_api/web_shell.html"
                "$<TARGET_FILE_DIR:nativekit_ui_c_api>/nativekit_ui_c_api.html"
            COMMENT "Copying NativeKit UI browser host")
    endif()
    if(NK_BUILD_TESTS)
        find_program(NKUI_C_API_XVFB_RUN xvfb-run)
        if(NKUI_C_API_XVFB_RUN)
            add_test(NAME nativekit_ui_c_api_smoke
                COMMAND ${NKUI_C_API_XVFB_RUN} -a
                    $<TARGET_FILE:nativekit_ui_c_api> --smoke-test)
            set_tests_properties(nativekit_ui_c_api_smoke PROPERTIES
                LABELS "graphics;ui;abi"
                TIMEOUT 15
                ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1")
        endif()
    endif()

    # Capture the compiler checkout in the build cache so CTest and custom
    # targets keep using the same Haxeon branch across reconfigures.
    set(NKUI_HAXEON_DIR "$ENV{HAXEON_DIR}" CACHE PATH
        "Haxeon checkout used to compile the NativeKit UI sources")
    if(NOT NKUI_HAXEON_DIR)
        set(NKUI_HAXEON_DIR "${PROJECT_SOURCE_DIR}/../realtime-haxe" CACHE PATH
            "Haxeon checkout used to compile the NativeKit UI sources" FORCE)
    endif()
    set(NKUI_HAXEON_HAXE_BIN "$ENV{NATIVEKIT_HAXE_BIN}")
    if(NOT NKUI_HAXEON_HAXE_BIN)
        set(NKUI_HAXEON_HAXE_BIN "${NKUI_HAXEON_DIR}/.tools/haxe/haxe")
    endif()
    if(EXISTS "${NKUI_HAXEON_HAXE_BIN}")
        file(GLOB_RECURSE NKUI_HAXEON_DEPS CONFIGURE_DEPENDS
            "${NKUI_HAXEON_DIR}/src/*.hx")
        add_custom_target(nativekit_ui_showcase
            COMMAND ${CMAKE_COMMAND} -E env
                "NATIVEKIT_BUILD_DIR=${CMAKE_BINARY_DIR}"
                "HAXEON_DIR=${NKUI_HAXEON_DIR}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/showcase.sh" --build-only
            DEPENDS nativekit_ui
                "${CMAKE_CURRENT_SOURCE_DIR}/bindings/nativekit-ui-showcase.hxmap"
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
            USES_TERMINAL)

        if(NK_BUILD_TESTS)
            find_program(NKUI_SHOWCASE_XVFB_RUN xvfb-run)
            add_test(NAME nativekit_ui_framework_smoke
                COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tools/test-haxeon-framework.sh")
            set_tests_properties(nativekit_ui_framework_smoke PROPERTIES
                LABELS "ui;haxe;framework"
                TIMEOUT 120
                ENVIRONMENT "NATIVEKIT_BUILD_DIR=${CMAKE_BINARY_DIR};HAXEON_DIR=${NKUI_HAXEON_DIR}")
            if(NKUI_SHOWCASE_XVFB_RUN)
                add_test(NAME nativekit_ui_explorer_smoke
                    COMMAND ${NKUI_SHOWCASE_XVFB_RUN} -a
                        "${CMAKE_CURRENT_SOURCE_DIR}/tools/showcase.sh"
                        --ui-smoke-test)
                set_tests_properties(nativekit_ui_explorer_smoke PROPERTIES
                    LABELS "ui;haxe;showcase;smoke"
                    TIMEOUT 120
                    ENVIRONMENT
                        "LIBGL_ALWAYS_SOFTWARE=1;NATIVEKIT_BUILD_DIR=${CMAKE_BINARY_DIR};HAXEON_DIR=${NKUI_HAXEON_DIR}")
                add_test(NAME nativekit_ui_window_chrome
                    COMMAND ${NKUI_SHOWCASE_XVFB_RUN} -a
                        "${CMAKE_CURRENT_SOURCE_DIR}/tools/showcase.sh"
                        --ui-window-chrome-test)
                set_tests_properties(nativekit_ui_window_chrome PROPERTIES
                    LABELS "graphics;ui;showcase;window-chrome"
                    TIMEOUT 120
                    ENVIRONMENT
                        "LIBGL_ALWAYS_SOFTWARE=1;NATIVEKIT_BUILD_DIR=${CMAKE_BINARY_DIR};HAXEON_DIR=${NKUI_HAXEON_DIR}")
                add_test(NAME nativekit_ui_showcase_static
                    COMMAND ${NKUI_SHOWCASE_XVFB_RUN} -a
                        "${CMAKE_CURRENT_SOURCE_DIR}/tools/showcase.sh"
                        --static-frame --stats)
                set_tests_properties(nativekit_ui_showcase_static PROPERTIES
                    LABELS "graphics;ui;showcase;visual"
                    TIMEOUT 120
                    ENVIRONMENT
                        "LIBGL_ALWAYS_SOFTWARE=1;NATIVEKIT_BUILD_DIR=${CMAKE_BINARY_DIR};HAXEON_DIR=${NKUI_HAXEON_DIR}")
            endif()
        endif()

        if(EMSCRIPTEN)
            option(NKUI_HAXEON_BUNDLE_FONTS
                "Bundle Haxeon showcase fonts into the Emscripten data file"
                OFF)
            option(NKUI_HAXEON_SUBSET_FONTS
                "Subset Haxeon showcase fonts to glyphs used by the showcase"
                ON)
            set(NKUI_HAXEON_TARGET "wasm32" CACHE STRING
                "Haxeon WebAssembly target used by the browser UI Explorer guest")
            set_property(CACHE NKUI_HAXEON_TARGET PROPERTY STRINGS wasm32 wasm-gc)
            if(NOT NKUI_HAXEON_TARGET STREQUAL "wasm32" AND
               NOT NKUI_HAXEON_TARGET STREQUAL "wasm-gc")
                message(FATAL_ERROR
                    "Unsupported NKUI_HAXEON_TARGET=${NKUI_HAXEON_TARGET}; expected wasm32 or wasm-gc")
            endif()
            set(NKUI_HAXEON_EXCEPTION_MODE "legacy" CACHE STRING
                "Wasm exception encoding used by the browser UI Explorer guest")
            set_property(CACHE NKUI_HAXEON_EXCEPTION_MODE PROPERTY STRINGS legacy try-table)
            if(NOT NKUI_HAXEON_EXCEPTION_MODE STREQUAL "legacy" AND
               NOT NKUI_HAXEON_EXCEPTION_MODE STREQUAL "try-table")
                message(FATAL_ERROR
                    "Unsupported NKUI_HAXEON_EXCEPTION_MODE=${NKUI_HAXEON_EXCEPTION_MODE}; expected legacy or try-table")
            endif()
            option(NKUI_HAXEON_MEMORY_STATS
                "Export Haxeon guest allocator counters for Wasm benchmark profiling" OFF)
            if(NKUI_HAXEON_MEMORY_STATS AND NKUI_HAXEON_TARGET STREQUAL "wasm-gc")
                message(FATAL_ERROR
                    "NKUI_HAXEON_MEMORY_STATS is currently supported only with NKUI_HAXEON_TARGET=wasm32")
            endif()
            file(GLOB_RECURSE NKUI_HAXEON_GUEST_HX_DEPS CONFIGURE_DEPENDS
                "${CMAKE_CURRENT_SOURCE_DIR}/examples/ui_showcase/*.hx"
                "${CMAKE_CURRENT_SOURCE_DIR}/haxe/nativekit/ui/*.hx"
                "${CMAKE_CURRENT_SOURCE_DIR}/bindings/haxe/*.hx"
                "${PROJECT_SOURCE_DIR}/bindings/haxe/*.hx")
            set(NKUI_HAXEON_MEMORY_CONTRACT_VERSION 1)
            set(NKUI_HAXEON_WASM_PAGE_SIZE 65536)
            set(NKUI_HAXEON_HOST_HEAP_BASE 0)
            set(NKUI_HAXEON_HOST_HEAP_LIMIT ${NK_WASM_HOST_HEAP_LIMIT})
            set(NKUI_HAXEON_GUEST_MEMORY_BASE ${NKUI_HAXEON_HOST_HEAP_LIMIT})
            set(NKUI_HAXEON_GUEST_MEMORY_LIMIT 268435456)
            set(NKUI_HAXEON_MEMORY_LIMIT ${NKUI_HAXEON_GUEST_MEMORY_LIMIT})
            set(NKUI_HAXEON_MEMORY_CONTRACT
                "${CMAKE_CURRENT_BINARY_DIR}/nativekit_haxeon_memory_contract.json")
            configure_file(
                "${CMAKE_CURRENT_SOURCE_DIR}/cmake/wasm_memory_contract.json.in"
                "${NKUI_HAXEON_MEMORY_CONTRACT}" @ONLY)
            set(NKUI_HAXEON_MEMORY_CONTRACT_HEADER
                "${CMAKE_CURRENT_BINARY_DIR}/nativekit_haxeon_memory_contract.h")
            configure_file(
                "${CMAKE_CURRENT_SOURCE_DIR}/cmake/wasm_memory_contract.h.in"
                "${NKUI_HAXEON_MEMORY_CONTRACT_HEADER}" @ONLY)
            string(REPLACE "-" "_" NKUI_HAXEON_TARGET_FILE "${NKUI_HAXEON_TARGET}")
            set(NKUI_HAXEON_GUEST_WASM
                "${CMAKE_CURRENT_BINARY_DIR}/nativekit_ui_showcase_${NKUI_HAXEON_TARGET_FILE}.wasm")
            set(NKUI_HAXEON_MEMORY_STATS_VALUE 0)
            if(NKUI_HAXEON_MEMORY_STATS)
                set(NKUI_HAXEON_MEMORY_STATS_VALUE 1)
            endif()
            set(NKUI_HAXEON_BUNDLE_FONTS_VALUE 0)
            if(NKUI_HAXEON_BUNDLE_FONTS)
                set(NKUI_HAXEON_BUNDLE_FONTS_VALUE 1)
            endif()
            add_custom_command(OUTPUT "${NKUI_HAXEON_GUEST_WASM}"
                COMMAND ${CMAKE_COMMAND} -E env
                    "NATIVEKIT_WASM_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}"
                    "HAXEON_DIR=${NKUI_HAXEON_DIR}"
                    "NATIVEKIT_HAXE_BIN=${NKUI_HAXEON_HAXE_BIN}"
                    "NATIVEKIT_HAXEON_TARGET=${NKUI_HAXEON_TARGET}"
                    "NATIVEKIT_HAXEON_EXCEPTION_MODE=${NKUI_HAXEON_EXCEPTION_MODE}"
                    "NATIVEKIT_HAXEON_MEMORY_CONTRACT=${NKUI_HAXEON_MEMORY_CONTRACT}"
                    "NATIVEKIT_HAXEON_MEMORY_STATS=${NKUI_HAXEON_MEMORY_STATS_VALUE}"
                    "NATIVEKIT_HAXEON_BUNDLE_FONTS=${NKUI_HAXEON_BUNDLE_FONTS_VALUE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/showcase-wasm.sh"
                DEPENDS
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/showcase-wasm.sh"
                    "${NKUI_HAXEON_MEMORY_CONTRACT}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/bindings/nativekit-ui-wasm.hxi"
                    "${CMAKE_CURRENT_SOURCE_DIR}/bindings/nativekit-ui-showcase-wasm.hxi"
                    "${CMAKE_CURRENT_SOURCE_DIR}/bindings/nativekit-ui-showcase.hxmap"
                    "${PROJECT_SOURCE_DIR}/bindings/haxe/nativekit-wasm.hxi"
                    "${CMAKE_CURRENT_SOURCE_DIR}/bindings/nativekit-ui.hxmap"
                    "${PROJECT_SOURCE_DIR}/bindings/haxe/nativekit.hxmap"
                    ${NKUI_HAXEON_GUEST_HX_DEPS}
                    ${NKUI_HAXEON_DEPS}
                WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
                USES_TERMINAL)
            add_custom_target(nativekit_ui_haxeon_guest
                DEPENDS "${NKUI_HAXEON_GUEST_WASM}")

            add_executable(nativekit_ui_haxeon
                examples/ui_haxeon/host.cpp
                src/platform/wasm/haxeon_memory_contract.cpp)
            target_include_directories(nativekit_ui_haxeon PRIVATE
                "${CMAKE_CURRENT_BINARY_DIR}")
            target_link_libraries(nativekit_ui_haxeon PRIVATE NativeKit::ui NativeKit::nativekit
                NativeKit::wasm_host_allocator)
            add_dependencies(nativekit_ui_haxeon nativekit_ui_haxeon_guest)
            set(NKUI_HAXEON_FONT_DIR "${NK_VENDOR_DIR}/skribidi/example/data")
            set(NKUI_HAXEON_FONT_PATHS
                "${NKUI_HAXEON_FONT_DIR}/IBMPlexSans-Regular.ttf"
                "${NKUI_HAXEON_FONT_DIR}/IBMPlexSansArabic-Regular.ttf"
                "${NKUI_HAXEON_FONT_DIR}/IBMPlexSansHebrew-Regular.ttf"
                "${NKUI_HAXEON_FONT_DIR}/IBMPlexSansJP-Regular.ttf"
                "${NKUI_HAXEON_FONT_DIR}/NotoEmoji-Regular.ttf")
            if(NKUI_HAXEON_SUBSET_FONTS)
                find_package(Python3 COMPONENTS Interpreter REQUIRED)
                set(NKUI_HAXEON_FONT_ASSET_DIR
                    "${CMAKE_CURRENT_BINARY_DIR}/haxeon-fonts")
                set(NKUI_HAXEON_FONT_ASSET_PATHS)
                foreach(NKUI_HAXEON_FONT_PATH IN LISTS NKUI_HAXEON_FONT_PATHS)
                    get_filename_component(NKUI_HAXEON_FONT_NAME
                        "${NKUI_HAXEON_FONT_PATH}" NAME)
                    list(APPEND NKUI_HAXEON_FONT_ASSET_PATHS
                        "${NKUI_HAXEON_FONT_ASSET_DIR}/${NKUI_HAXEON_FONT_NAME}")
                endforeach()
                add_custom_command(OUTPUT ${NKUI_HAXEON_FONT_ASSET_PATHS}
                    COMMAND ${CMAKE_COMMAND} -E make_directory
                        "${NKUI_HAXEON_FONT_ASSET_DIR}"
                    COMMAND ${Python3_EXECUTABLE}
                        "${CMAKE_CURRENT_SOURCE_DIR}/tools/subset-showcase-fonts.py"
                        --source-dir "${NKUI_HAXEON_FONT_DIR}"
                        --showcase-dir "${CMAKE_CURRENT_SOURCE_DIR}/examples/ui_showcase"
                        --output-dir "${NKUI_HAXEON_FONT_ASSET_DIR}"
                    DEPENDS
                        "${CMAKE_CURRENT_SOURCE_DIR}/tools/subset-showcase-fonts.py"
                        ${NKUI_HAXEON_FONT_PATHS}
                        ${NKUI_HAXEON_GUEST_HX_DEPS}
                    COMMENT "Subsetting Haxeon showcase fonts"
                    VERBATIM)
                add_custom_target(nativekit_ui_haxeon_fonts
                    DEPENDS ${NKUI_HAXEON_FONT_ASSET_PATHS})
            else()
                set(NKUI_HAXEON_FONT_ASSET_DIR "${NKUI_HAXEON_FONT_DIR}")
                set(NKUI_HAXEON_FONT_ASSET_PATHS ${NKUI_HAXEON_FONT_PATHS})
                add_custom_target(nativekit_ui_haxeon_fonts)
            endif()
            add_dependencies(nativekit_ui_haxeon nativekit_ui_haxeon_fonts)
            set(NKUI_HAXEON_EXPORTED_FUNCTIONS
                _main
                _nk_api_version
                _nk_clipboard_read_text
                _nk_clipboard_set_text
                _nk_cursor_create_custom
                _nk_cursor_create_standard
                _nk_cursor_destroy
                _nk_event_release
                _nk_get_capabilities
                _nk_init
                _nk_last_error
                _nk_poll_event
                _nk_resource_load_async
                _nk_shutdown
                _nk_time_seconds
                _nk_surface_accessibility_clear
                _nk_surface_accessibility_remove_node
                _nk_surface_accessibility_set_focus
                _nk_surface_accessibility_set_node
                _nk_surface_accessibility_update_with_removed_ids
                _nk_surface_create
                _nk_surface_destroy
                _nk_surface_get_framebuffer_size
                _nk_surface_make_current
                _nk_surface_present
                _nk_surface_set_bounds
                _nk_surface_set_text_input_active
                _nk_surface_set_text_input_state
                _nk_window_create
                _nk_window_destroy
                _nk_window_get_scale
                _nk_window_get_cursor_mode
                _nk_window_set_cursor
                _nk_window_set_cursor_mode
                _nk_window_set_decoration_regions
                _nk_webview_create
                _nk_webview_destroy
                _nk_webview_set_bounds
                _nk_webview_set_html
                _nk_webview_show
                _nkui_display_list_create
                _nkui_display_list_destroy
                _nkui_display_list_get_info
                _nkui_display_list_reset
                _nkui_display_list_submit
                _nkui_font_collection_add
                _nkui_font_collection_add_data
                _nkui_font_collection_add_system_fallbacks
                _nkui_font_collection_create
                _nkui_image_create
                _nkui_image_create_filtered
                _nkui_image_load_file
                _nkui_layout_session_clear_custom_paints
                _nkui_layout_session_create
                _nkui_layout_session_destroy
                _nkui_layout_session_get_resolved_items
                _nkui_layout_session_render_frame
                _nkui_layout_session_set_custom_paint
                _nkui_layout_session_set_font_collection
                _nkui_layout_session_set_measure_callback
                _nkui_layout_session_submit
                _nkui_paint_create_linear_gradient
                _nkui_paint_create_solid
                _nkui_path_create
                _nkui_renderer_create
                _nkui_renderer_destroy
                _nkui_renderer_get_stats
                _nkui_renderer_render
                _nkui_renderer_render_frame
                _nkui_resource_destroy
                _nkui_text_layout_align_grapheme
                _nkui_text_layout_caret
                _nkui_text_layout_create
                _nkui_text_layout_create_styled
                _nkui_text_layout_get_selection_rects
                _nkui_text_layout_hit_test
                _nkui_text_layout_line_range_at
                _nkui_text_layout_measure
                _nkui_text_layout_move_paragraph
                _nkui_text_layout_move_word
                _nkui_text_layout_next_grapheme
                _nkui_text_layout_position_offset
                _nkui_text_layout_previous_grapheme
                _nkui_text_layout_set_text
                _nkui_text_layout_update
                _nkui_text_layout_word_range_at
                _nkui_text_layout_word_range
                _nkui_showcase_cube_create
                _nkui_showcase_cube_set_rotation
                _nkui_showcase_diagnostic_report
                _nkui_showcase_diagnostic_message
                _nkui_showcase_diagnostic_stack
                _nkui_showcase_diagnostic_breadcrumbs
                _nkgpu_last_error
                _nkui_haxeon_memory_contract_status
                _nkui_haxeon_memory_contract_version
                _nkui_haxeon_memory_contract_page_size
                _nkui_haxeon_memory_contract_host_base
                _nkui_haxeon_memory_contract_host_limit
                _nkui_haxeon_memory_contract_base
                _nkui_haxeon_memory_contract_guest_base
                _nkui_haxeon_memory_contract_guest_limit
                _nkui_haxeon_memory_contract_memory_size
                _nkui_haxeon_memory_contract_limit)
            list(JOIN NKUI_HAXEON_EXPORTED_FUNCTIONS ","
                NKUI_HAXEON_EXPORTED_FUNCTIONS_CSV)
            target_link_options(nativekit_ui_haxeon PRIVATE
                "-sINITIAL_MEMORY=${NKUI_HAXEON_MEMORY_LIMIT}"
                "-sALLOW_MEMORY_GROWTH=0"
                "-sEXPORTED_FUNCTIONS=[${NKUI_HAXEON_EXPORTED_FUNCTIONS_CSV}]"
                "-sNO_EXIT_RUNTIME=1")
            if(NKUI_HAXEON_BUNDLE_FONTS)
                foreach(NKUI_HAXEON_FONT_PATH IN LISTS NKUI_HAXEON_FONT_ASSET_PATHS)
                    get_filename_component(NKUI_HAXEON_FONT_NAME
                        "${NKUI_HAXEON_FONT_PATH}" NAME)
                    target_link_options(nativekit_ui_haxeon PRIVATE
                        "--preload-file=${NKUI_HAXEON_FONT_PATH}@/assets/${NKUI_HAXEON_FONT_NAME}")
                endforeach()
            endif()
            set(NKUI_HAXEON_ASSET_COMMANDS
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${NKUI_HAXEON_GUEST_WASM}"
                    "$<TARGET_FILE_DIR:nativekit_ui_haxeon>/nativekit_ui_haxeon_guest.wasm"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CMAKE_CURRENT_SOURCE_DIR}/examples/ui_haxeon/web_shell.html"
                    "$<TARGET_FILE_DIR:nativekit_ui_haxeon>/nativekit_ui_haxeon.html")
            if(NOT NKUI_HAXEON_BUNDLE_FONTS)
                list(APPEND NKUI_HAXEON_ASSET_COMMANDS
                    COMMAND ${CMAKE_COMMAND} -E rm -f
                        "$<TARGET_FILE_DIR:nativekit_ui_haxeon>/nativekit_ui_haxeon.data"
                    COMMAND ${CMAKE_COMMAND} -E make_directory
                        "$<TARGET_FILE_DIR:nativekit_ui_haxeon>/assets"
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${NKUI_HAXEON_FONT_ASSET_DIR}/IBMPlexSans-Regular.ttf"
                        "$<TARGET_FILE_DIR:nativekit_ui_haxeon>/assets/IBMPlexSans-Regular.ttf"
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${NKUI_HAXEON_FONT_ASSET_DIR}/IBMPlexSansArabic-Regular.ttf"
                        "$<TARGET_FILE_DIR:nativekit_ui_haxeon>/assets/IBMPlexSansArabic-Regular.ttf"
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${NKUI_HAXEON_FONT_ASSET_DIR}/IBMPlexSansHebrew-Regular.ttf"
                        "$<TARGET_FILE_DIR:nativekit_ui_haxeon>/assets/IBMPlexSansHebrew-Regular.ttf"
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${NKUI_HAXEON_FONT_ASSET_DIR}/IBMPlexSansJP-Regular.ttf"
                        "$<TARGET_FILE_DIR:nativekit_ui_haxeon>/assets/IBMPlexSansJP-Regular.ttf"
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${NKUI_HAXEON_FONT_ASSET_DIR}/NotoEmoji-Regular.ttf"
                        "$<TARGET_FILE_DIR:nativekit_ui_haxeon>/assets/NotoEmoji-Regular.ttf")
            endif()
            add_custom_target(nativekit_ui_haxeon_assets
                ${NKUI_HAXEON_ASSET_COMMANDS}
                DEPENDS
                    nativekit_ui_haxeon_guest
                    nativekit_ui_haxeon_fonts
                    ${NKUI_HAXEON_FONT_ASSET_PATHS}
                    "${CMAKE_CURRENT_SOURCE_DIR}/examples/ui_haxeon/web_shell.html"
                COMMENT "Copying NativeKit Haxeon browser host")
            add_dependencies(nativekit_ui_haxeon nativekit_ui_haxeon_assets)
        endif()
    endif()
endif()

if(NOT EMSCRIPTEN)
    install(TARGETS nativekit_ui EXPORT NativeKitTargets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()
if(NOT EMSCRIPTEN AND NK_LIBRARY_TYPE STREQUAL "STATIC")
    set_target_properties(harfbuzz PROPERTIES EXPORT_NAME ui_harfbuzz)
    set_target_properties(nkui_sheenbidi PROPERTIES EXPORT_NAME ui_sheenbidi)
    set_target_properties(nkui_libunibreak PROPERTIES EXPORT_NAME ui_libunibreak)
    if(TARGET nkui_budouxc)
        set_target_properties(nkui_budouxc PROPERTIES EXPORT_NAME ui_budouxc)
    endif()
    set_target_properties(nkui_skribidi PROPERTIES EXPORT_NAME ui_skribidi)
    set_target_properties(nkui_nanovg_core PROPERTIES EXPORT_NAME ui_nanovg_core)
    set(NKUI_INSTALL_TARGETS
        harfbuzz nkui_sheenbidi nkui_libunibreak nkui_skribidi nkui_nanovg_core)
    if(TARGET nkui_budouxc)
        list(APPEND NKUI_INSTALL_TARGETS nkui_budouxc)
    endif()
    install(TARGETS ${NKUI_INSTALL_TARGETS}
        EXPORT NativeKitTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES
    "${NK_VENDOR_DIR}/skribidi/LICENSE"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/NativeKit/UI/Skribidi)
install(FILES
    "${NK_VENDOR_DIR}/nanovg/LICENSE.txt"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/NativeKit/UI/NanoVG)
install(FILES "${NK_VENDOR_DIR}/harfbuzz/COPYING"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/NativeKit/UI/HarfBuzz)
install(FILES "${NK_VENDOR_DIR}/sheenbidi/LICENSE"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/NativeKit/UI/SheenBidi)
install(FILES "${NK_VENDOR_DIR}/libunibreak/LICENCE"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/NativeKit/UI/libunibreak)
install(FILES "${NK_VENDOR_DIR}/budouxc/LICENSE"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/NativeKit/UI/BudouX-C)
