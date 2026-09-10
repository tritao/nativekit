foreach(dependency IN ITEMS sokol nanovg nanovg-sokol)
    if(NOT EXISTS "${NK_VENDOR_DIR}/${dependency}/.git")
        message(FATAL_ERROR
            "${dependency} submodule is missing; run git submodule update --init")
    endif()
endforeach()

if(UNIX AND NOT APPLE AND NOT ANDROID)
    add_library(nkui_nanovg_core STATIC
        "${NK_VENDOR_DIR}/nanovg/src/nanovg.c")
    target_include_directories(nkui_nanovg_core
        PUBLIC "${NK_VENDOR_DIR}/nanovg/src"
    )
    set_target_properties(nkui_nanovg_core PROPERTIES
        POSITION_INDEPENDENT_CODE YES
        C_VISIBILITY_PRESET hidden)

    add_library(nkui_nanovg STATIC src/render/nanovg_sokol.c)
    target_include_directories(nkui_nanovg
        PUBLIC "${NK_VENDOR_DIR}/nanovg/src"
        PRIVATE
            "${NK_VENDOR_DIR}/nanovg-sokol/src/nanovg_sokol"
            "${NK_VENDOR_DIR}/sokol")
    target_link_libraries(nkui_nanovg PUBLIC nkui_nanovg_core nativekit_sokol_runtime)
    set_target_properties(nkui_nanovg PROPERTIES
        POSITION_INDEPENDENT_CODE YES
        C_VISIBILITY_PRESET hidden)
else()
    message(STATUS "NativeKit UI NanoVG-Sokol bring-up is currently enabled on desktop Linux only")
endif()
