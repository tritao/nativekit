#pragma once

#include <string_view>

namespace nk::core {
void clear_error() noexcept;
void set_error(std::string_view message) noexcept;
const char *last_error() noexcept;
} // namespace nk::core
