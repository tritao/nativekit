foreach(dependency IN ITEMS harfbuzz sheenbidi libunibreak budouxc skribidi)
    if(NOT EXISTS "${NK_VENDOR_DIR}/${dependency}/.git")
        message(FATAL_ERROR
            "${dependency} submodule is missing; run git submodule update --init")
    endif()
endforeach()

set(HB_BUILD_SUBSET OFF CACHE BOOL "" FORCE)
set(HB_BUILD_UTILS OFF CACHE BOOL "" FORCE)
set(HB_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(HB_BUILD_DOCS OFF CACHE BOOL "" FORCE)
add_subdirectory("${NK_VENDOR_DIR}/harfbuzz"
    "${CMAKE_CURRENT_BINARY_DIR}/third_party/harfbuzz" EXCLUDE_FROM_ALL)

add_library(nkui_sheenbidi STATIC
    "${NK_VENDOR_DIR}/sheenbidi/Source/SheenBidi.c")
target_compile_definitions(nkui_sheenbidi PRIVATE SB_CONFIG_UNITY)
target_include_directories(nkui_sheenbidi
    PUBLIC "${NK_VENDOR_DIR}/sheenbidi/Headers"
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
    "${NK_VENDOR_DIR}/libunibreak/src")

add_library(nkui_budouxc STATIC
    "${NK_VENDOR_DIR}/budouxc/src/budoux.c")
target_compile_features(nkui_budouxc PUBLIC c_std_17)
target_include_directories(nkui_budouxc
    PUBLIC "${NK_VENDOR_DIR}/budouxc/include"
    PRIVATE "${NK_VENDOR_DIR}/budouxc/src")

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
    PUBLIC "${NKUI_SKRIBIDI_DIR}/include"
    PRIVATE "${NKUI_SKRIBIDI_DIR}/src")
target_link_libraries(nkui_skribidi PRIVATE
    harfbuzz nkui_sheenbidi nkui_libunibreak nkui_budouxc)
if(NOT WIN32)
    target_link_libraries(nkui_skribidi PRIVATE m)
endif()

foreach(target IN ITEMS nkui_sheenbidi nkui_libunibreak nkui_budouxc nkui_skribidi)
    set_target_properties(${target} PROPERTIES
        POSITION_INDEPENDENT_CODE YES
        C_VISIBILITY_PRESET hidden)
endforeach()
