add_executable(nativekit_c_abi abi/c_api.c)
target_link_libraries(nativekit_c_abi PRIVATE NativeKit::nativekit)
set_target_properties(nativekit_c_abi PROPERTIES LINKER_LANGUAGE CXX)
add_test(NAME c_abi COMMAND nativekit_c_abi)

add_executable(nativekit_core_tests
    unit/core_tests.cpp
    ${PROJECT_SOURCE_DIR}/src/core/event_queue.cpp
    ${PROJECT_SOURCE_DIR}/src/core/handle_registry.cpp
)
target_include_directories(nativekit_core_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(nativekit_core_tests PRIVATE NativeKit::nativekit)
add_test(NAME core COMMAND nativekit_core_tests)

if(NK_BACKEND_GTK_AVAILABLE)
    find_program(XVFB_RUN xvfb-run)
    if(XVFB_RUN)
        add_executable(nativekit_gtk_integration integration/gtk_create_destroy.c)
        target_link_libraries(nativekit_gtk_integration PRIVATE NativeKit::nativekit)
        set_target_properties(nativekit_gtk_integration PROPERTIES LINKER_LANGUAGE CXX)
        add_test(NAME gtk_create_destroy
            COMMAND ${XVFB_RUN} -a $<TARGET_FILE:nativekit_gtk_integration>)
    endif()
endif()

if(NK_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    set(sanitized_test_targets nativekit_c_abi nativekit_core_tests)
    if(TARGET nativekit_gtk_integration)
        list(APPEND sanitized_test_targets nativekit_gtk_integration)
    endif()
    foreach(target ${sanitized_test_targets})
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endforeach()
endif()
