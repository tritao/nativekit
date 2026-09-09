#include "nativekit_gamepad.h"

#include "core/boundary.hpp"
#include "core/error.hpp"
#include "core/gamepad_mapping.hpp"
#include "core/gamepad_mappings_generated.hpp"
#include "core/gamepad_events.hpp"
#include "core/runtime.hpp"
#include "nativekit_joystick.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace {
using nk::core::gamepad::Mapping;

struct StoredMapping {
    Mapping mapping;
    nk_gamepad_mapping_source source = NK_GAMEPAD_MAPPING_BUILT_IN;
};

std::mutex mappings_mutex;
std::vector<StoredMapping> mappings;
std::once_flag builtin_once;
nk_gamepad_options gamepad_options{sizeof(nk_gamepad_options), 0.f, 0.f, 0, 0, {0, 0}};
std::unordered_map<nk_handle, nk_gamepad_state> event_states;

void load_builtins() {
    std::lock_guard lock(mappings_mutex);
    for (const auto *text : nk_builtin_gamepad_mappings) {
        Mapping mapping;
        if (nk::core::gamepad::parse_mapping(text, mapping))
            mappings.push_back({std::move(mapping), NK_GAMEPAD_MAPPING_BUILT_IN});
    }
}

void ensure_builtins() {
    std::call_once(builtin_once, load_builtins);
}

nk_result read_guid(nk_handle joystick, std::string &guid) {
    std::uint32_t size = 0;
    auto result = nk_joystick_get_guid(joystick, nullptr, &size);
    if (result != NK_ERROR_BUFFER_TOO_SMALL)
        return result;
    std::vector<char> buffer(size);
    result = nk_joystick_get_guid(joystick, buffer.data(), &size);
    if (result == NK_OK)
        guid.assign(buffer.data());
    return result;
}

const char *current_platform() {
#if defined(__ANDROID__)
    return "Android";
#elif defined(_WIN32)
    return "Windows";
#elif defined(__APPLE__)
    return "Mac OS X";
#elif defined(__linux__)
    return "Linux";
#else
    return "";
#endif
}

bool applicable(const Mapping &mapping) {
    return mapping.platform.empty() || mapping.platform == current_platform();
}

std::optional<StoredMapping> find_mapping(std::string guid) {
    std::transform(guid.begin(), guid.end(), guid.begin(), [](char value) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
    });
    ensure_builtins();
    std::lock_guard lock(mappings_mutex);
    const auto found =
        std::find_if(mappings.rbegin(), mappings.rend(), [&](const StoredMapping &item) {
            return item.mapping.guid == guid;
        });
    if (found == mappings.rend())
        return std::nullopt;
    return *found;
}

nk_result mapping_for(nk_handle joystick, StoredMapping &mapping) {
    std::string guid;
    if (const auto result = read_guid(joystick, guid); result != NK_OK)
        return result;
    const auto found = find_mapping(std::move(guid));
    if (!found) {
        nk::core::set_error("the joystick has no gamepad mapping");
        return NK_ERROR_UNSUPPORTED;
    }
    mapping = *found;
    return NK_OK;
}

void store_application_mapping(Mapping mapping) {
    const auto found = std::find_if(mappings.rbegin(), mappings.rend(),
                                    [&](const StoredMapping &item) {
                                        return item.mapping.guid == mapping.guid;
                                    });
    StoredMapping stored{std::move(mapping), NK_GAMEPAD_MAPPING_APPLICATION};
    if (found == mappings.rend())
        mappings.push_back(std::move(stored));
    else
        *found = std::move(stored);
}

template <typename T, typename Function>
nk_result read_array(nk_handle joystick, std::vector<T> &values, Function function) {
    std::uint32_t count = 0;
    auto result = function(joystick, nullptr, &count);
    if (result != NK_ERROR_BUFFER_TOO_SMALL && result != NK_OK)
        return result;
    values.resize(count);
    if (count == 0)
        return NK_OK;
    return function(joystick, values.data(), &count);
}

nk_result copy_string(const std::string &source, char *buffer, std::uint32_t *inout_size) {
    if (!inout_size) {
        nk::core::set_error("inout_size is required");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    const auto required = static_cast<std::uint32_t>(source.size() + 1);
    if (!buffer || *inout_size < required) {
        *inout_size = required;
        return NK_ERROR_BUFFER_TOO_SMALL;
    }
    std::memcpy(buffer, source.c_str(), required);
    *inout_size = required;
    return NK_OK;
}

template <typename Payload>
void emit_state_event(nk_event_kind kind, nk_handle source, const Payload &payload) {
    nk::core::QueuedEvent event;
    event.kind = kind;
    event.source = source;
    const auto *begin = reinterpret_cast<const std::byte *>(&payload);
    event.data.assign(begin, begin + sizeof(payload));
    nk::core::push_event(std::move(event));
}
} // namespace

extern "C" {

nk_result NK_CALL nk_gamepad_add_mapping(const char *text) {
    return nk::core::result_boundary("unexpected error while adding a gamepad mapping",
                                     [&]() -> nk_result {
                                         nk::core::clear_error();
                                         if (const auto result = nk::core::require_ui_thread();
                                             result != NK_OK)
                                             return result;
                                         if (!text) {
                                             nk::core::set_error("mapping is required");
                                             return NK_ERROR_INVALID_ARGUMENT;
                                         }
                                         Mapping mapping;
                                         if (!nk::core::gamepad::parse_mapping(text, mapping)) {
                                             nk::core::set_error("invalid gamepad mapping");
                                             return NK_ERROR_INVALID_ARGUMENT;
                                         }
                                         if (!applicable(mapping)) {
                                             nk::core::set_error(
                                                 "gamepad mapping targets another platform");
                                             return NK_ERROR_UNSUPPORTED;
                                         }
                                         ensure_builtins();
                                         std::lock_guard lock(mappings_mutex);
                                         store_application_mapping(std::move(mapping));
                                         event_states.clear();
                                         return NK_OK;
                                     });
}

nk_result NK_CALL nk_gamepad_add_mappings(const char *database, uint32_t *out_added) {
    return nk::core::result_boundary("unexpected error while adding gamepad mappings",
                                     [&]() -> nk_result {
                                         nk::core::clear_error();
                                         if (const auto result = nk::core::require_ui_thread();
                                             result != NK_OK)
                                             return result;
                                         if (!database || !out_added) {
                                             nk::core::set_error(
                                                 "database and out_added are required");
                                             return NK_ERROR_INVALID_ARGUMENT;
                                         }
                                         std::vector<Mapping> parsed;
                                         std::string_view input(database);
                                         std::size_t position = 0;
                                         while (position < input.size()) {
                                             auto end = input.find('\n', position);
                                             if (end == std::string_view::npos)
                                                 end = input.size();
                                             auto line = input.substr(position, end - position);
                                             if (!line.empty() && line.back() == '\r')
                                                 line.remove_suffix(1);
                                             if (!line.empty() && line.front() != '#') {
                                                 Mapping mapping;
                                                 if (!nk::core::gamepad::parse_mapping(line,
                                                                                       mapping)) {
                                                     nk::core::set_error(
                                                         "invalid line in gamepad mapping database");
                                                     return NK_ERROR_INVALID_ARGUMENT;
                                                 }
                                                 if (applicable(mapping))
                                                     parsed.push_back(std::move(mapping));
                                             }
                                             position = end + 1;
                                         }
                                         ensure_builtins();
                                         std::lock_guard lock(mappings_mutex);
                                         for (auto &mapping : parsed)
                                             store_application_mapping(std::move(mapping));
                                         event_states.clear();
                                         *out_added = static_cast<std::uint32_t>(parsed.size());
                                         return NK_OK;
                                     });
}

nk_result NK_CALL nk_gamepad_is_mapped(nk_handle joystick, uint32_t *out_mapped) {
    return nk::core::result_boundary("unexpected error while checking a gamepad mapping",
                                     [&]() -> nk_result {
                                         nk::core::clear_error();
                                         if (!out_mapped) {
                                             nk::core::set_error("out_mapped is required");
                                             return NK_ERROR_INVALID_ARGUMENT;
                                         }
                                         std::string guid;
                                         if (const auto result = read_guid(joystick, guid);
                                             result != NK_OK)
                                             return result;
                                         *out_mapped = find_mapping(std::move(guid)) ? 1u : 0u;
                                         return NK_OK;
                                     });
}

nk_result NK_CALL
nk_gamepad_get_mapping_source(nk_handle joystick, nk_gamepad_mapping_source *out_source) {
    return nk::core::result_boundary("unexpected error while reading gamepad mapping source",
                                     [&]() -> nk_result {
                                         nk::core::clear_error();
                                         if (!out_source) {
                                             nk::core::set_error("out_source is required");
                                             return NK_ERROR_INVALID_ARGUMENT;
                                         }
                                         StoredMapping mapping;
                                         if (const auto result = mapping_for(joystick, mapping);
                                             result != NK_OK)
                                             return result;
                                         *out_source = mapping.source;
                                         return NK_OK;
                                     });
}

nk_result NK_CALL nk_gamepad_get_builtin_database_revision(char *buffer,
                                                           uint32_t *inout_size) {
    return nk::core::result_boundary("unexpected error while reading gamepad database revision",
                                     [&]() -> nk_result {
                                         nk::core::clear_error();
                                         return copy_string(nk_builtin_gamepad_database_revision,
                                                            buffer, inout_size);
                                     });
}

nk_result NK_CALL nk_gamepad_get_name(nk_handle joystick, char *buffer,
                                      uint32_t *inout_size) {
    return nk::core::result_boundary("unexpected error while reading a gamepad name",
                                     [&]() -> nk_result {
                                         nk::core::clear_error();
                                         StoredMapping mapping;
                                         if (const auto result = mapping_for(joystick, mapping);
                                             result != NK_OK)
                                             return result;
                                         return copy_string(mapping.mapping.name, buffer,
                                                            inout_size);
                                     });
}

nk_result NK_CALL nk_gamepad_get_state(nk_handle joystick, nk_gamepad_state *out_state) {
    return nk::core::result_boundary("unexpected error while reading gamepad state",
                                     [&]() -> nk_result {
                                         nk::core::clear_error();
                                         if (!out_state ||
                                             out_state->struct_size < sizeof(nk_gamepad_state)) {
                                             nk::core::set_error(
                                                 "nk_gamepad_state is missing or too small");
                                             return NK_ERROR_INVALID_ARGUMENT;
                                         }
                                         StoredMapping mapping;
                                         if (const auto result = mapping_for(joystick, mapping);
                                             result != NK_OK)
                                             return result;
                                         std::vector<float> axes;
                                         std::vector<std::uint8_t> buttons;
                                         std::vector<std::uint8_t> hats;
                                         if (const auto result = read_array<float>(
                                                 joystick, axes, nk_joystick_get_axes);
                                             result != NK_OK)
                                             return result;
                                         if (const auto result = read_array<std::uint8_t>(
                                                 joystick, buttons, nk_joystick_get_buttons);
                                             result != NK_OK)
                                             return result;
                                         if (const auto result = read_array<std::uint8_t>(
                                                 joystick, hats, nk_joystick_get_hats);
                                             result != NK_OK)
                                             return result;
                                         const auto size = out_state->struct_size;
                                         *out_state = {};
                                         out_state->struct_size = size;
                                         if (!nk::core::gamepad::apply_mapping(
                                                 mapping.mapping, axes, buttons, hats,
                                                 *out_state)) {
                                             nk::core::set_error(
                                                 "gamepad mapping references unavailable input");
                                             return NK_ERROR_UNSUPPORTED;
                                         }
                                         nk::core::gamepad::normalize_state(
                                             *out_state, gamepad_options.stick_dead_zone,
                                             gamepad_options.trigger_dead_zone,
                                             gamepad_options.flags);
                                         return NK_OK;
                                     });
}

nk_result NK_CALL nk_gamepad_set_options(const nk_gamepad_options *options) {
    return nk::core::result_boundary("unexpected error while setting gamepad options",
                                     [&]() -> nk_result {
                                         nk::core::clear_error();
                                         if (const auto result = nk::core::require_ui_thread();
                                             result != NK_OK)
                                             return result;
                                         if (!options ||
                                             options->struct_size < sizeof(nk_gamepad_options) ||
                                             options->stick_dead_zone < 0.f ||
                                             options->stick_dead_zone >= 1.f ||
                                             options->trigger_dead_zone < 0.f ||
                                             options->trigger_dead_zone >= 1.f ||
                                             (options->flags & ~NK_GAMEPAD_TRIGGER_ZERO_TO_ONE)) {
                                             nk::core::set_error("invalid gamepad options");
                                             return NK_ERROR_INVALID_ARGUMENT;
                                         }
                                         gamepad_options = *options;
                                         event_states.clear();
                                         return NK_OK;
                                     });
}

nk_result NK_CALL nk_gamepad_get_options(nk_gamepad_options *out_options) {
    return nk::core::result_boundary("unexpected error while reading gamepad options",
                                     [&]() -> nk_result {
                                         nk::core::clear_error();
                                         if (const auto result = nk::core::require_ui_thread();
                                             result != NK_OK)
                                             return result;
                                         if (!out_options || out_options->struct_size <
                                                                 sizeof(nk_gamepad_options)) {
                                             nk::core::set_error(
                                                 "nk_gamepad_options is missing or too small");
                                             return NK_ERROR_INVALID_ARGUMENT;
                                         }
                                         const auto size = out_options->struct_size;
                                         *out_options = gamepad_options;
                                         out_options->struct_size = size;
                                         return NK_OK;
                                     });
}
}

namespace nk::core::gamepad_events {
void update(nk_handle joystick, bool emit_changes) noexcept {
    try {
        nk_gamepad_state current{};
        current.struct_size = sizeof(current);
        if (nk_gamepad_get_state(joystick, &current) != NK_OK) {
            event_states.erase(joystick);
            nk::core::clear_error();
            return;
        }
        const auto found = event_states.find(joystick);
        if (found == event_states.end()) {
            event_states.emplace(joystick, current);
            return;
        }
        if (emit_changes) {
            for (std::uint32_t index = 0; index < NK_GAMEPAD_BUTTON_COUNT; ++index) {
                if (found->second.buttons[index] == current.buttons[index])
                    continue;
                const nk_gamepad_button_event payload{index, current.buttons[index]};
                emit_state_event(NK_EVENT_GAMEPAD_BUTTON, joystick, payload);
            }
            for (std::uint32_t index = 0; index < NK_GAMEPAD_AXIS_COUNT; ++index) {
                if (found->second.axes[index] == current.axes[index])
                    continue;
                const nk_gamepad_axis_event payload{index, current.axes[index]};
                emit_state_event(NK_EVENT_GAMEPAD_AXIS, joystick, payload);
            }
        }
        found->second = current;
    } catch (...) {
    }
}

void disconnect(nk_handle joystick) noexcept {
    event_states.erase(joystick);
}

void reset() noexcept {
    event_states.clear();
}
} // namespace nk::core::gamepad_events
