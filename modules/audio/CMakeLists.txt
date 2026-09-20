if(NOT EXISTS "${NK_VENDOR_DIR}/daisysp/Source/daisysp.h")
    message(FATAL_ERROR "DaisySP submodule is missing; run git submodule update --init")
endif()

add_library(nativekit_daisysp OBJECT
    "${NK_VENDOR_DIR}/daisysp/Source/Control/adsr.cpp"
    "${NK_VENDOR_DIR}/daisysp/Source/Filters/svf.cpp"
    "${NK_VENDOR_DIR}/daisysp/Source/Synthesis/oscillator.cpp")
target_compile_features(nativekit_daisysp PRIVATE cxx_std_17)
target_include_directories(nativekit_daisysp SYSTEM PRIVATE
    "${NK_VENDOR_DIR}/daisysp/Source"
    "${NK_VENDOR_DIR}/daisysp/Source/Utility")
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(nativekit_daisysp PRIVATE -Wno-enum-compare-conditional)
endif()

target_sources(nativekit PRIVATE
    src/audio.cpp
    src/audio_dsp.cpp
    src/audio_dsp_backend.cpp
    $<TARGET_OBJECTS:nativekit_daisysp>)

target_compile_definitions(nativekit PRIVATE NKAUDIO_BUILDING_LIBRARY)
target_include_directories(nativekit PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${NK_VENDOR_DIR}/miniaudio")
target_include_directories(nativekit SYSTEM PRIVATE
    "${NK_VENDOR_DIR}/daisysp/Source"
    "${NK_VENDOR_DIR}/daisysp/Source/Utility")
if(APPLE)
    target_link_libraries(nativekit PRIVATE "-framework CoreAudio" "-framework AudioToolbox")
endif()

add_library(nativekit_audio INTERFACE)
add_library(NativeKit::audio ALIAS nativekit_audio)
target_include_directories(nativekit_audio
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(nativekit_audio INTERFACE NativeKit::nativekit)
set_target_properties(nativekit_audio PROPERTIES EXPORT_NAME audio)

add_library(nativekit_audio_graph INTERFACE)
add_library(NativeKit::audio_graph ALIAS nativekit_audio_graph)
target_link_libraries(nativekit_audio_graph INTERFACE NativeKit::audio)
set_target_properties(nativekit_audio_graph PROPERTIES EXPORT_NAME audio_graph)

add_library(nativekit_audio_dsp INTERFACE)
add_library(NativeKit::audio_dsp ALIAS nativekit_audio_dsp)
target_link_libraries(nativekit_audio_dsp INTERFACE NativeKit::audio)
set_target_properties(nativekit_audio_dsp PROPERTIES EXPORT_NAME audio_dsp)

if(NK_BUILD_TESTS)
    add_executable(nativekit_audio_tests tests/audio.cpp)
    target_compile_features(nativekit_audio_tests PRIVATE cxx_std_17)
    target_link_libraries(nativekit_audio_tests PRIVATE NativeKit::audio_graph)
    add_test(NAME audio COMMAND nativekit_audio_tests)
    set_tests_properties(audio PROPERTIES LABELS "audio" SKIP_RETURN_CODE 77)

    add_executable(nativekit_audio_dsp_tests tests/audio_dsp.cpp)
    target_compile_features(nativekit_audio_dsp_tests PRIVATE cxx_std_17)
    target_link_libraries(nativekit_audio_dsp_tests PRIVATE NativeKit::audio_dsp)
    add_test(NAME audio_dsp COMMAND nativekit_audio_dsp_tests)
    set_tests_properties(audio_dsp PROPERTIES LABELS "audio")
endif()

if(NK_BUILD_EXAMPLES)
    add_executable(nativekit_audio_example examples/audio.c)
    target_link_libraries(nativekit_audio_example PRIVATE NativeKit::audio_graph)
    set_target_properties(nativekit_audio_example PROPERTIES LINKER_LANGUAGE CXX)
endif()

install(TARGETS nativekit_audio nativekit_audio_graph nativekit_audio_dsp EXPORT NativeKitTargets
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES "${NK_VENDOR_DIR}/miniaudio/LICENSE"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/NativeKit
    RENAME miniaudio-LICENSE)
install(FILES "${NK_VENDOR_DIR}/daisysp/LICENSE"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/NativeKit
    RENAME DaisySP-LICENSE)
