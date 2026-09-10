include(cmake/SkribidiSubmodules.cmake)
include(cmake/NanoVGSubmodules.cmake)

add_library(nativekit_ui SHARED
    src/api.cpp
    src/compositor/compositor.cpp
    src/display_list/display_list.cpp
    src/prepare/nanovg_recorder.cpp
    src/prepare/skribidi_adapter.cpp
    src/render/sokol_backend.cpp)
add_library(NativeKit::ui ALIAS nativekit_ui)
target_compile_features(nativekit_ui PRIVATE cxx_std_17)
target_include_directories(nativekit_ui
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE src)
target_include_directories(nativekit_ui PRIVATE "${NK_VENDOR_DIR}/sokol")
target_link_libraries(nativekit_ui PRIVATE NativeKit::nativekit nkui_skribidi nkui_nanovg_core
    nativekit_sokol_runtime)
target_compile_definitions(nativekit_ui PRIVATE NKUI_BUILDING_LIBRARY)
set_target_properties(nativekit_ui PROPERTIES
    EXPORT_NAME ui
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    VERSION ${PROJECT_VERSION}
    SOVERSION 0)

if(NK_BUILD_TESTS)
    add_executable(nativekit_ui_abi_test tests/abi.c)
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
    add_executable(nativekit_ui_skribidi_adapter_test
        tests/skribidi_adapter.cpp
        src/prepare/skribidi_adapter.cpp)
    target_include_directories(nativekit_ui_skribidi_adapter_test PRIVATE src)
    target_compile_definitions(nativekit_ui_skribidi_adapter_test PRIVATE
        NKUI_TEST_FONT_PATH="${NK_VENDOR_DIR}/skribidi/example/data/IBMPlexSans-Regular.ttf")
    target_link_libraries(nativekit_ui_skribidi_adapter_test PRIVATE nkui_skribidi)
    add_test(NAME nativekit_ui_skribidi_adapter COMMAND nativekit_ui_skribidi_adapter_test)
    add_executable(nativekit_ui_compositor_test
        tests/compositor.cpp
        src/compositor/compositor.cpp
        src/display_list/display_list.cpp)
    target_include_directories(nativekit_ui_compositor_test PRIVATE src)
    add_test(NAME nativekit_ui_compositor COMMAND nativekit_ui_compositor_test)
    add_executable(nativekit_ui_sokol_backend_mesh_test
        tests/sokol_backend_mesh.cpp
        src/prepare/nanovg_recorder.cpp
        src/prepare/skribidi_adapter.cpp
        src/render/sokol_backend.cpp)
    target_include_directories(nativekit_ui_sokol_backend_mesh_test PRIVATE
        src "${NK_VENDOR_DIR}/sokol")
    target_link_libraries(nativekit_ui_sokol_backend_mesh_test PRIVATE
        nkui_nanovg_core nkui_skribidi nativekit_sokol_runtime)
    add_test(NAME nativekit_ui_sokol_backend_mesh COMMAND nativekit_ui_sokol_backend_mesh_test)
endif()

if(NK_BUILD_EXAMPLES AND TARGET nkui_nanovg)
    add_executable(nativekit_ui_skribidi_nanovg examples/skribidi_nanovg.c)
    target_include_directories(nativekit_ui_skribidi_nanovg PRIVATE
        "${NK_VENDOR_DIR}/nanovg-sokol/src/nanovg_sokol"
        "${NK_VENDOR_DIR}/sokol")
    target_compile_definitions(nativekit_ui_skribidi_nanovg PRIVATE
        NKUI_TEST_FONT_PATH="${NK_VENDOR_DIR}/skribidi/example/data/IBMPlexSans-Regular.ttf")
    target_link_libraries(nativekit_ui_skribidi_nanovg PRIVATE
        NativeKit::nativekit nkui_skribidi nkui_nanovg OpenGL::GL)
    set_target_properties(nativekit_ui_skribidi_nanovg PROPERTIES LINKER_LANGUAGE CXX)
    if(NK_BUILD_TESTS)
        find_program(NKUI_XVFB_RUN xvfb-run)
        if(NKUI_XVFB_RUN)
            add_test(NAME nativekit_ui_skribidi_nanovg_smoke
                COMMAND ${NKUI_XVFB_RUN} -a
                    $<TARGET_FILE:nativekit_ui_skribidi_nanovg> --smoke-test)
            set_tests_properties(nativekit_ui_skribidi_nanovg_smoke PROPERTIES
                LABELS "graphics;ui"
                TIMEOUT 15
                ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1")
        endif()
    endif()
endif()

install(TARGETS nativekit_ui EXPORT NativeKitTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES
    "${NK_VENDOR_DIR}/skribidi/LICENSE"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/NativeKit/UI/Skribidi)
install(FILES
    "${NK_VENDOR_DIR}/nanovg/LICENSE.txt"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/NativeKit/UI/NanoVG)
install(FILES
    "${NK_VENDOR_DIR}/nanovg-sokol/LICENSE"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/NativeKit/UI/NanoVG-Sokol)
