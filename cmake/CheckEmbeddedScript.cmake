# Fails when JavaScript embedded in C or C++ (EM_ASM, EM_JS) has had `===` or `!==` split by ClangFormat into
# `== =` or `!= =`, which is a syntax error that only shows when the web build runs. Keep embedded script inside
# `// clang-format off` regions.
file(STRINGS "${NK_FORMAT_SOURCE_LIST}" sources)
set(failures "")
foreach(source IN LISTS sources)
    file(STRINGS "${source}" lines REGEX "[=!]= =")
    foreach(line IN LISTS lines)
        string(STRIP "${line}" line)
        string(APPEND failures "  ${source}: ${line}\n")
    endforeach()
endforeach()
if(failures)
    message(FATAL_ERROR "ClangFormat split a JavaScript equality operator; restore it inside `// clang-format off`:\n${failures}")
endif()
