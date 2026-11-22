add_library(nativekit_credentials ${NK_LIBRARY_TYPE}
    src/credentials.cpp)
add_library(NativeKit::credentials ALIAS nativekit_credentials)
target_compile_features(nativekit_credentials PRIVATE cxx_std_17)
target_compile_definitions(nativekit_credentials PRIVATE NKC_BUILDING_LIBRARY)
if(NK_ENABLE_NO_EXCEPTIONS)
    target_compile_definitions(nativekit_credentials PRIVATE NK_ENABLE_NO_EXCEPTIONS=1)
    if(MSVC)
        target_compile_options(nativekit_credentials PRIVATE /EHs-c-)
    else()
        target_compile_options(nativekit_credentials PRIVATE -fno-exceptions)
    endif()
endif()
target_include_directories(nativekit_credentials
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

if(NOT NK_BUILD_SHARED OR EMSCRIPTEN)
    target_compile_definitions(nativekit_credentials PUBLIC NKC_STATIC)
endif()

if(WIN32)
    target_sources(nativekit_credentials PRIVATE src/windows.cpp)
    target_link_libraries(nativekit_credentials PRIVATE Advapi32)
elseif(APPLE)
    target_sources(nativekit_credentials PRIVATE src/apple.mm)
    set_target_properties(nativekit_credentials PROPERTIES
        OBJCXX_STANDARD 17
        OBJCXX_STANDARD_REQUIRED YES)
    target_compile_options(nativekit_credentials PRIVATE
        $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    target_link_libraries(nativekit_credentials PRIVATE
        "-framework Foundation" "-framework Security")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # libsecret (0.19 or later) is opened on first use; building needs no development files.
    target_sources(nativekit_credentials PRIVATE src/linux.cpp)
    target_link_libraries(nativekit_credentials PRIVATE ${CMAKE_DL_LIBS})
else()
    target_sources(nativekit_credentials PRIVATE src/unsupported.cpp)
endif()

set_target_properties(nativekit_credentials PROPERTIES
    EXPORT_NAME credentials
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    VERSION ${PROJECT_VERSION}
    SOVERSION 0)

install(TARGETS nativekit_credentials EXPORT NativeKitTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if(NK_BUILD_TESTS)
    add_executable(nativekit_credentials_tests tests/credentials.cpp)
    target_compile_features(nativekit_credentials_tests PRIVATE cxx_std_17)
    target_link_libraries(nativekit_credentials_tests PRIVATE NativeKit::credentials)
    add_test(NAME credentials COMMAND nativekit_credentials_tests)
    set_tests_properties(credentials PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 30 LABELS "credentials")
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        # A libsecret-1.so.0 without libsecret's symbols first on the library path stands in
        # for a system without libsecret.
        add_library(nativekit_unusable_libsecret SHARED
            "${PROJECT_SOURCE_DIR}/tests/unit/unusable_library.c")
        set_target_properties(nativekit_unusable_libsecret PROPERTIES
            OUTPUT_NAME secret-1
            SOVERSION 0
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/unusable-libsecret")
        add_dependencies(nativekit_credentials_tests nativekit_unusable_libsecret)
        add_test(NAME credentials_without_libsecret
            COMMAND nativekit_credentials_tests --unavailable)
        set_tests_properties(credentials_without_libsecret PROPERTIES
            TIMEOUT 30 LABELS "credentials"
            ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_CURRENT_BINARY_DIR}/unusable-libsecret")
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(NKC_TEST_LIBSECRET QUIET IMPORTED_TARGET libsecret-1)
        endif()
        if(TARGET PkgConfig::NKC_TEST_LIBSECRET)
            # The backend declares the libsecret ABI it uses; compare it with libsecret's headers.
            add_executable(nativekit_libsecret_abi_test tests/libsecret_abi.cpp)
            target_compile_features(nativekit_libsecret_abi_test PRIVATE cxx_std_17)
            target_include_directories(nativekit_libsecret_abi_test PRIVATE
                "${CMAKE_CURRENT_SOURCE_DIR}/src")
            target_link_libraries(nativekit_libsecret_abi_test PRIVATE
                PkgConfig::NKC_TEST_LIBSECRET)
            add_test(NAME libsecret_abi COMMAND nativekit_libsecret_abi_test)
            set_tests_properties(libsecret_abi PROPERTIES LABELS "credentials")
        endif()
    endif()
endif()
