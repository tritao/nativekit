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

add_executable(nativekit_plugin_tests unit/plugin_tests.cpp)
target_compile_features(nativekit_plugin_tests PRIVATE cxx_std_17)
target_link_libraries(nativekit_plugin_tests PRIVATE NativeKit::nativekit)
add_test(NAME plugin COMMAND nativekit_plugin_tests)

add_executable(nativekit_dispatch_tests unit/dispatch_tests.cpp)
target_compile_features(nativekit_dispatch_tests PRIVATE cxx_std_17)
target_include_directories(nativekit_dispatch_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(nativekit_dispatch_tests PRIVATE NativeKit::nativekit)
add_test(NAME dispatch COMMAND nativekit_dispatch_tests)

add_executable(nativekit_task_tests unit/task_tests.cpp)
target_compile_features(nativekit_task_tests PRIVATE cxx_std_17)
target_link_libraries(nativekit_task_tests PRIVATE NativeKit::nativekit)
add_test(NAME task COMMAND nativekit_task_tests)

add_executable(nativekit_worker_pool_tests
    unit/worker_pool_tests.cpp
    ${PROJECT_SOURCE_DIR}/src/core/worker_pool.cpp)
target_compile_features(nativekit_worker_pool_tests PRIVATE cxx_std_17)
target_include_directories(nativekit_worker_pool_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(nativekit_worker_pool_tests PRIVATE NativeKit::nativekit)
add_test(NAME worker_pool COMMAND nativekit_worker_pool_tests)

if(NK_NET_BACKEND_CURL)
    add_executable(nativekit_net_contract_test unit/net_contract.cpp)
    target_compile_features(nativekit_net_contract_test PRIVATE cxx_std_17)
    target_include_directories(nativekit_net_contract_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
    target_link_libraries(nativekit_net_contract_test PRIVATE NativeKit::nativekit)
    add_test(NAME net_contract COMMAND nativekit_net_contract_test)
    set_tests_properties(net_contract PROPERTIES LABELS "network;linux" TIMEOUT 30)
    if(NK_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(nativekit_net_contract_test PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(nativekit_net_contract_test PRIVATE -fsanitize=address,undefined)
    endif()
    add_executable(nativekit_curl_progress_test unit/curl_progress.cpp)
    target_compile_features(nativekit_curl_progress_test PRIVATE cxx_std_17)
    target_include_directories(nativekit_curl_progress_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
    add_test(NAME curl_progress COMMAND nativekit_curl_progress_test)
endif()

add_executable(nativekit_platform_parity unit/platform_parity.cpp)
target_compile_features(nativekit_platform_parity PRIVATE cxx_std_17)
target_link_libraries(nativekit_platform_parity PRIVATE NativeKit::nativekit)
configure_file(${PROJECT_SOURCE_DIR}/tests/capability-snapshots.txt
    ${CMAKE_CURRENT_BINARY_DIR}/capability-snapshots.txt COPYONLY)
if(EMSCRIPTEN)
    target_compile_definitions(nativekit_platform_parity PRIVATE
        NK_CAPABILITY_SNAPSHOT_FILE="capability-snapshots.txt")
    target_link_options(nativekit_platform_parity PRIVATE
        "--preload-file=${CMAKE_CURRENT_BINARY_DIR}/capability-snapshots.txt@capability-snapshots.txt")
    set_target_properties(nativekit_platform_parity PROPERTIES SUFFIX ".html"
        LINKER_LANGUAGE CXX)
else()
    target_compile_definitions(nativekit_platform_parity PRIVATE
        NK_CAPABILITY_SNAPSHOT_FILE="${CMAKE_CURRENT_BINARY_DIR}/capability-snapshots.txt")
endif()
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

if(WIN32 OR (APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS") OR NK_BACKEND_GTK_AVAILABLE)
    add_executable(nativekit_capability_conformance integration/capability_conformance.c)
    target_compile_features(nativekit_capability_conformance PRIVATE c_std_11)
    target_link_libraries(nativekit_capability_conformance PRIVATE NativeKit::nativekit)
    set_target_properties(nativekit_capability_conformance PROPERTIES LINKER_LANGUAGE CXX)
endif()

add_executable(nativekit_wasm_host_allocator_test
    unit/wasm_host_allocator.cpp
    ${PROJECT_SOURCE_DIR}/src/wasm/host_allocator.cpp)
target_compile_features(nativekit_wasm_host_allocator_test PRIVATE cxx_std_17)
target_include_directories(nativekit_wasm_host_allocator_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
add_test(NAME wasm_host_allocator COMMAND nativekit_wasm_host_allocator_test)

if(EMSCRIPTEN)
    add_executable(nativekit_web_tasks integration/web_tasks.c)
    target_compile_features(nativekit_web_tasks PRIVATE c_std_11)
    target_link_libraries(nativekit_web_tasks PRIVATE NativeKit::nativekit)
    set_target_properties(nativekit_web_tasks PROPERTIES SUFFIX ".html"
        LINKER_LANGUAGE CXX)
    add_executable(nativekit_web_accessibility integration/web_accessibility.c)
    target_compile_features(nativekit_web_accessibility PRIVATE c_std_11)
    target_link_libraries(nativekit_web_accessibility PRIVATE NativeKit::nativekit)
    set_target_properties(nativekit_web_accessibility PROPERTIES SUFFIX ".html"
        LINKER_LANGUAGE CXX)
    add_executable(nativekit_web_system_equivalents integration/web_system_equivalents.c)
    target_compile_features(nativekit_web_system_equivalents PRIVATE c_std_11)
    target_link_libraries(nativekit_web_system_equivalents PRIVATE NativeKit::nativekit)
    set_target_properties(nativekit_web_system_equivalents PROPERTIES SUFFIX ".html"
        LINKER_LANGUAGE CXX)
endif()

if(NK_BACKEND_GTK_AVAILABLE)
    add_executable(nativekit_linux_joystick_uinput integration/linux_joystick_uinput.c)
    target_link_libraries(nativekit_linux_joystick_uinput PRIVATE NativeKit::nativekit m)
    set_target_properties(nativekit_linux_joystick_uinput PROPERTIES LINKER_LANGUAGE CXX)
    add_test(NAME linux_joystick_uinput COMMAND nativekit_linux_joystick_uinput)
    set_tests_properties(linux_joystick_uinput PROPERTIES SKIP_RETURN_CODE 77 LABELS "linux")

    find_program(XVFB_RUN xvfb-run)
    find_program(DBUS_RUN_SESSION dbus-run-session)
    if(XVFB_RUN)
        if(TARGET PkgConfig::WEBKITGTK)
            add_executable(nativekit_gtk_integration integration/gtk_create_destroy.c)
            target_link_libraries(nativekit_gtk_integration PRIVATE NativeKit::nativekit PkgConfig::GTK3)
            set_target_properties(nativekit_gtk_integration PROPERTIES LINKER_LANGUAGE CXX)
            add_test(NAME gtk_create_destroy
                COMMAND ${XVFB_RUN} -a $<TARGET_FILE:nativekit_gtk_integration>)
        endif()
        add_executable(nativekit_gtk_native_window integration/gtk_native_window.c)
        target_link_libraries(nativekit_gtk_native_window PRIVATE NativeKit::nativekit)
        set_target_properties(nativekit_gtk_native_window PROPERTIES LINKER_LANGUAGE CXX)
        add_test(NAME gtk_native_window
            COMMAND ${XVFB_RUN} -a $<TARGET_FILE:nativekit_gtk_native_window>)
        add_executable(nativekit_gtk_pointer_capture integration/pointer_capture.c)
        target_link_libraries(nativekit_gtk_pointer_capture PRIVATE NativeKit::nativekit)
        set_target_properties(nativekit_gtk_pointer_capture PROPERTIES LINKER_LANGUAGE CXX)
        add_test(NAME gtk_pointer_capture
            COMMAND ${XVFB_RUN} -a $<TARGET_FILE:nativekit_gtk_pointer_capture>)
        set_tests_properties(gtk_pointer_capture PROPERTIES
            LABELS "linux;input" TIMEOUT 30 SKIP_RETURN_CODE 77)
        add_executable(nativekit_gtk_surface_frame integration/gtk_surface_frame.c)
        target_link_libraries(nativekit_gtk_surface_frame PRIVATE NativeKit::nativekit)
        set_target_properties(nativekit_gtk_surface_frame PROPERTIES LINKER_LANGUAGE CXX)
        add_test(NAME gtk_surface_frame
            COMMAND ${XVFB_RUN} -a $<TARGET_FILE:nativekit_gtk_surface_frame>)
        add_executable(nativekit_gtk_surface_make_current
            integration/gtk_surface_make_current.c)
        target_link_libraries(nativekit_gtk_surface_make_current PRIVATE NativeKit::nativekit)
        set_target_properties(nativekit_gtk_surface_make_current PROPERTIES LINKER_LANGUAGE CXX)
        add_test(NAME gtk_surface_make_current
            COMMAND ${XVFB_RUN} -a $<TARGET_FILE:nativekit_gtk_surface_make_current>)
        add_executable(nativekit_gtk_surface_request_frame
            integration/gtk_surface_request_frame.c)
        target_link_libraries(nativekit_gtk_surface_request_frame PRIVATE NativeKit::nativekit)
        set_target_properties(nativekit_gtk_surface_request_frame PROPERTIES LINKER_LANGUAGE CXX)
        add_test(NAME gtk_surface_request_frame
            COMMAND ${XVFB_RUN} -a $<TARGET_FILE:nativekit_gtk_surface_request_frame>)
        add_executable(nativekit_gtk_surface_frame_transaction
            integration/gtk_surface_frame_transaction.c)
        target_link_libraries(nativekit_gtk_surface_frame_transaction PRIVATE NativeKit::nativekit)
        set_target_properties(nativekit_gtk_surface_frame_transaction
            PROPERTIES LINKER_LANGUAGE CXX)
        add_test(NAME gtk_surface_frame_transaction
            COMMAND ${XVFB_RUN} -a $<TARGET_FILE:nativekit_gtk_surface_frame_transaction>)
        add_executable(nativekit_gtk_accessibility integration/gtk_accessibility.c)
        target_link_libraries(nativekit_gtk_accessibility PRIVATE NativeKit::nativekit)
        set_target_properties(nativekit_gtk_accessibility PROPERTIES LINKER_LANGUAGE CXX)
        add_test(NAME gtk_accessibility
            COMMAND ${XVFB_RUN} -a $<TARGET_FILE:nativekit_gtk_accessibility>)
        if(TARGET PkgConfig::WEBKITGTK)
            add_executable(nativekit_gtk_webview_resize integration/gtk_webview_resize.c)
            target_link_libraries(nativekit_gtk_webview_resize PRIVATE NativeKit::nativekit)
            set_target_properties(nativekit_gtk_webview_resize PROPERTIES LINKER_LANGUAGE CXX)
            add_test(NAME gtk_webview_resize
                COMMAND ${XVFB_RUN} -a $<TARGET_FILE:nativekit_gtk_webview_resize>)
        endif()
        add_executable(nativekit_gtk_native_view integration/gtk_native_view.c)
        target_link_libraries(nativekit_gtk_native_view PRIVATE NativeKit::nativekit
            PkgConfig::GTK3)
        set_target_properties(nativekit_gtk_native_view PROPERTIES LINKER_LANGUAGE CXX)
        add_test(NAME gtk_native_view
            COMMAND ${XVFB_RUN} -a $<TARGET_FILE:nativekit_gtk_native_view>)
        add_executable(nativekit_gtk_resource_workflow integration/gtk_resource_workflow.c)
        target_link_libraries(nativekit_gtk_resource_workflow PRIVATE NativeKit::nativekit)
        set_target_properties(nativekit_gtk_resource_workflow PROPERTIES LINKER_LANGUAGE CXX)
        add_test(NAME gtk_resource_workflow
            COMMAND ${XVFB_RUN} -a $<TARGET_FILE:nativekit_gtk_resource_workflow>)
        add_test(NAME capability_conformance
            COMMAND ${XVFB_RUN} -a $<TARGET_FILE:nativekit_capability_conformance>)
        set_tests_properties(capability_conformance PROPERTIES
            LABELS "linux;conformance" TIMEOUT 120)
        if(DBUS_RUN_SESSION)
            add_executable(nativekit_gtk_menu integration/gtk_menu.cpp)
            target_compile_features(nativekit_gtk_menu PRIVATE cxx_std_17)
            target_link_libraries(nativekit_gtk_menu PRIVATE NativeKit::nativekit PkgConfig::GTK3)
            add_test(NAME gtk_menu
                COMMAND ${DBUS_RUN_SESSION} -- ${XVFB_RUN} -a $<TARGET_FILE:nativekit_gtk_menu>)
            set_tests_properties(gtk_menu PROPERTIES LABELS "linux;menu" TIMEOUT 30)
        endif()
    endif()
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

if(UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN)
    add_executable(nativekit_file_watch integration/file_watch.c)
    target_link_libraries(nativekit_file_watch PRIVATE NativeKit::nativekit)
    set_target_properties(nativekit_file_watch PROPERTIES LINKER_LANGUAGE CXX)
    if(NK_BACKEND_GTK_AVAILABLE AND XVFB_RUN)
        add_test(NAME file_watch
            COMMAND ${XVFB_RUN} -a $<TARGET_FILE:nativekit_file_watch>)
    else()
        add_test(NAME file_watch COMMAND nativekit_file_watch)
    endif()
    set_tests_properties(file_watch PROPERTIES LABELS "linux;file-watch" TIMEOUT 30
        SKIP_RETURN_CODE 77)
    add_executable(nativekit_clipboard_watch integration/clipboard_watch.c)
    target_link_libraries(nativekit_clipboard_watch PRIVATE NativeKit::nativekit)
    set_target_properties(nativekit_clipboard_watch PROPERTIES LINKER_LANGUAGE CXX)
    if(NK_BACKEND_GTK_AVAILABLE AND XVFB_RUN)
        add_test(NAME clipboard_watch
            COMMAND ${XVFB_RUN} -a $<TARGET_FILE:nativekit_clipboard_watch>)
    else()
        add_test(NAME clipboard_watch COMMAND nativekit_clipboard_watch)
    endif()
    set_tests_properties(clipboard_watch PROPERTIES LABELS "linux;clipboard-watch" TIMEOUT 30
        SKIP_RETURN_CODE 77)
endif()

if(WIN32)
    add_executable(nativekit_win_menu integration/win_menu.cpp)
    target_link_libraries(nativekit_win_menu PRIVATE NativeKit::nativekit)
    add_test(NAME win_menu COMMAND nativekit_win_menu)
    set_tests_properties(win_menu PROPERTIES LABELS "windows;menu" TIMEOUT 120)
    add_executable(nativekit_win_integration integration/win_create_destroy.c)
    target_link_libraries(nativekit_win_integration PRIVATE NativeKit::nativekit)
    set_target_properties(nativekit_win_integration PROPERTIES LINKER_LANGUAGE CXX)
    add_test(NAME win_create_destroy COMMAND nativekit_win_integration)
    set_tests_properties(win_create_destroy PROPERTIES LABELS "windows" TIMEOUT 120)
    add_executable(nativekit_win_pointer_capture integration/pointer_capture.c)
    target_link_libraries(nativekit_win_pointer_capture PRIVATE NativeKit::nativekit)
    set_target_properties(nativekit_win_pointer_capture PROPERTIES LINKER_LANGUAGE CXX)
    add_test(NAME win_pointer_capture COMMAND nativekit_win_pointer_capture)
    set_tests_properties(win_pointer_capture PROPERTIES
        LABELS "windows;input" TIMEOUT 120 SKIP_RETURN_CODE 77)
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
    add_test(NAME capability_conformance COMMAND nativekit_capability_conformance)
    set_tests_properties(capability_conformance PROPERTIES
        LABELS "windows;conformance" TIMEOUT 120)
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
    add_executable(nativekit_osx_pointer_capture integration/pointer_capture.c)
    target_link_libraries(nativekit_osx_pointer_capture PRIVATE NativeKit::nativekit)
    set_target_properties(nativekit_osx_pointer_capture PROPERTIES LINKER_LANGUAGE OBJCXX)
    add_test(NAME osx_pointer_capture COMMAND nativekit_osx_pointer_capture)
    set_tests_properties(osx_pointer_capture PROPERTIES
        LABELS "macos;input" TIMEOUT 120 SKIP_RETURN_CODE 77)
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
    add_executable(nativekit_osx_menu integration/osx_menu.mm)
    target_link_libraries(nativekit_osx_menu PRIVATE NativeKit::nativekit "-framework Cocoa")
    set_target_properties(nativekit_osx_menu PROPERTIES LINKER_LANGUAGE OBJCXX)
    add_test(NAME osx_menu COMMAND nativekit_osx_menu)
    set_tests_properties(osx_menu PROPERTIES LABELS "macos;menu" TIMEOUT 120)
    add_test(NAME capability_conformance COMMAND nativekit_capability_conformance)
    set_tests_properties(capability_conformance PROPERTIES
        LABELS "macos;conformance" TIMEOUT 120)
endif()

if(MINGW)
    foreach(target nativekit_c_abi nativekit_core_tests nativekit_win_integration
            nativekit_win_menu
            nativekit_win_pointer_capture
            nativekit_win_accessibility_uia nativekit_win_text_input
            nativekit_capability_conformance)
        if(TARGET ${target})
            # Keep Wine smoke tests and redistributed test binaries independent
            # of the compiler runtime DLLs installed with a particular MinGW.
            target_link_options(${target} PRIVATE -static -static-libgcc -static-libstdc++)
        endif()
    endforeach()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    add_executable(nativekit_ios_runtime_tests MACOSX_BUNDLE integration/ios_runtime.mm)
    target_compile_features(nativekit_ios_runtime_tests PRIVATE cxx_std_17)
    target_link_libraries(nativekit_ios_runtime_tests PRIVATE NativeKit::nativekit
        "-framework Foundation" "-framework UIKit" "-framework CoreGraphics"
        "-framework Metal" "-framework QuartzCore" "-framework UniformTypeIdentifiers"
        "-framework UserNotifications" "-framework WebKit" "-framework GameController")
    set_target_properties(nativekit_ios_runtime_tests PROPERTIES
        LINKER_LANGUAGE OBJCXX
        MACOSX_BUNDLE_INFO_PLIST
            "${PROJECT_SOURCE_DIR}/tests/integration/ios_runtime-Info.plist"
        XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
        XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO
        XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED NO
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY ""
        XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET 15.0
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "org.nativekit.tests.ios-runtime"
        XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2")
    target_compile_options(nativekit_ios_runtime_tests PRIVATE
        $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>
        $<$<COMPILE_LANGUAGE:OBJCXX>:-fblocks>)
    # The portable binaries remain compile/link probes for the iOS SDK. The
    # application-hosted runtime target is launched by the simulator workflow.
    set_tests_properties(c_abi core time platform_parity wasm_host_allocator PROPERTIES DISABLED TRUE)
endif()

if(NK_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    set(sanitized_test_targets nativekit_c_abi nativekit_core_tests nativekit_time_tests
        nativekit_platform_parity)
    if(TARGET nativekit_capability_conformance)
        list(APPEND sanitized_test_targets nativekit_capability_conformance)
    endif()
    if(TARGET nativekit_gtk_integration)
        list(APPEND sanitized_test_targets nativekit_gtk_integration)
    endif()
    if(TARGET nativekit_gtk_webview_resize)
        list(APPEND sanitized_test_targets nativekit_gtk_webview_resize)
    endif()
    if(TARGET nativekit_gtk_resource_workflow)
        list(APPEND sanitized_test_targets nativekit_gtk_resource_workflow)
    endif()
    if(TARGET nativekit_linux_notification_failure)
        list(APPEND sanitized_test_targets nativekit_linux_notification_failure)
    endif()
    if(TARGET nativekit_linux_joystick_uinput)
        list(APPEND sanitized_test_targets nativekit_linux_joystick_uinput)
    endif()
    if(TARGET nativekit_file_watch)
        list(APPEND sanitized_test_targets nativekit_file_watch)
    endif()
    if(TARGET nativekit_clipboard_watch)
        list(APPEND sanitized_test_targets nativekit_clipboard_watch)
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
        if(TEST capability_conformance)
            set_tests_properties(capability_conformance PROPERTIES
                ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")
        endif()
    endif()
    if(TARGET nativekit_gtk_webview_resize)
        set_tests_properties(gtk_webview_resize PROPERTIES
            ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")
    endif()
endif()
