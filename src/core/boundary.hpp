#pragma once

#include "core/error.hpp"
#include "nativekit.h"

#include <new>
#include <string_view>
#include <utility>

namespace nk::core {

template<typename Function>
nk_result result_boundary(std::string_view operation, Function&& function) noexcept {
    try {
        return std::forward<Function>(function)();
    } catch (const std::bad_alloc&) {
        set_error("out of memory at NativeKit C boundary");
        return NK_ERROR_OUT_OF_MEMORY;
    } catch (...) {
        set_error(operation);
        return NK_ERROR_UNKNOWN;
    }
}

template<typename Function>
void callback_boundary(Function&& function) noexcept {
    try {
        std::forward<Function>(function)();
    } catch (...) {
        /* Native callback frames must never observe a C++ exception. */
    }
}

template<typename Result, typename Function>
Result callback_boundary_or(Result fallback, Function&& function) noexcept {
    try {
        return std::forward<Function>(function)();
    } catch (...) {
        return fallback;
    }
}

} // namespace nk::core
