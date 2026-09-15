add_executable(nativekit_c_abi abi/c_api.c)
target_compile_features(nativekit_c_abi PRIVATE c_std_11)
target_link_libraries(nativekit_c_abi PRIVATE NativeKit::nativekit)
set_target_properties(nativekit_c_abi PROPERTIES LINKER_LANGUAGE CXX)
add_test(NAME c_abi COMMAND nativekit_c_abi)

add_executable(nativekit_core_tests
    unit/core_tests.cpp
    ${PROJECT_SOURCE_DIR}/src/core/error.cpp
    ${PROJECT_SOURCE_DIR}/src/core/event_queue.cpp
    ${PROJECT_SOURCE_DIR}/src/core/gamepad_mapping.cpp
    ${PROJECT_SOURCE_DIR}/src/core/handle_registry.cpp
)
target_compile_features(nativekit_core_tests PRIVATE cxx_std_17)
target_include_directories(nativekit_core_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(nativekit_core_tests PRIVATE NativeKit::nativekit)
if(MSVC)
    target_compile_options(nativekit_core_tests PRIVATE /EHsc)
endif()
add_test(NAME core COMMAND nativekit_core_tests)

add_executable(nativekit_time_tests unit/time_tests.cpp)
target_compile_features(nativekit_time_tests PRIVATE cxx_std_17)
target_link_libraries(nativekit_time_tests PRIVATE NativeKit::nativekit)
add_test(NAME time COMMAND nativekit_time_tests)

add_executable(nativekit_platform_parity unit/platform_parity.cpp)
target_compile_features(nativekit_platform_parity PRIVATE cxx_std_17)
target_link_libraries(nativekit_platform_parity PRIVATE NativeKit::nativekit)
if(ANDROID)
    target_compile_definitions(nativekit_platform_parity PRIVATE NK_PARITY_BACKEND_ANDROID=1)
elseif(WIN32)
    target_compile_definitions(nativekit_platform_parity PRIVATE NK_PARITY_BACKEND_WINDOWS=1)
elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    target_compile_definitions(nativekit_platform_parity PRIVATE NK_PARITY_BACKEND_IOS=1)
elseif(APPLE)
    target_compile_definitions(nativekit_platform_parity PRIVATE NK_PARITY_BACKEND_MACOS=1)
elseif(EMSCRIPTEN)
    target_compile_definitions(nativekit_platform_parity PRIVATE NK_PARITY_BACKEND_WEB=1)
elseif(NK_BACKEND_GTK_AVAILABLE)
    target_compile_definitions(nativekit_platform_parity PRIVATE NK_PARITY_BACKEND_LINUX=1)
endif()
add_test(NAME platform_parity COMMAND nativekit_platform_parity)

add_executable(nativekit_wasm_host_allocator_test
    unit/wasm_host_allocator.cpp
    ${PROJECT_SOURCE_DIR}/src/platform/wasm/host_allocator.cpp)
target_compile_features(nativekit_wasm_host_allocator_test PRIVATE cxx_std_17)
target_include_directories(nativekit_wasm_host_allocator_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
add_test(NAME wasm_host_allocator COMMAND nativekit_wasm_host_allocator_test)

if(EMSCRIPTEN)
    add_executable(nativekit_web_accessibility integration/web_accessibility.c)
    target_compile_features(nativekit_web_accessibility PRIVATE c_std_11)
    target_link_libraries(nativekit_web_accessibility PRIVATE NativeKit::nativekit)
    set_target_properties(nativekit_web_accessibility PROPERTIES SUFFIX ".html"
        LINKER_LANGUAGE CXX)
endif()

if(NK_BACKEND_GTK_AVAILABLE)
    add_executable(nativekit_linux_joystick_uinput integration/linux_joystick_uinput.c)
    target_link_libraries(nativekit_linux_joystick_uinput PRIVATE NativeKit::nativekit m)
    set_target_properties(nativekit_linux_joystick_uinput PROPERTIES LINKER_LANGUAGE CXX)
    add_test(NAME linux_joystick_uinput COMMAND nativekit_linux_joystick_uinput)
    set_tests_properties(linux_joystick_uinput PROPERTIES SKIP_RETURN_CODE 77 LABELS "linux")

    find_program(XVFB_RUN xvfb-run)
    if(XVFB_RUN)
        add_executable(nativekit_gtk_integration integration/gtk_create_destroy.c)
        target_link_libraries(nativekit_gtk_integration PRIVATE NativeKit::nativekit PkgConfig::GTK3)
        set_target_properties(nativekit_gtk_integration PROPERTIES LINKER_LANGUAGE CXX)
        add_test(NAME gtk_create_destroy
            COMMAND ${XVFB_RUN} -a $<TARGET_FILE:nativekit_gtk_integration>)
        add_executable(nativekit_gtk_surface_frame integration/gtk_surface_frame.c)
        target_link_libraries(nativekit_gtk_surface_frame PRIVATE NativeKit::nativekit)
        set_target_properties(nativekit_gtk_surface_frame PROPERTIES LINKER_LANGUAGE CXX)
        add_test(NAME gtk_surface_frame
            COMMAND ${XVFB_RUN} -a $<TARGET_FILE:nativekit_gtk_surface_frame>)
        add_executable(nativekit_gtk_accessibility integration/gtk_accessibility.c)
        target_link_libraries(nativekit_gtk_accessibility PRIVATE NativeKit::nativekit)
        set_target_properties(nativekit_gtk_accessibility PROPERTIES LINKER_LANGUAGE CXX)
        add_test(NAME gtk_accessibility
            COMMAND ${XVFB_RUN} -a $<TARGET_FILE:nativekit_gtk_accessibility>)
        add_executable(nativekit_gtk_webview_resize integration/gtk_webview_resize.c)
        target_link_libraries(nativekit_gtk_webview_resize PRIVATE NativeKit::nativekit)
        set_target_properties(nativekit_gtk_webview_resize PROPERTIES LINKER_LANGUAGE CXX)
        add_test(NAME gtk_webview_resize
            COMMAND ${XVFB_RUN} -a $<TARGET_FILE:nativekit_gtk_webview_resize>)
    endif()
    find_program(DBUS_RUN_SESSION dbus-run-session)
    if(DBUS_RUN_SESSION)
        add_executable(nativekit_linux_notification_failure
            integration/linux_notification_failure.c)
        target_link_libraries(nativekit_linux_notification_failure
            PRIVATE NativeKit::nativekit)
        set_target_properties(nativekit_linux_notification_failure
            PROPERTIES LINKER_LANGUAGE CXX)
        add_test(NAME linux_notification_failure
            COMMAND ${DBUS_RUN_SESSION} --
                $<TARGET_FILE:nativekit_linux_notification_failure>)
    endif()
endif()

if(WIN32)
    add_executable(nativekit_win_integration integration/win_create_destroy.c)
    target_link_libraries(nativekit_win_integration PRIVATE NativeKit::nativekit)
    set_target_properties(nativekit_win_integration PROPERTIES LINKER_LANGUAGE CXX)
    add_test(NAME win_create_destroy COMMAND nativekit_win_integration)
    set_tests_properties(win_create_destroy PROPERTIES LABELS "windows" TIMEOUT 120)
    add_executable(nativekit_win_surface_d3d11 integration/win_surface_d3d11.c)
    target_link_libraries(nativekit_win_surface_d3d11 PRIVATE NativeKit::nativekit)
    set_target_properties(nativekit_win_surface_d3d11 PROPERTIES LINKER_LANGUAGE CXX)
    add_test(NAME win_surface_d3d11 COMMAND nativekit_win_surface_d3d11)
    set_tests_properties(win_surface_d3d11 PROPERTIES LABELS "windows;graphics" TIMEOUT 120)
    add_executable(nativekit_win_text_input integration/win_text_input.c)
    target_link_libraries(nativekit_win_text_input PRIVATE NativeKit::nativekit imm32 ole32)
    set_target_properties(nativekit_win_text_input PROPERTIES LINKER_LANGUAGE CXX)
    add_test(NAME win_text_input COMMAND nativekit_win_text_input)
    set_tests_properties(win_text_input PROPERTIES LABELS "windows;text-input" TIMEOUT 120
        SKIP_RETURN_CODE 77)
    add_executable(nativekit_win_accessibility_uia integration/win_accessibility_uia.cpp)
    target_link_libraries(nativekit_win_accessibility_uia PRIVATE NativeKit::nativekit
        ole32 oleaut32)
    add_test(NAME win_accessibility_uia COMMAND nativekit_win_accessibility_uia)
    set_tests_properties(win_accessibility_uia PROPERTIES
        LABELS "windows;accessibility;graphics"
        TIMEOUT 120)
    if(NK_WEBVIEW2_LOADER_DLL)
        add_custom_command(TARGET nativekit_win_integration POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${NK_WEBVIEW2_LOADER_DLL}"
                "$<TARGET_FILE_DIR:nativekit_win_integration>")
    endif()
endif()

if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    add_executable(nativekit_osx_integration integration/osx_create_destroy.c)
    target_link_libraries(nativekit_osx_integration PRIVATE NativeKit::nativekit)
    set_target_properties(nativekit_osx_integration PROPERTIES LINKER_LANGUAGE OBJCXX)
    add_test(NAME osx_create_destroy COMMAND nativekit_osx_integration)
    set_tests_properties(osx_create_destroy PROPERTIES LABELS "macos")
    add_executable(nativekit_osx_surface_metal integration/osx_surface_metal.c)
    target_link_libraries(nativekit_osx_surface_metal PRIVATE NativeKit::nativekit)
    set_target_properties(nativekit_osx_surface_metal PROPERTIES LINKER_LANGUAGE OBJCXX)
    add_test(NAME osx_surface_metal COMMAND nativekit_osx_surface_metal)
    set_tests_properties(osx_surface_metal PROPERTIES LABELS "macos;graphics" SKIP_RETURN_CODE 77)
    add_executable(nativekit_osx_text_input integration/osx_text_input.mm)
    target_link_libraries(nativekit_osx_text_input PRIVATE NativeKit::nativekit "-framework Cocoa")
    set_target_properties(nativekit_osx_text_input PROPERTIES LINKER_LANGUAGE OBJCXX)
    add_test(NAME osx_text_input COMMAND nativekit_osx_text_input)
    set_tests_properties(osx_text_input PROPERTIES LABELS "macos;text-input" TIMEOUT 120)
endif()

if(MINGW)
    foreach(target nativekit_c_abi nativekit_core_tests nativekit_win_integration
            nativekit_win_accessibility_uia nativekit_win_text_input)
        if(TARGET ${target})
            # Keep Wine smoke tests and redistributed test binaries independent
            # of the compiler runtime DLLs installed with a particular MinGW.
            target_link_options(${target} PRIVATE -static -static-libgcc -static-libstdc++)
        endif()
    endforeach()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    # These binaries are compile/link probes for the iOS SDK. Running them
    # requires an application host and a simulator or physical device.
    set_tests_properties(c_abi core time platform_parity wasm_host_allocator PROPERTIES DISABLED TRUE)
endif()

if(NK_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    set(sanitized_test_targets nativekit_c_abi nativekit_core_tests nativekit_time_tests
        nativekit_platform_parity)
    if(TARGET nativekit_gtk_integration)
        list(APPEND sanitized_test_targets nativekit_gtk_integration)
    endif()
    if(TARGET nativekit_gtk_webview_resize)
        list(APPEND sanitized_test_targets nativekit_gtk_webview_resize)
    endif()
    if(TARGET nativekit_linux_notification_failure)
        list(APPEND sanitized_test_targets nativekit_linux_notification_failure)
    endif()
    if(TARGET nativekit_linux_joystick_uinput)
        list(APPEND sanitized_test_targets nativekit_linux_joystick_uinput)
    endif()
    foreach(target ${sanitized_test_targets})
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endforeach()
    if(TARGET nativekit_gtk_integration)
        # GTK/Pango/Fontconfig retain process-lifetime caches that LSan reports;
        # keep address/undefined checks here and leak checks in the core tests.
        set_tests_properties(gtk_create_destroy PROPERTIES
            ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")
    endif()
    if(TARGET nativekit_gtk_webview_resize)
        set_tests_properties(gtk_webview_resize PROPERTIES
            ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")
    endif()
endif()
