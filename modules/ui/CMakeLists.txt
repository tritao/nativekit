include(cmake/SkribidiSubmodules.cmake)
include(cmake/NanoVGSubmodules.cmake)
include(cmake/SokolShaders.cmake)

nkui_generate_shader(
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/toolchain_probe.glsl"
    ui_toolchain_probe.glsl.h
    NKUI_TOOLCHAIN_PROBE_SHADER)
nkui_generate_shader(
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/path.glsl"
    nkui_path.glsl.h
    NKUI_PATH_SHADER)
nkui_generate_shader(
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/solid.glsl"
    nkui_solid.glsl.h
    NKUI_SOLID_SHADER)
nkui_generate_shader(
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/text.glsl"
    nkui_text.glsl.h
    NKUI_TEXT_SHADER)
nkui_generate_shader(
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/composite.glsl"
    nkui_composite.glsl.h
    NKUI_COMPOSITE_SHADER)

add_library(nativekit_ui ${NK_LIBRARY_TYPE}
    src/api.cpp
    src/compositor/compositor.cpp
    src/compositor/render_plan.cpp
    src/display_list/display_list.cpp
    src/layout/layout_engine.cpp
    src/layout/layout_render_compiler.cpp
    src/prepare/nanovg_path.cpp
    src/prepare/skribidi_adapter.cpp
    src/prepare/system_fonts.cpp
    src/render/frame_resources.cpp
    src/render/graphics_device.cpp
    src/render/render_backend_factory.cpp
    src/render/render_plan_executor.cpp
    src/render/shader_toolchain_probe.cpp
    src/render/sokol_backend.cpp
    "${NKUI_TOOLCHAIN_PROBE_SHADER}"
    "${NKUI_PATH_SHADER}"
    "${NKUI_SOLID_SHADER}"
    "${NKUI_TEXT_SHADER}"
    "${NKUI_COMPOSITE_SHADER}")
add_library(NativeKit::ui ALIAS nativekit_ui)
target_compile_features(nativekit_ui PRIVATE cxx_std_20)
target_include_directories(nativekit_ui
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE src)
target_include_directories(nativekit_ui PRIVATE "${NK_VENDOR_DIR}/sokol")
target_include_directories(nativekit_ui PRIVATE "${NK_VENDOR_DIR}/clay")
target_include_directories(nativekit_ui PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../sokol/src")
target_include_directories(nativekit_ui PRIVATE "${NKUI_SHADER_GENERATED_DIR}")
target_link_libraries(nativekit_ui PRIVATE NativeKit::nativekit nkui_skribidi nkui_nanovg_core
    nativekit_sokol_runtime ${NK_SOKOL_GRAPHICS_LIBRARIES})
if(UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(FONTCONFIG QUIET IMPORTED_TARGET fontconfig)
    endif()
    if(TARGET PkgConfig::FONTCONFIG)
        target_link_libraries(nativekit_ui PRIVATE PkgConfig::FONTCONFIG)
        target_compile_definitions(nativekit_ui PRIVATE NKUI_HAS_FONTCONFIG=1)
    endif()
elseif(APPLE)
    target_link_libraries(nativekit_ui PRIVATE "-framework CoreText" "-framework CoreFoundation")
endif()
if(NK_BUILD_SOKOL_BACKEND_MATRIX)
    target_link_libraries(nativekit_ui PRIVATE
        nativekit_sokol_runtime_glcore nativekit_sokol_runtime_gles3)
    target_compile_definitions(nativekit_ui PRIVATE NKUI_SOKOL_RUNTIME_MATRIX=1)
endif()
target_compile_definitions(nativekit_ui PUBLIC ${NK_SOKOL_COMPILE_DEFINITION}
    PRIVATE NKUI_BUILDING_LIBRARY)
set_target_properties(nativekit_ui PROPERTIES
    EXPORT_NAME ui
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    VERSION ${PROJECT_VERSION}
    SOVERSION 0)

if(NK_BUILD_TESTS)
    add_test(NAME nativekit_ui_dependencies
        COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tools/check-dependencies.sh")
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
    add_executable(nativekit_ui_sokol_backend_mesh_test
        tests/sokol_backend_mesh.cpp
        src/display_list/display_list.cpp
        src/prepare/nanovg_recorder.cpp
        src/prepare/nanovg_path.cpp
        src/prepare/skribidi_adapter.cpp
        src/prepare/system_fonts.cpp
        src/render/graphics_device.cpp
        src/render/sokol_backend.cpp)
    target_include_directories(nativekit_ui_sokol_backend_mesh_test PRIVATE
        src "${PROJECT_SOURCE_DIR}/include" "${NK_VENDOR_DIR}/sokol" "${NKUI_SHADER_GENERATED_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../sokol/src" ${NK_SOKOL_GRAPHICS_INCLUDE_DIRS})
    target_link_libraries(nativekit_ui_sokol_backend_mesh_test PRIVATE
        nkui_nanovg_core nkui_skribidi nativekit_sokol_runtime)
    add_test(NAME nativekit_ui_sokol_backend_mesh COMMAND nativekit_ui_sokol_backend_mesh_test)
    add_executable(nativekit_ui_frame_resources_test
        tests/frame_resources.cpp
        src/display_list/display_list.cpp
        src/prepare/nanovg_recorder.cpp
        src/prepare/nanovg_path.cpp
        src/render/frame_resources.cpp)
    target_include_directories(nativekit_ui_frame_resources_test PRIVATE src)
    target_link_libraries(nativekit_ui_frame_resources_test PRIVATE nkui_nanovg_core)
    add_test(NAME nativekit_ui_frame_resources COMMAND nativekit_ui_frame_resources_test)
    add_executable(nativekit_ui_layout_engine_test
        tests/layout_engine.cpp
        src/layout/layout_engine.cpp
        src/prepare/skribidi_adapter.cpp
        src/prepare/system_fonts.cpp)
    target_compile_features(nativekit_ui_layout_engine_test PRIVATE cxx_std_20)
    target_include_directories(nativekit_ui_layout_engine_test PRIVATE
        src "${NK_VENDOR_DIR}/clay")
    target_compile_definitions(nativekit_ui_layout_engine_test PRIVATE
        NKUI_TEST_FONT_PATH="${NK_VENDOR_DIR}/skribidi/example/data/IBMPlexSans-Regular.ttf")
    target_link_libraries(nativekit_ui_layout_engine_test PRIVATE nkui_skribidi)
    add_test(NAME nativekit_ui_layout_engine COMMAND nativekit_ui_layout_engine_test)
    add_executable(nativekit_ui_layout_render_compiler_test
        tests/layout_render_compiler.cpp
        src/layout/layout_engine.cpp
        src/layout/layout_render_compiler.cpp
        src/display_list/display_list.cpp
        src/prepare/nanovg_path.cpp
        src/prepare/skribidi_adapter.cpp
        src/prepare/system_fonts.cpp
        src/render/frame_resources.cpp
        src/render/render_plan_executor.cpp
        src/compositor/render_plan.cpp)
    target_compile_features(nativekit_ui_layout_render_compiler_test PRIVATE cxx_std_20)
    target_include_directories(nativekit_ui_layout_render_compiler_test PRIVATE
        src "${PROJECT_SOURCE_DIR}/include" "${NK_VENDOR_DIR}/clay")
    target_compile_definitions(nativekit_ui_layout_render_compiler_test PRIVATE
        NKUI_TEST_FONT_PATH="${NK_VENDOR_DIR}/skribidi/example/data/IBMPlexSans-Regular.ttf")
    target_link_libraries(nativekit_ui_layout_render_compiler_test PRIVATE
        nkui_nanovg_core nkui_skribidi)
    add_test(NAME nativekit_ui_layout_render_compiler
        COMMAND nativekit_ui_layout_render_compiler_test)
    if(NK_BUILD_EXAMPLES)
        add_executable(nativekit_ui_sokol_backend_smoke_test
            tests/sokol_backend_smoke.cpp
            src/compositor/compositor.cpp
            src/compositor/render_plan.cpp
            src/display_list/display_list.cpp
            src/prepare/nanovg_recorder.cpp
            src/prepare/nanovg_path.cpp
            src/prepare/skribidi_adapter.cpp
            src/prepare/system_fonts.cpp
            src/render/graphics_device.cpp
            src/render/frame_resources.cpp
            src/render/render_plan_executor.cpp
            src/render/sokol_backend.cpp)
        target_include_directories(nativekit_ui_sokol_backend_smoke_test PRIVATE
            src "${NK_VENDOR_DIR}/sokol" "${NKUI_SHADER_GENERATED_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/../sokol/src")
        target_compile_definitions(nativekit_ui_sokol_backend_smoke_test PRIVATE
            NKUI_TEST_FONT_PATH="${NK_VENDOR_DIR}/skribidi/example/data/IBMPlexSans-Regular.ttf"
            NKUI_TEST_COLOR_FONT_PATH="${NK_VENDOR_DIR}/skribidi/example/data/NotoColorEmoji-Regular.ttf")
        target_link_libraries(nativekit_ui_sokol_backend_smoke_test PRIVATE
            NativeKit::nativekit nkui_nanovg_core nkui_skribidi nativekit_sokol_runtime
            ${NK_SOKOL_GRAPHICS_LIBRARIES})
        target_compile_definitions(nativekit_ui_sokol_backend_smoke_test PRIVATE
            ${NK_SOKOL_COMPILE_DEFINITION})
        if(TARGET NativeKit::sokol)
            target_compile_definitions(nativekit_ui_sokol_backend_smoke_test PRIVATE
                NKUI_TEST_PUBLIC_SOKOL_RUNTIME)
            target_link_libraries(nativekit_ui_sokol_backend_smoke_test PRIVATE
                NativeKit::sokol)
        endif()
        add_executable(nativekit_ui_public_renderer_smoke_test tests/public_renderer_smoke.cpp)
        target_compile_definitions(nativekit_ui_public_renderer_smoke_test PRIVATE
            NKUI_TEST_FONT_PATH="${NK_VENDOR_DIR}/skribidi/example/data/IBMPlexSans-Regular.ttf")
        target_link_libraries(nativekit_ui_public_renderer_smoke_test PRIVATE
            NativeKit::ui NativeKit::nativekit ${NK_SOKOL_GRAPHICS_LIBRARIES})
        target_compile_definitions(nativekit_ui_public_renderer_smoke_test PRIVATE
            ${NK_SOKOL_COMPILE_DEFINITION})
        find_program(NKUI_BACKEND_XVFB_RUN xvfb-run)
        if(NKUI_BACKEND_XVFB_RUN)
            add_test(NAME nativekit_ui_sokol_backend_smoke
                COMMAND ${NKUI_BACKEND_XVFB_RUN} -a
                    $<TARGET_FILE:nativekit_ui_sokol_backend_smoke_test>)
            set_tests_properties(nativekit_ui_sokol_backend_smoke PROPERTIES
                LABELS "graphics;ui"
                TIMEOUT 15
                ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1")
            add_test(NAME nativekit_ui_public_renderer_smoke
                COMMAND ${NKUI_BACKEND_XVFB_RUN} -a
                    $<TARGET_FILE:nativekit_ui_public_renderer_smoke_test>)
            set_tests_properties(nativekit_ui_public_renderer_smoke PROPERTIES
                LABELS "graphics;ui"
                TIMEOUT 15
                ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1")
        endif()
    endif()
endif()

if(NK_BUILD_EXAMPLES)
    add_executable(nativekit_ui_c_api examples/ui_c_api/showcase.cpp)
    set(NKUI_C_API_FONT_PATH
        "${NK_VENDOR_DIR}/skribidi/example/data/IBMPlexSans-Regular.ttf")
    target_compile_definitions(nativekit_ui_c_api PRIVATE
        NKUI_TEST_FONT_PATH="${NKUI_C_API_FONT_PATH}")
    target_link_libraries(nativekit_ui_c_api PRIVATE NativeKit::ui NativeKit::nativekit)
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

    set(NKUI_HAXEON_DIR "$ENV{HAXEON_DIR}")
    if(NOT NKUI_HAXEON_DIR)
        set(NKUI_HAXEON_DIR "${PROJECT_SOURCE_DIR}/../realtime-haxe")
    endif()
    if(EXISTS "${NKUI_HAXEON_DIR}/.tools/haxe/haxe")
        add_custom_target(nativekit_ui_showcase
            COMMAND ${CMAKE_COMMAND} -E env
                "NATIVEKIT_BUILD_DIR=${CMAKE_BINARY_DIR}"
                "HAXEON_DIR=${NKUI_HAXEON_DIR}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/showcase.sh" --build-only
            DEPENDS nativekit_ui
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
            USES_TERMINAL)

        if(NK_BUILD_TESTS)
            find_program(NKUI_SHOWCASE_XVFB_RUN xvfb-run)
            if(NKUI_SHOWCASE_XVFB_RUN)
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
    endif()
    if(NK_BUILD_SOKOL_BACKEND_MATRIX AND NK_BUILD_TESTS)
        add_executable(nativekit_ui_sokol_backend_matrix_smoke_test
            tests/sokol_backend_matrix_smoke.cpp
            src/display_list/display_list.cpp
            src/prepare/nanovg_recorder.cpp
            src/prepare/nanovg_path.cpp
            src/prepare/skribidi_adapter.cpp
            src/prepare/system_fonts.cpp
            src/render/graphics_device.cpp
            src/render/render_backend_factory.cpp
            src/render/sokol_backend.cpp
            "${NKUI_TOOLCHAIN_PROBE_SHADER}"
            "${NKUI_PATH_SHADER}"
            "${NKUI_SOLID_SHADER}"
            "${NKUI_TEXT_SHADER}"
            "${NKUI_COMPOSITE_SHADER}")
        target_include_directories(nativekit_ui_sokol_backend_matrix_smoke_test PRIVATE
            src "${PROJECT_SOURCE_DIR}/include" "${NK_VENDOR_DIR}/sokol"
            "${NKUI_SHADER_GENERATED_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../sokol/src"
            ${NK_SOKOL_GRAPHICS_INCLUDE_DIRS})
        target_compile_definitions(nativekit_ui_sokol_backend_matrix_smoke_test PRIVATE
            NKUI_SOKOL_RUNTIME_MATRIX=1)
        target_link_libraries(nativekit_ui_sokol_backend_matrix_smoke_test PRIVATE
            NativeKit::nativekit nkui_nanovg_core nkui_skribidi
            nativekit_sokol_runtime_glcore nativekit_sokol_runtime_gles3
            OpenGL::GL "${NK_SOKOL_MATRIX_GLES3_LIBRARY}")
        find_program(NKUI_MATRIX_XVFB_RUN xvfb-run)
        if(NKUI_MATRIX_XVFB_RUN)
            add_test(NAME nativekit_ui_sokol_backend_matrix_smoke
                COMMAND ${NKUI_MATRIX_XVFB_RUN} -a
                    $<TARGET_FILE:nativekit_ui_sokol_backend_matrix_smoke_test>)
            set_tests_properties(nativekit_ui_sokol_backend_matrix_smoke PROPERTIES
                LABELS "graphics;ui;sokol;matrix"
                TIMEOUT 15
                ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1")
        endif()
    endif()

endif()

if(NOT EMSCRIPTEN)
    install(TARGETS nativekit_ui EXPORT NativeKitTargets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES
    "${NK_VENDOR_DIR}/skribidi/LICENSE"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/NativeKit/UI/Skribidi)
install(FILES
    "${NK_VENDOR_DIR}/nanovg/LICENSE.txt"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/NativeKit/UI/NanoVG)
install(FILES "${NK_VENDOR_DIR}/sokol/LICENSE"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/NativeKit/UI/Sokol)
install(FILES "${NK_VENDOR_DIR}/harfbuzz/COPYING"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/NativeKit/UI/HarfBuzz)
install(FILES "${NK_VENDOR_DIR}/sheenbidi/LICENSE"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/NativeKit/UI/SheenBidi)
install(FILES "${NK_VENDOR_DIR}/libunibreak/LICENCE"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/NativeKit/UI/libunibreak)
install(FILES "${NK_VENDOR_DIR}/budouxc/LICENSE"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/NativeKit/UI/BudouX-C)
