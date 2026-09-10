foreach(dependency IN ITEMS sokol nanovg)
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
    target_compile_definitions(nkui_nanovg_core PUBLIC NVG_NO_TEXT=1)
    set_target_properties(nkui_nanovg_core PROPERTIES
        POSITION_INDEPENDENT_CODE YES
        C_VISIBILITY_PRESET hidden)

else()
    message(STATUS "NativeKit UI NanoVG path preparation is currently enabled on desktop Linux only")
endif()
