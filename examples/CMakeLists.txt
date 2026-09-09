add_executable(nativekit_browser browser.c)
target_link_libraries(nativekit_browser PRIVATE NativeKit::nativekit)
set_target_properties(nativekit_browser PROPERTIES LINKER_LANGUAGE CXX)

