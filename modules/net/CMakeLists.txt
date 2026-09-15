# The implementation is part of NativeKit's optional library because it uses
# the core event queue and request-ID machinery. This interface target keeps
# the consumer-facing module name consistent with the other optional modules.
add_library(nativekit_net INTERFACE)
add_library(NativeKit::net ALIAS nativekit_net)
target_link_libraries(nativekit_net INTERFACE NativeKit::nativekit)
target_include_directories(nativekit_net INTERFACE
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

install(TARGETS nativekit_net EXPORT NativeKitTargets)

if(NK_BUILD_TESTS AND NK_NET_BACKEND_CURL)
    add_executable(nativekit_net_contract_test tests/contract.cpp)
    target_compile_features(nativekit_net_contract_test PRIVATE cxx_std_17)
    target_include_directories(nativekit_net_contract_test PRIVATE
        "${PROJECT_SOURCE_DIR}/src")
    target_link_libraries(nativekit_net_contract_test PRIVATE NativeKit::net)
    add_test(NAME net_contract COMMAND nativekit_net_contract_test)
    set_tests_properties(net_contract PROPERTIES LABELS "network;linux" TIMEOUT 30)
    if(NK_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(nativekit_net_contract_test PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(nativekit_net_contract_test PRIVATE -fsanitize=address,undefined)
    endif()
endif()
