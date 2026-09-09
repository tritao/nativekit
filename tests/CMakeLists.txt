add_executable(nativekit_c_abi abi/c_api.c)
target_link_libraries(nativekit_c_abi PRIVATE NativeKit::nativekit)
set_target_properties(nativekit_c_abi PROPERTIES LINKER_LANGUAGE CXX)
add_test(NAME c_abi COMMAND nativekit_c_abi)

add_executable(nativekit_core_tests
    unit/core_tests.cpp
    ${PROJECT_SOURCE_DIR}/src/core/error.cpp
    ${PROJECT_SOURCE_DIR}/src/core/event_queue.cpp
    ${PROJECT_SOURCE_DIR}/src/core/handle_registry.cpp
)
target_compile_features(nativekit_core_tests PRIVATE cxx_std_17)
target_include_directories(nativekit_core_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(nativekit_core_tests PRIVATE NativeKit::nativekit)
if(MSVC)
    target_compile_options(nativekit_core_tests PRIVATE /EHsc)
endif()
add_test(NAME core COMMAND nativekit_core_tests)

if(NK_BACKEND_GTK_AVAILABLE)
    find_program(XVFB_RUN xvfb-run)
    if(XVFB_RUN)
        add_executable(nativekit_gtk_integration integration/gtk_create_destroy.c)
        target_link_libraries(nativekit_gtk_integration PRIVATE NativeKit::nativekit)
        set_target_properties(nativekit_gtk_integration PROPERTIES LINKER_LANGUAGE CXX)
        add_test(NAME gtk_create_destroy
            COMMAND ${XVFB_RUN} -a $<TARGET_FILE:nativekit_gtk_integration>)
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
    set_tests_properties(win_create_destroy PROPERTIES LABELS "windows")
    if(NK_WEBVIEW2_LOADER_DLL)
        add_custom_command(TARGET nativekit_win_integration POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${NK_WEBVIEW2_LOADER_DLL}"
                "$<TARGET_FILE_DIR:nativekit_win_integration>")
    endif()
endif()

if(APPLE)
    add_executable(nativekit_osx_integration integration/osx_create_destroy.c)
    target_link_libraries(nativekit_osx_integration PRIVATE NativeKit::nativekit)
    set_target_properties(nativekit_osx_integration PROPERTIES LINKER_LANGUAGE OBJCXX)
    add_test(NAME osx_create_destroy COMMAND nativekit_osx_integration)
    set_tests_properties(osx_create_destroy PROPERTIES LABELS "macos")
endif()

if(MINGW)
    foreach(target nativekit_c_abi nativekit_core_tests nativekit_win_integration)
        if(TARGET ${target})
            # Keep Wine smoke tests and redistributed test binaries independent
            # of the compiler runtime DLLs installed with a particular MinGW.
            target_link_options(${target} PRIVATE -static -static-libgcc -static-libstdc++)
        endif()
    endforeach()
endif()

if(NK_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    set(sanitized_test_targets nativekit_c_abi nativekit_core_tests)
    if(TARGET nativekit_gtk_integration)
        list(APPEND sanitized_test_targets nativekit_gtk_integration)
    endif()
    if(TARGET nativekit_gtk_webview_resize)
        list(APPEND sanitized_test_targets nativekit_gtk_webview_resize)
    endif()
    if(TARGET nativekit_linux_notification_failure)
        list(APPEND sanitized_test_targets nativekit_linux_notification_failure)
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
