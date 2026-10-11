if(NOT EXISTS "${NK_VENDOR_DIR}/daisysp/Source/daisysp.h")
    message(FATAL_ERROR "DaisySP submodule is missing; run git submodule update --init")
endif()

if(NOT EXISTS "${NK_VENDOR_DIR}/tinysoundfont/tsf.h")
    message(FATAL_ERROR "TinySoundFont submodule is missing; run git submodule update --init --recursive")
endif()

foreach(NK_SIGNALSMITH_FILE
    "${NK_VENDOR_DIR}/signalsmith-basics/include/signalsmith-basics/reverb.h"
    "${NK_VENDOR_DIR}/signalsmith-dsp/include/signalsmith-dsp/delay.h")
    if(NOT EXISTS "${NK_SIGNALSMITH_FILE}")
        message(FATAL_ERROR "Signalsmith submodules are missing; run git submodule update --init --recursive")
    endif()
endforeach()

add_library(nativekit_daisysp OBJECT
    "${NK_VENDOR_DIR}/daisysp/Source/Dynamics/crossfade.cpp"
    "${NK_VENDOR_DIR}/daisysp/Source/Utility/dcblock.cpp"
    "${NK_VENDOR_DIR}/daisysp/Source/Synthesis/fm2.cpp"
    "${NK_VENDOR_DIR}/daisysp/Source/PhysicalModeling/stringvoice.cpp"
    "${NK_VENDOR_DIR}/daisysp/Source/PhysicalModeling/KarplusString.cpp"
    "${NK_VENDOR_DIR}/daisysp/Source/PhysicalModeling/modalvoice.cpp"
    "${NK_VENDOR_DIR}/daisysp/Source/PhysicalModeling/resonator.cpp"
    "${NK_VENDOR_DIR}/daisysp/Source/PhysicalModeling/drip.cpp"
    "${NK_VENDOR_DIR}/daisysp/Source/Drums/analogbassdrum.cpp"
    "${NK_VENDOR_DIR}/daisysp/Source/Drums/synthbassdrum.cpp"
    "${NK_VENDOR_DIR}/daisysp/Source/Drums/analogsnaredrum.cpp"
    "${NK_VENDOR_DIR}/daisysp/Source/Drums/synthsnaredrum.cpp"
    "${NK_VENDOR_DIR}/daisysp/Source/Drums/hihat.cpp"
    "${NK_VENDOR_DIR}/daisysp/Source/Synthesis/formantosc.cpp"
    "${NK_VENDOR_DIR}/daisysp/Source/Synthesis/vosim.cpp"
    "${NK_VENDOR_DIR}/daisysp/Source/Synthesis/zoscillator.cpp"
    "${NK_VENDOR_DIR}/daisysp/Source/Synthesis/variablesawosc.cpp"
    "${NK_VENDOR_DIR}/daisysp/Source/Synthesis/variableshapeosc.cpp"
    "${NK_VENDOR_DIR}/daisysp/Source/Synthesis/oscillatorbank.cpp"
    "${NK_VENDOR_DIR}/daisysp/Source/Noise/grainlet.cpp"
    "${NK_VENDOR_DIR}/daisysp/Source/Noise/particle.cpp"
    "${NK_VENDOR_DIR}/daisysp/Source/Noise/clockednoise.cpp"
    "${NK_VENDOR_DIR}/daisysp/Source/Sampling/granularplayer.cpp"
    "${NK_VENDOR_DIR}/daisysp/Source/Control/phasor.cpp"

    "${NK_VENDOR_DIR}/daisysp/Source/Control/adsr.cpp"
    "${NK_VENDOR_DIR}/daisysp/Source/Filters/svf.cpp"
    "${NK_VENDOR_DIR}/daisysp/Source/Synthesis/oscillator.cpp"
    "${NK_VENDOR_DIR}/daisysp/Source/Synthesis/wavetable_oscillator.cpp")
target_compile_features(nativekit_daisysp PRIVATE cxx_std_17)
# All Daisy headers and compiled sources must share the host random binding.
target_compile_definitions(nativekit_daisysp PRIVATE DAISYSP_SCOPED_RANDOM)
target_compile_definitions(nativekit PRIVATE DAISYSP_SCOPED_RANDOM)
set_target_properties(nativekit_daisysp PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(nativekit_daisysp SYSTEM PRIVATE
    "${NK_VENDOR_DIR}/daisysp/Source"
    "${NK_VENDOR_DIR}/daisysp/Source/Utility"
    "${NK_VENDOR_DIR}/daisysp/Source/Sampling"
    "${NK_VENDOR_DIR}/daisysp/Source/PhysicalModeling")
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(nativekit_daisysp PRIVATE -Wno-enum-compare-conditional)
endif()

target_sources(nativekit PRIVATE
    src/audio.cpp
    src/audio_effect_processor.cpp
    src/audio_delay_processor.cpp
    src/audio_dsp.cpp
    src/audio_sample_source.cpp
    src/audio_soundfont.cpp
    src/audio_dsp_backend.cpp
    src/audio_daisy_source.cpp
    $<TARGET_OBJECTS:nativekit_daisysp>)

target_compile_definitions(nativekit PRIVATE NKAUDIO_BUILDING_LIBRARY)
target_include_directories(nativekit PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${NK_VENDOR_DIR}/miniaudio")
target_include_directories(nativekit SYSTEM PRIVATE
    "${NK_VENDOR_DIR}/daisysp/Source"
    "${NK_VENDOR_DIR}/daisysp/Source/Utility"
    "${NK_VENDOR_DIR}/signalsmith-basics/include"
    "${NK_VENDOR_DIR}/signalsmith-dsp/include"
    "${NK_VENDOR_DIR}/tinysoundfont")
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
    add_executable(nativekit_miniaudio_splitter_tests
        "${NK_VENDOR_DIR}/miniaudio/tests/node_graph/splitter_clock.c")
    if(UNIX)
        target_link_libraries(nativekit_miniaudio_splitter_tests PRIVATE m)
    endif()
    add_test(NAME miniaudio_splitter COMMAND nativekit_miniaudio_splitter_tests)
    set_tests_properties(miniaudio_splitter PROPERTIES LABELS "audio")

    find_package(Threads REQUIRED)
    add_executable(nativekit_miniaudio_detach_tests
        "${NK_VENDOR_DIR}/miniaudio/tests/node_graph/detach_iteration.cpp")
    target_compile_features(nativekit_miniaudio_detach_tests PRIVATE cxx_std_17)
    target_link_libraries(nativekit_miniaudio_detach_tests PRIVATE Threads::Threads)
    if(UNIX)
        target_link_libraries(nativekit_miniaudio_detach_tests PRIVATE m)
    endif()
    add_test(NAME miniaudio_detach COMMAND nativekit_miniaudio_detach_tests)
    set_tests_properties(miniaudio_detach PROPERTIES LABELS "audio" TIMEOUT 15)

    target_compile_definitions(nativekit PRIVATE NK_AUDIO_TESTING)
    add_executable(nativekit_audio_multi_dsp_tests tests/audio_multi_dsp.cpp)
    target_compile_features(nativekit_audio_multi_dsp_tests PRIVATE cxx_std_17)
    target_link_libraries(nativekit_audio_multi_dsp_tests PRIVATE NativeKit::audio_graph)
    add_test(NAME audio_multi_dsp COMMAND nativekit_audio_multi_dsp_tests)
    set_tests_properties(audio_multi_dsp PROPERTIES LABELS "audio")

    add_executable(nativekit_audio_sends_tests tests/audio_sends.cpp)
    target_compile_features(nativekit_audio_sends_tests PRIVATE cxx_std_17)
    target_link_libraries(nativekit_audio_sends_tests PRIVATE NativeKit::audio_graph)
    add_test(NAME audio_sends COMMAND nativekit_audio_sends_tests)
    set_tests_properties(audio_sends PROPERTIES LABELS "audio")
    add_executable(nativekit_audio_routing_tests tests/audio_routing.cpp)
    target_compile_features(nativekit_audio_routing_tests PRIVATE cxx_std_17)
    target_link_libraries(nativekit_audio_routing_tests PRIVATE NativeKit::audio_graph)
    add_test(NAME audio_routing COMMAND nativekit_audio_routing_tests)
    set_tests_properties(audio_routing PROPERTIES LABELS "audio")
    add_executable(nativekit_audio_tests tests/audio.cpp)
    target_compile_features(nativekit_audio_tests PRIVATE cxx_std_17)
    target_link_libraries(nativekit_audio_tests PRIVATE NativeKit::audio_graph)
    add_test(NAME audio COMMAND nativekit_audio_tests)
    set_tests_properties(audio PROPERTIES LABELS "audio" SKIP_RETURN_CODE 77)

    add_executable(nativekit_audio_effects_tests tests/audio_effects.cpp src/audio_effect_processor.cpp src/audio_delay_processor.cpp)
    target_compile_features(nativekit_audio_effects_tests PRIVATE cxx_std_17)
    target_include_directories(nativekit_audio_effects_tests PRIVATE src "${NK_VENDOR_DIR}/miniaudio")
    target_include_directories(nativekit_audio_effects_tests SYSTEM PRIVATE
            "${NK_VENDOR_DIR}/signalsmith-basics/include" "${NK_VENDOR_DIR}/signalsmith-dsp/include")
    target_sources(nativekit_audio_effects_tests PRIVATE $<TARGET_OBJECTS:nativekit_miniaudio>)
    target_link_libraries(nativekit_audio_effects_tests PRIVATE NativeKit::audio_graph)
    if(UNIX AND NOT APPLE)
        target_link_libraries(nativekit_audio_effects_tests PRIVATE ${CMAKE_DL_LIBS} pthread m)
    elseif(APPLE)
        target_link_libraries(nativekit_audio_effects_tests PRIVATE "-framework CoreAudio" "-framework AudioToolbox")
    endif()
    add_test(NAME audio_effects COMMAND nativekit_audio_effects_tests)
    set_tests_properties(audio_effects PROPERTIES LABELS "audio")

    add_executable(nativekit_audio_daisy_boundaries_tests
        tests/audio_daisy_boundaries.cpp src/audio_dsp_backend.cpp src/audio_daisy_source.cpp
        $<TARGET_OBJECTS:nativekit_daisysp>)
    target_compile_definitions(nativekit_audio_daisy_boundaries_tests PRIVATE DAISYSP_SCOPED_RANDOM)
    target_compile_features(nativekit_audio_daisy_boundaries_tests PRIVATE cxx_std_17)
    target_include_directories(nativekit_audio_daisy_boundaries_tests PRIVATE src include "${PROJECT_SOURCE_DIR}/include")
    target_include_directories(nativekit_audio_daisy_boundaries_tests SYSTEM PRIVATE
        "${NK_VENDOR_DIR}/daisysp/Source" "${NK_VENDOR_DIR}/daisysp/Source/Utility")
    add_test(NAME audio_daisy_boundaries COMMAND nativekit_audio_daisy_boundaries_tests)
    set_tests_properties(audio_daisy_boundaries PROPERTIES LABELS "audio")

    add_executable(nativekit_audio_daisy_initialization_tests
        "${NK_VENDOR_DIR}/daisysp/tests/initialization.cpp" $<TARGET_OBJECTS:nativekit_daisysp>)
    target_compile_definitions(nativekit_audio_daisy_initialization_tests PRIVATE DAISYSP_SCOPED_RANDOM)
    target_compile_features(nativekit_audio_daisy_initialization_tests PRIVATE cxx_std_17)
    target_include_directories(nativekit_audio_daisy_initialization_tests PRIVATE src)
    target_include_directories(nativekit_audio_daisy_initialization_tests SYSTEM PRIVATE
        "${NK_VENDOR_DIR}/daisysp/Source" "${NK_VENDOR_DIR}/daisysp/Source/Utility")
    add_test(NAME audio_daisy_initialization COMMAND nativekit_audio_daisy_initialization_tests)
    set_tests_properties(audio_daisy_initialization PROPERTIES LABELS "audio")

    add_executable(nativekit_audio_daisy_sources_tests tests/audio_daisy_sources.cpp)
    target_link_libraries(nativekit_audio_daisy_sources_tests PRIVATE NativeKit::audio_dsp)
    add_test(NAME audio_daisy_sources COMMAND nativekit_audio_daisy_sources_tests)
    set_tests_properties(audio_daisy_sources PROPERTIES LABELS "audio")

    add_executable(nativekit_audio_dsp_voice_tests tests/audio_dsp_voice.cpp)
    target_compile_features(nativekit_audio_dsp_voice_tests PRIVATE cxx_std_17)
    target_link_libraries(nativekit_audio_dsp_voice_tests PRIVATE NativeKit::audio_dsp)
    add_test(NAME audio_dsp_voice COMMAND nativekit_audio_dsp_voice_tests)
    set_tests_properties(audio_dsp_voice PROPERTIES LABELS "audio")

    add_executable(nativekit_audio_soundfont_tests tests/audio_soundfont.cpp)
    target_compile_features(nativekit_audio_soundfont_tests PRIVATE cxx_std_17)
    target_compile_definitions(nativekit_audio_soundfont_tests PRIVATE
        NK_SF2_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/minimal.sf2")
    target_link_libraries(nativekit_audio_soundfont_tests PRIVATE NativeKit::audio_dsp)
    add_test(NAME audio_soundfont COMMAND nativekit_audio_soundfont_tests)
    set_tests_properties(audio_soundfont PROPERTIES LABELS "audio")

    add_executable(nativekit_audio_runtime_tests tests/audio_runtime.cpp)
    target_compile_features(nativekit_audio_runtime_tests PRIVATE cxx_std_17)
    target_link_libraries(nativekit_audio_runtime_tests PRIVATE NativeKit::audio_dsp)
    add_test(NAME audio_runtime COMMAND nativekit_audio_runtime_tests)
    set_tests_properties(audio_runtime PROPERTIES LABELS "audio")

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

# Compile only the selected header processors; avoid fetching unrelated FFT/Hilbert dependencies.
install(FILES "${NK_VENDOR_DIR}/signalsmith-basics/LICENSE.txt"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/NativeKit RENAME Signalsmith-Basics-LICENSE)
install(FILES "${NK_VENDOR_DIR}/signalsmith-dsp/LICENSE.txt"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/NativeKit RENAME Signalsmith-DSP-LICENSE)

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/patches/Signalsmith-STFX-LICENSE.txt"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/NativeKit)

install(FILES "${NK_VENDOR_DIR}/tinysoundfont/LICENSE"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/NativeKit RENAME TinySoundFont-LICENSE)
