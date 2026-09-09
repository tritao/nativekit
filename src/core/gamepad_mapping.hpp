#pragma once

#include "nativekit_gamepad.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace nk::core::gamepad {

enum class InputType : std::uint8_t { none, axis, button, hat };

struct Binding {
    InputType type = InputType::none;
    std::uint16_t index = 0;
    std::uint8_t hat_mask = 0;
    float axis_scale = 1.f;
    float axis_offset = 0.f;
};

struct Mapping {
    std::string guid;
    std::string name;
    std::string platform;
    std::array<Binding, NK_GAMEPAD_BUTTON_COUNT> buttons{};
    std::array<Binding, NK_GAMEPAD_AXIS_COUNT> axes{};
};

bool parse_mapping(std::string_view text, Mapping &mapping);
bool apply_mapping(const Mapping &mapping, const std::vector<float> &axes,
                   const std::vector<std::uint8_t> &buttons,
                   const std::vector<std::uint8_t> &hats, nk_gamepad_state &state);

} // namespace nk::core::gamepad
