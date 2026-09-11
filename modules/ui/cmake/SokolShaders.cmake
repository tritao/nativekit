set(NK_SOKOL_SHDC "" CACHE FILEPATH
    "Path to the sokol-shdc executable used to generate NativeKit UI shaders")

if(NK_SOKOL_SHDC)
    if(NOT EXISTS "${NK_SOKOL_SHDC}")
        message(FATAL_ERROR "NK_SOKOL_SHDC does not exist: ${NK_SOKOL_SHDC}")
    endif()
    set(NK_SOKOL_SHDC_EXECUTABLE "${NK_SOKOL_SHDC}")
else()
    find_program(NK_SOKOL_SHDC_EXECUTABLE NAMES sokol-shdc)
endif()

if(NOT NK_SOKOL_SHDC_EXECUTABLE)
    message(FATAL_ERROR
        "NativeKit UI requires sokol-shdc. Install sokol-tools-bin or set "
        "-DNK_SOKOL_SHDC=/path/to/sokol-shdc.")
endif()

if(EMSCRIPTEN AND NOT DEFINED NKUI_SHADER_LANGUAGES)
    set(NKUI_SHADER_LANGUAGES "glsl300es" CACHE STRING
        "Colon-separated sokol-shdc shader languages for NativeKit UI")
else()
    set(NKUI_SHADER_LANGUAGES
        "glsl410:glsl300es:hlsl5:metal_macos:metal_ios:metal_sim:wgsl:spirv_vk"
        CACHE STRING
        "Colon-separated sokol-shdc shader languages for NativeKit UI")
endif()
set(NKUI_SHADER_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated" CACHE INTERNAL
    "NativeKit UI generated shader directory")

function(nkui_generate_shader source output_name output_variable)
    get_filename_component(source_absolute "${source}" ABSOLUTE)
    set(output "${NKUI_SHADER_GENERATED_DIR}/${output_name}")
    set(depfile "${output}.d")
    add_custom_command(
        OUTPUT "${output}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${NKUI_SHADER_GENERATED_DIR}"
        COMMAND "${NK_SOKOL_SHDC_EXECUTABLE}"
            --input "${source_absolute}"
            --output "${output}"
            --slang "${NKUI_SHADER_LANGUAGES}"
            --no-log-cmdline
            --dependency-file "${depfile}"
        DEPENDS "${source_absolute}"
        DEPFILE "${depfile}"
        COMMENT "Generating ${output_name}"
        VERBATIM)
    set("${output_variable}" "${output}" PARENT_SCOPE)
endfunction()
