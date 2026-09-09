add_executable(nativekit_hello hello.c)
target_link_libraries(nativekit_hello PRIVATE NativeKit::nativekit)
set_target_properties(nativekit_hello PROPERTIES LINKER_LANGUAGE CXX)

add_executable(nativekit_showcase showcase.c)
target_link_libraries(nativekit_showcase PRIVATE NativeKit::nativekit)
set_target_properties(nativekit_showcase PROPERTIES LINKER_LANGUAGE CXX)

add_executable(nativekit_opengl opengl.c)
target_link_libraries(nativekit_opengl PRIVATE NativeKit::nativekit)
set_target_properties(nativekit_opengl PROPERTIES LINKER_LANGUAGE CXX)

add_executable(nativekit_browser browser.c)
target_link_libraries(nativekit_browser PRIVATE NativeKit::nativekit)
set_target_properties(nativekit_browser PROPERTIES LINKER_LANGUAGE CXX)
