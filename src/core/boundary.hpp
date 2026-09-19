#pragma once

#include "core/error.hpp"
#include "nativekit.h"

#include <string_view>
#include <utility>

namespace nk::core {

template <typename Function>
nk_result result_boundary(std::string_view operation, Function &&function) noexcept {
    (void)operation;
    return std::forward<Function>(function)();
}

template <typename Function> void callback_boundary(Function &&function) noexcept {
    std::forward<Function>(function)();
}

template <typename Result, typename Function>
Result callback_boundary_or(Result fallback, Function &&function) noexcept {
    (void)fallback;
    return std::forward<Function>(function)();
}

} // namespace nk::core
