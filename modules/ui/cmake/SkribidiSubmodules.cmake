foreach(dependency IN ITEMS harfbuzz sheenbidi libunibreak skribidi)
    if(NOT EXISTS "${NK_VENDOR_DIR}/${dependency}/.git")
        message(FATAL_ERROR
            "${dependency} submodule is missing; run git submodule update --init")
    endif()
endforeach()
if(NKUI_ENABLE_BUDOUX AND NOT EXISTS "${NK_VENDOR_DIR}/budouxc/.git")
    message(FATAL_ERROR
        "budouxc submodule is missing; run git submodule update --init")
endif()

set(HB_BUILD_SUBSET OFF CACHE BOOL "" FORCE)
set(HB_BUILD_UTILS OFF CACHE BOOL "" FORCE)
set(HB_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(HB_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(SKIP_INSTALL_LIBRARIES ON)
option(NKUI_ENABLE_HARFBUZZ_MINI
    "Build the private native HarfBuzz copy without legacy and AAT shaping"
    ON)
option(NKUI_ENABLE_HARFBUZZ_SIZE_OPTIMIZATION
    "Compile the private HarfBuzz copy for smaller Release code"
    ON)
set(NKUI_HARFBUZZ_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/third_party/harfbuzz")
set(NKUI_SAVED_BUILD_SHARED_LIBS "${BUILD_SHARED_LIBS}")
set(BUILD_SHARED_LIBS OFF)
add_subdirectory("${NK_VENDOR_DIR}/harfbuzz" "${NKUI_HARFBUZZ_BUILD_DIR}" EXCLUDE_FROM_ALL)
set(BUILD_SHARED_LIBS "${NKUI_SAVED_BUILD_SHARED_LIBS}")
unset(NKUI_SAVED_BUILD_SHARED_LIBS)
set_target_properties(harfbuzz PROPERTIES POSITION_INDEPENDENT_CODE YES)

# HarfBuzz is linked privately into the shared UI library. Keep its API and
# implementation symbols out of the UI library's dynamic symbol table; the
# public HarfBuzz target used by static builds retains its normal visibility.
if(NK_LIBRARY_TYPE STREQUAL "SHARED" AND NOT EMSCRIPTEN)
    set_target_properties(harfbuzz PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES)
    target_compile_definitions(harfbuzz PRIVATE
        HB_NO_BUFFER_MESSAGE
        HB_NO_BUFFER_SERIALIZE
        HB_NO_BUFFER_VERIFY
        HB_NO_MATH
        HB_NO_META
        HB_NO_OT_FONT_GLYPH_NAMES
        HB_NO_OT_SHAPE_FRACTIONS)
    if(NOT APPLE)
        if(NKUI_ENABLE_HARFBUZZ_MINI)
            target_compile_definitions(harfbuzz PRIVATE HB_MINI)
        else()
            target_compile_definitions(harfbuzz PRIVATE HB_NO_AAT)
        endif()
    endif()
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        target_compile_options(harfbuzz PRIVATE -fno-semantic-interposition)
    endif()
endif()
if(NKUI_ENABLE_HARFBUZZ_SIZE_OPTIMIZATION AND
   CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(harfbuzz PRIVATE $<$<CONFIG:Release>:-Os>)
endif()
if(APPLE AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
    # The pinned CoreText backend uses an int format for unsigned diagnostic
    # values; keep this third-party warning out of NativeKit builds.
    target_compile_options(harfbuzz PRIVATE -Wno-format)
endif()
set_property(TARGET harfbuzz PROPERTY INTERFACE_INCLUDE_DIRECTORIES
    "$<BUILD_INTERFACE:${NK_VENDOR_DIR}/harfbuzz/src>"
    "$<BUILD_INTERFACE:${NKUI_HARFBUZZ_BUILD_DIR}/src>")

add_library(nkui_sheenbidi STATIC
    "${NK_VENDOR_DIR}/sheenbidi/Source/SheenBidi.c")
target_compile_definitions(nkui_sheenbidi PRIVATE SB_CONFIG_UNITY)
target_include_directories(nkui_sheenbidi
    PUBLIC "$<BUILD_INTERFACE:${NK_VENDOR_DIR}/sheenbidi/Headers>"
    PRIVATE "${NK_VENDOR_DIR}/sheenbidi/Source")

add_library(nkui_libunibreak STATIC
    "${NK_VENDOR_DIR}/libunibreak/src/eastasianwidthdata.c"
    "${NK_VENDOR_DIR}/libunibreak/src/eastasianwidthdef.c"
    "${NK_VENDOR_DIR}/libunibreak/src/emojidata.c"
    "${NK_VENDOR_DIR}/libunibreak/src/emojidef.c"
    "${NK_VENDOR_DIR}/libunibreak/src/graphemebreak.c"
    "${NK_VENDOR_DIR}/libunibreak/src/graphemebreakdata.c"
    "${NK_VENDOR_DIR}/libunibreak/src/indicconjunctbreakdata.c"
    "${NK_VENDOR_DIR}/libunibreak/src/linebreak.c"
    "${NK_VENDOR_DIR}/libunibreak/src/linebreakdata.c"
    "${NK_VENDOR_DIR}/libunibreak/src/linebreakdef.c"
    "${NK_VENDOR_DIR}/libunibreak/src/unibreakbase.c"
    "${NK_VENDOR_DIR}/libunibreak/src/unibreakdef.c"
    "${NK_VENDOR_DIR}/libunibreak/src/wordbreak.c"
    "${NK_VENDOR_DIR}/libunibreak/src/wordbreakdata.c")
target_include_directories(nkui_libunibreak PUBLIC
    "$<BUILD_INTERFACE:${NK_VENDOR_DIR}/libunibreak/src>")

if(NKUI_ENABLE_BUDOUX)
    add_library(nkui_budouxc STATIC
        "${NK_VENDOR_DIR}/budouxc/src/budoux.c")
    target_compile_features(nkui_budouxc PUBLIC c_std_17)
    target_include_directories(nkui_budouxc
        PUBLIC "$<BUILD_INTERFACE:${NK_VENDOR_DIR}/budouxc/include>"
        PRIVATE "${NK_VENDOR_DIR}/budouxc/src")
endif()

set(NKUI_SKRIBIDI_DIR "${NK_VENDOR_DIR}/skribidi")
add_library(nkui_skribidi STATIC
    "${NKUI_SKRIBIDI_DIR}/src/skb_attributes.c"
    "${NKUI_SKRIBIDI_DIR}/src/skb_attribute_collection.c"
    "${NKUI_SKRIBIDI_DIR}/src/skb_canvas.c"
    "${NKUI_SKRIBIDI_DIR}/src/skb_common.c"
    "${NKUI_SKRIBIDI_DIR}/src/skb_editor.c"
    "${NKUI_SKRIBIDI_DIR}/src/skb_editor_rules.c"
    "${NKUI_SKRIBIDI_DIR}/src/skb_font_collection.c"
    "${NKUI_SKRIBIDI_DIR}/src/skb_icon_collection.c"
    "${NKUI_SKRIBIDI_DIR}/src/skb_image_atlas.c"
    "${NKUI_SKRIBIDI_DIR}/src/skb_layout.c"
    "${NKUI_SKRIBIDI_DIR}/src/skb_layout_cache.c"
    "${NKUI_SKRIBIDI_DIR}/src/skb_rasterizer.c"
    "${NKUI_SKRIBIDI_DIR}/src/skb_rich_layout.c"
    "${NKUI_SKRIBIDI_DIR}/src/skb_rich_text.c"
    "${NKUI_SKRIBIDI_DIR}/src/skb_text.c")
target_compile_features(nkui_skribidi PUBLIC c_std_17)
target_include_directories(nkui_skribidi
    PUBLIC "$<BUILD_INTERFACE:${NKUI_SKRIBIDI_DIR}/include>"
    PRIVATE "${NKUI_SKRIBIDI_DIR}/src")
target_link_libraries(nkui_skribidi PRIVATE
    "$<BUILD_INTERFACE:harfbuzz>"
    "$<INSTALL_INTERFACE:NativeKit::ui_harfbuzz>"
    nkui_sheenbidi nkui_libunibreak)
if(EMSCRIPTEN)
    # Skribidi's platform detector checks the lowercase emscripten macro,
    # while Emscripten defines the standard uppercase spelling.
    target_compile_definitions(nkui_skribidi PRIVATE SKB_PLATFORM_POSIX)
endif()
if(NKUI_ENABLE_BUDOUX)
    target_link_libraries(nkui_skribidi PRIVATE nkui_budouxc)
else()
    target_compile_definitions(nkui_skribidi PRIVATE SKB_DISABLE_BUDOUX)
endif()
if(NOT WIN32)
    target_link_libraries(nkui_skribidi PRIVATE m)
endif()

set(NKUI_SKRIBIDI_TARGETS nkui_sheenbidi nkui_libunibreak nkui_skribidi)
if(TARGET nkui_budouxc)
    list(APPEND NKUI_SKRIBIDI_TARGETS nkui_budouxc)
endif()
foreach(target IN LISTS NKUI_SKRIBIDI_TARGETS)
    set_target_properties(${target} PROPERTIES
        POSITION_INDEPENDENT_CODE YES
        C_VISIBILITY_PRESET hidden)
endforeach()
