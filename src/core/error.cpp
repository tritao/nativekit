#include "core/error.hpp"

#include <array>
#include <cstring>

namespace nk::core {
namespace {
thread_local std::array<char, 512> current_error{};
}

void clear_error() noexcept { current_error[0] = '\0'; }

void set_error(std::string_view message) noexcept {
    const auto length = message.size() < current_error.size() - 1
        ? message.size() : current_error.size() - 1;
    std::memcpy(current_error.data(), message.data(), length);
    current_error[length] = '\0';
}

const char* last_error() noexcept { return current_error.data(); }
}
