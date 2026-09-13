if(NOT DEFINED NKUI_SHADER_SOURCE_DIR OR NOT DEFINED NKUI_SHADER_OUTPUT)
    message(FATAL_ERROR "shader source directory and output are required")
endif()

file(GLOB_RECURSE shader_sources
    "${NKUI_SHADER_SOURCE_DIR}/glsl410/*.glsl"
    "${NKUI_SHADER_SOURCE_DIR}/glsl300es/*.glsl")
if(NOT shader_sources)
    message(FATAL_ERROR "NativeKit UI shader source files are missing")
endif()
list(SORT shader_sources)
file(WRITE "${NKUI_SHADER_OUTPUT}" "#pragma once\n\nnamespace nkui::shader_source {\n")
foreach(shader_source IN LISTS shader_sources)
    file(RELATIVE_PATH shader_name "${NKUI_SHADER_SOURCE_DIR}" "${shader_source}")
    string(REGEX REPLACE "\\.glsl$" "" symbol "${shader_name}")
    string(REGEX REPLACE "[/.-]" "_" symbol "${symbol}")
    file(READ "${shader_source}" shader_text)
    file(APPEND "${NKUI_SHADER_OUTPUT}"
        "inline constexpr char ui_shader_${symbol}[] = R\"NKUI_GLSL(${shader_text})NKUI_GLSL\";\n")
endforeach()
file(APPEND "${NKUI_SHADER_OUTPUT}" "} // namespace nkui::shader_source\n")
