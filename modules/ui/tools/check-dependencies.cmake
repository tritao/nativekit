if(NOT DEFINED NK_REPO_DIR OR NOT IS_DIRECTORY "${NK_REPO_DIR}/.git")
    message(FATAL_ERROR "NK_REPO_DIR must point to the NativeKit Git checkout")
endif()

set(dependencies budouxc clay harfbuzz libunibreak nanovg sheenbidi skribidi)
set(licenses
    budouxc/LICENSE
    clay/LICENSE.md
    harfbuzz/COPYING
    libunibreak/LICENCE
    nanovg/LICENSE.txt
    sheenbidi/LICENSE
    skribidi/LICENSE)

foreach(dependency IN LISTS dependencies)
    execute_process(
        COMMAND git -C "${NK_REPO_DIR}" ls-tree HEAD "vendor/${dependency}"
        RESULT_VARIABLE git_result
        OUTPUT_VARIABLE git_output
        ERROR_VARIABLE git_error
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT git_result EQUAL 0)
        message(FATAL_ERROR "git ls-tree failed for ${dependency}: ${git_error}")
    endif()
    string(REGEX MATCH "160000 commit ([0-9a-f]+)" _ "${git_output}")
    set(expected "${CMAKE_MATCH_1}")

    # A new submodule may be staged before the feature commit exists.
    if(NOT expected)
        execute_process(
            COMMAND git -C "${NK_REPO_DIR}" ls-files -s -- "vendor/${dependency}"
            RESULT_VARIABLE git_result
            OUTPUT_VARIABLE git_output
            ERROR_VARIABLE git_error
            OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(NOT git_result EQUAL 0)
            message(FATAL_ERROR "git ls-files failed for ${dependency}: ${git_error}")
        endif()
        string(REGEX MATCH "160000 ([0-9a-f]+)" _ "${git_output}")
        set(expected "${CMAKE_MATCH_1}")
    endif()
    if(NOT expected)
        message(FATAL_ERROR "${dependency} is not pinned as a gitlink")
    endif()

    execute_process(
        COMMAND git -C "${NK_REPO_DIR}/vendor/${dependency}" rev-parse HEAD
        RESULT_VARIABLE git_result
        OUTPUT_VARIABLE actual
        ERROR_VARIABLE git_error
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT git_result EQUAL 0)
        message(FATAL_ERROR "git rev-parse failed for ${dependency}: ${git_error}")
    endif()
    if(NOT actual STREQUAL expected)
        message(FATAL_ERROR "${dependency} revision mismatch: expected ${expected}, found ${actual}")
    endif()
endforeach()

foreach(license IN LISTS licenses)
    set(license_path "${NK_REPO_DIR}/vendor/${license}")
    if(NOT EXISTS "${license_path}")
        message(FATAL_ERROR "missing dependency license: vendor/${license}")
    endif()
    file(SIZE "${license_path}" license_size)
    if(license_size EQUAL 0)
        message(FATAL_ERROR "empty dependency license: vendor/${license}")
    endif()
endforeach()

message(STATUS "PASS: UI dependency revisions and licenses are reproducible")
