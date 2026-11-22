if(NOT EXISTS "${NK_VENDOR_DIR}/miniaudio/miniaudio.h" OR
   NOT EXISTS "${NK_VENDOR_DIR}/miniaudio/miniaudio.c")
    message(FATAL_ERROR "miniaudio submodule is missing; run git submodule update --init")
endif()

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

add_library(nativekit_miniaudio OBJECT "${NK_VENDOR_DIR}/miniaudio/miniaudio.c")
target_include_directories(nativekit_miniaudio PRIVATE "${NK_VENDOR_DIR}/miniaudio")
target_compile_definitions(nativekit_miniaudio PRIVATE MA_NO_ENCODING)
set_target_properties(nativekit_miniaudio PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    C_VISIBILITY_PRESET hidden)
if(EMSCRIPTEN)
    target_compile_definitions(nativekit_miniaudio PRIVATE
        MA_NO_RUNTIME_LINKING MA_ENABLE_ONLY_SPECIFIC_BACKENDS MA_ENABLE_WEBAUDIO)
    if(NK_AUDIO_WEB_WORKLETS)
        target_compile_definitions(nativekit_miniaudio PRIVATE MA_ENABLE_AUDIO_WORKLETS)
        target_compile_options(nativekit_miniaudio PRIVATE -pthread)
    endif()
endif()

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
set_target_properties(nativekit_daisysp PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES)
target_include_directories(nativekit_daisysp SYSTEM PRIVATE
    "${NK_VENDOR_DIR}/daisysp/Source"
    "${NK_VENDOR_DIR}/daisysp/Source/Utility"
    "${NK_VENDOR_DIR}/daisysp/Source/Sampling"
    "${NK_VENDOR_DIR}/daisysp/Source/PhysicalModeling")
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(nativekit_daisysp PRIVATE -Wno-enum-compare-conditional)
endif()
if(NK_AUDIO_WEB_WORKLETS)
    target_compile_options(nativekit_daisysp PRIVATE -pthread)
endif()

# Audio is its own library on top of the public NativeKit runtime, so the core
# library is the same whether or not audio is built. It reaches NativeKit's
# handles, events, errors and workers through the internal module bridge.
add_library(nativekit_audio ${NK_LIBRARY_TYPE}
    src/audio.cpp
    src/audio_effect_processor.cpp
    src/audio_delay_processor.cpp
    src/audio_dsp.cpp
    src/audio_sample_source.cpp
    src/audio_soundfont.cpp
    src/audio_dsp_backend.cpp
    src/audio_daisy_source.cpp
    $<TARGET_OBJECTS:nativekit_miniaudio>
    $<TARGET_OBJECTS:nativekit_daisysp>)
add_library(NativeKit::audio ALIAS nativekit_audio)
target_compile_features(nativekit_audio PRIVATE cxx_std_17)
target_compile_definitions(nativekit_audio PRIVATE NKAUDIO_BUILDING_LIBRARY DAISYSP_SCOPED_RANDOM)
if(NK_ENABLE_NO_EXCEPTIONS)
    target_compile_definitions(nativekit_audio PRIVATE NK_ENABLE_NO_EXCEPTIONS=1)
    if(MSVC)
        target_compile_options(nativekit_audio PRIVATE /EHs-c-)
    else()
        target_compile_options(nativekit_audio PRIVATE -fno-exceptions)
    endif()
endif()
target_include_directories(nativekit_audio
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        "${PROJECT_SOURCE_DIR}/src"
        "${NK_VENDOR_DIR}/miniaudio")
target_include_directories(nativekit_audio SYSTEM PRIVATE
    "${NK_VENDOR_DIR}/daisysp/Source"
    "${NK_VENDOR_DIR}/daisysp/Source/Utility"
    "${NK_VENDOR_DIR}/signalsmith-basics/include"
    "${NK_VENDOR_DIR}/signalsmith-dsp/include"
    "${NK_VENDOR_DIR}/tinysoundfont")
target_link_libraries(nativekit_audio PUBLIC NativeKit::nativekit)
if(MSVC)
    target_compile_options(nativekit_audio PRIVATE /W4 /permissive-)
else()
    target_compile_options(nativekit_audio PRIVATE -Wall -Wextra -Wpedantic)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(nativekit_audio PRIVATE
            -Wno-nullability-extension -Wno-nullability-completeness)
    endif()
endif()
if(APPLE)
    target_link_libraries(nativekit_audio PRIVATE "-framework CoreAudio" "-framework AudioToolbox")
elseif(UNIX AND NOT EMSCRIPTEN)
    find_package(Threads REQUIRED)
    target_link_libraries(nativekit_audio PRIVATE ${CMAKE_DL_LIBS} Threads::Threads m)
endif()
if(NK_AUDIO_WEB_WORKLETS)
    target_compile_options(nativekit_audio PUBLIC -pthread)
    target_link_options(nativekit_audio INTERFACE
        -pthread -sAUDIO_WORKLET=1 -sWASM_WORKERS=1 -sASYNCIFY)
endif()
if(NK_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(nativekit_audio PRIVATE
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(nativekit_audio PRIVATE -fsanitize=address,undefined)
endif()
set_target_properties(nativekit_audio PROPERTIES
    EXPORT_NAME audio
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    VERSION ${PROJECT_VERSION}
    SOVERSION 0)

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

    target_compile_definitions(nativekit_audio PRIVATE NK_AUDIO_TESTING)
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

    add_executable(nativekit_audio_dsp_articulation_tests tests/audio_dsp_articulation.cpp)
    target_compile_features(nativekit_audio_dsp_articulation_tests PRIVATE cxx_std_17)
    target_link_libraries(nativekit_audio_dsp_articulation_tests PRIVATE NativeKit::audio_dsp)
    add_test(NAME audio_dsp_articulation COMMAND nativekit_audio_dsp_articulation_tests)
    set_tests_properties(audio_dsp_articulation PROPERTIES LABELS "audio" TIMEOUT 15)
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
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
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
