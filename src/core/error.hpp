#pragma once

#include "core/internal_api.hpp"

#include <string_view>

namespace nk::core {
NK_INTERNAL_API void clear_error() noexcept;
NK_INTERNAL_API void set_error(std::string_view message) noexcept;
const char *last_error() noexcept;
} // namespace nk::core
