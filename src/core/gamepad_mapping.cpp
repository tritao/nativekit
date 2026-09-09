#include "core/gamepad_mapping.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <limits>
#include <unordered_map>

namespace {
using nk::core::gamepad::Binding;

bool parse_number(std::string_view text, std::size_t &position, std::uint16_t &value) {
    if (position >= text.size() || !std::isdigit(static_cast<unsigned char>(text[position])))
        return false;
    unsigned long result = 0;
    while (position < text.size() &&
           std::isdigit(static_cast<unsigned char>(text[position]))) {
        result = result * 10 + static_cast<unsigned int>(text[position++] - '0');
        if (result > std::numeric_limits<std::uint16_t>::max())
            return false;
    }
    value = static_cast<std::uint16_t>(result);
    return true;
}

bool parse_binding(std::string_view text, Binding &binding) {
    std::size_t position = 0;
    int minimum = -1;
    int maximum = 1;
    if (position < text.size() && text[position] == '+') {
        minimum = 0;
        ++position;
    } else if (position < text.size() && text[position] == '-') {
        maximum = 0;
        ++position;
    }
    if (position >= text.size())
        return false;
    const char type = text[position++];
    if (!parse_number(text, position, binding.index))
        return false;
    if (type == 'a') {
        binding.type = nk::core::gamepad::InputType::axis;
        binding.axis_scale = 2.f / static_cast<float>(maximum - minimum);
        binding.axis_offset = static_cast<float>(-(maximum + minimum));
        if (position < text.size() && text[position] == '~') {
            binding.axis_scale = -binding.axis_scale;
            binding.axis_offset = -binding.axis_offset;
            ++position;
        }
    } else if (type == 'b') {
        binding.type = nk::core::gamepad::InputType::button;
    } else if (type == 'h') {
        if (position >= text.size() || text[position++] != '.')
            return false;
        std::uint16_t mask = 0;
        if (!parse_number(text, position, mask) || mask > 0xff)
            return false;
        binding.type = nk::core::gamepad::InputType::hat;
        binding.hat_mask = static_cast<std::uint8_t>(mask);
    } else {
        return false;
    }
    return position == text.size();
}

bool hexadecimal_guid(std::string_view guid) {
    if (guid.size() != 32)
        return false;
    return std::all_of(guid.begin(), guid.end(), [](char value) {
        return std::isxdigit(static_cast<unsigned char>(value)) != 0;
    });
}

float binding_value(const Binding &binding, const std::vector<float> &axes,
                    const std::vector<std::uint8_t> &buttons,
                    const std::vector<std::uint8_t> &hats, bool &valid) {
    valid = true;
    switch (binding.type) {
    case nk::core::gamepad::InputType::axis:
        if (binding.index < axes.size())
            return std::clamp(axes[binding.index] * binding.axis_scale + binding.axis_offset,
                              -1.f, 1.f);
        break;
    case nk::core::gamepad::InputType::button:
        if (binding.index < buttons.size())
            return buttons[binding.index] ? 1.f : -1.f;
        break;
    case nk::core::gamepad::InputType::hat:
        if (binding.index < hats.size())
            return (hats[binding.index] & binding.hat_mask) ? 1.f : -1.f;
        break;
    case nk::core::gamepad::InputType::none:
        valid = false;
        return 0.f;
    }
    valid = false;
    return 0.f;
}
} // namespace

namespace nk::core::gamepad {

bool parse_mapping(std::string_view text, Mapping &mapping) {
    const auto first = text.find(',');
    if (first == std::string_view::npos || !hexadecimal_guid(text.substr(0, first)))
        return false;
    const auto second = text.find(',', first + 1);
    if (second == std::string_view::npos || second == first + 1)
        return false;

    Mapping parsed;
    parsed.guid = std::string(text.substr(0, first));
    std::transform(parsed.guid.begin(), parsed.guid.end(), parsed.guid.begin(), [](char value) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
    });
    parsed.name = std::string(text.substr(first + 1, second - first - 1));

    static const std::unordered_map<std::string_view, std::size_t> button_fields = {
        {"a", NK_GAMEPAD_BUTTON_A},
        {"b", NK_GAMEPAD_BUTTON_B},
        {"x", NK_GAMEPAD_BUTTON_X},
        {"y", NK_GAMEPAD_BUTTON_Y},
        {"leftshoulder", NK_GAMEPAD_BUTTON_LEFT_BUMPER},
        {"rightshoulder", NK_GAMEPAD_BUTTON_RIGHT_BUMPER},
        {"back", NK_GAMEPAD_BUTTON_BACK},
        {"start", NK_GAMEPAD_BUTTON_START},
        {"guide", NK_GAMEPAD_BUTTON_GUIDE},
        {"leftstick", NK_GAMEPAD_BUTTON_LEFT_THUMB},
        {"rightstick", NK_GAMEPAD_BUTTON_RIGHT_THUMB},
        {"dpup", NK_GAMEPAD_BUTTON_DPAD_UP},
        {"dpright", NK_GAMEPAD_BUTTON_DPAD_RIGHT},
        {"dpdown", NK_GAMEPAD_BUTTON_DPAD_DOWN},
        {"dpleft", NK_GAMEPAD_BUTTON_DPAD_LEFT},
    };
    static const std::unordered_map<std::string_view, std::size_t> axis_fields = {
        {"leftx", NK_GAMEPAD_AXIS_LEFT_X},
        {"lefty", NK_GAMEPAD_AXIS_LEFT_Y},
        {"rightx", NK_GAMEPAD_AXIS_RIGHT_X},
        {"righty", NK_GAMEPAD_AXIS_RIGHT_Y},
        {"lefttrigger", NK_GAMEPAD_AXIS_LEFT_TRIGGER},
        {"righttrigger", NK_GAMEPAD_AXIS_RIGHT_TRIGGER},
    };

    std::size_t position = second + 1;
    while (position < text.size()) {
        auto end = text.find(',', position);
        if (end == std::string_view::npos)
            end = text.size();
        const auto field = text.substr(position, end - position);
        const auto colon = field.find(':');
        if (colon == std::string_view::npos)
            return false;
        const auto key = field.substr(0, colon);
        const auto value = field.substr(colon + 1);
        if (key == "platform") {
            parsed.platform = std::string(value);
            position = end + 1;
            continue;
        }
        if (key == "type" || key == "crc") {
            position = end + 1;
            continue;
        }
        Binding binding;
        if (!parse_binding(value, binding))
            return false;
        if (const auto found = button_fields.find(key); found != button_fields.end())
            parsed.buttons[found->second] = binding;
        else if (const auto found = axis_fields.find(key); found != axis_fields.end())
            parsed.axes[found->second] = binding;
        else
            return false;
        position = end + 1;
    }
    mapping = std::move(parsed);
    return true;
}

bool apply_mapping(const Mapping &mapping, const std::vector<float> &axes,
                   const std::vector<std::uint8_t> &buttons,
                   const std::vector<std::uint8_t> &hats, nk_gamepad_state &state) {
    for (std::size_t index = 0; index < mapping.buttons.size(); ++index) {
        const auto &binding = mapping.buttons[index];
        bool valid = false;
        const float value = binding_value(binding, axes, buttons, hats, valid);
        if (!valid && binding.type != InputType::none)
            return false;
        if (binding.type == InputType::axis) {
            state.buttons[index] =
                (binding.axis_offset < 0.f ||
                 (binding.axis_offset == 0.f && binding.axis_scale > 0.f))
                    ? value >= 0.f
                    : value <= 0.f;
        } else if (valid) {
            state.buttons[index] = value > 0.f;
        }
    }
    for (std::size_t index = 0; index < mapping.axes.size(); ++index) {
        bool valid = false;
        const float value = binding_value(mapping.axes[index], axes, buttons, hats, valid);
        if (!valid && mapping.axes[index].type != InputType::none)
            return false;
        state.axes[index] = valid ? value : 0.f;
    }
    return true;
}

} // namespace nk::core::gamepad
