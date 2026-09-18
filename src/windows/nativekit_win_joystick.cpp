#include "nativekit_joystick.h"

#include "core/error.hpp"
#include "core/gamepad_events.hpp"
#include "core/haptics_internal.hpp"
#include "core/handle_registry.hpp"
#include "core/runtime.hpp"
#include "nativekit_time.h"
#include "windows/joystick.hpp"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <xinput.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

struct WinJoystick final : nk::core::Resource {
    DWORD index = 0;
    nk_handle handle = NK_INVALID_HANDLE;
    std::string name;
    std::string guid;
    std::array<float, NK_GAMEPAD_AXIS_COUNT> axes{};
    std::array<std::uint8_t, 10> buttons{};
    std::array<std::uint8_t, NK_GAMEPAD_BUTTON_COUNT> gamepad_buttons{};
    std::array<std::uint8_t, 1> hats{};
};

std::array<std::shared_ptr<WinJoystick>, XUSER_MAX_COUNT> devices;
std::unordered_map<nk_handle, std::uint64_t> rumble_deadlines;

template <typename T> std::vector<std::byte> bytes_of(const T &value) {
    const auto *first = reinterpret_cast<const std::byte *>(&value);
    return {first, first + sizeof(value)};
}

void emit(nk_event_kind kind, nk_handle source) {
    nk::core::QueuedEvent event;
    event.kind = kind;
    event.source = source;
    nk::core::push_event(std::move(event));
}

template <typename Payload>
void emit_input(nk_event_kind kind, nk_handle source, const Payload &payload) {
    nk::core::QueuedEvent event;
    event.kind = kind;
    event.source = source;
    event.data = bytes_of(payload);
    nk::core::push_event(std::move(event));
}

float stick_value(SHORT value) {
    if (value < 0)
        return std::max(-1.f, static_cast<float>(value) / 32768.f);
    return std::min(1.f, static_cast<float>(value) / 32767.f);
}

float trigger_value(BYTE value) {
    return static_cast<float>(value) / 255.f * 2.f - 1.f;
}

std::array<float, NK_GAMEPAD_AXIS_COUNT> axes_from(const XINPUT_GAMEPAD &pad) {
    return {stick_value(pad.sThumbLX),       stick_value(pad.sThumbLY),
            stick_value(pad.sThumbRX),       stick_value(pad.sThumbRY),
            trigger_value(pad.bLeftTrigger), trigger_value(pad.bRightTrigger)};
}

std::array<std::uint8_t, 10> buttons_from(const XINPUT_GAMEPAD &pad) {
    return {static_cast<std::uint8_t>((pad.wButtons & XINPUT_GAMEPAD_A) != 0),
            static_cast<std::uint8_t>((pad.wButtons & XINPUT_GAMEPAD_B) != 0),
            static_cast<std::uint8_t>((pad.wButtons & XINPUT_GAMEPAD_X) != 0),
            static_cast<std::uint8_t>((pad.wButtons & XINPUT_GAMEPAD_Y) != 0),
            static_cast<std::uint8_t>((pad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0),
            static_cast<std::uint8_t>((pad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0),
            static_cast<std::uint8_t>((pad.wButtons & XINPUT_GAMEPAD_BACK) != 0),
            static_cast<std::uint8_t>((pad.wButtons & XINPUT_GAMEPAD_START) != 0),
            static_cast<std::uint8_t>((pad.wButtons & XINPUT_GAMEPAD_LEFT_THUMB) != 0),
            static_cast<std::uint8_t>((pad.wButtons & XINPUT_GAMEPAD_RIGHT_THUMB) != 0)};
}

std::array<std::uint8_t, NK_GAMEPAD_BUTTON_COUNT> gamepad_buttons_from(const XINPUT_GAMEPAD &pad) {
    std::array<std::uint8_t, NK_GAMEPAD_BUTTON_COUNT> result{};
    const auto raw = buttons_from(pad);
    std::copy(raw.begin(), raw.end(), result.begin());
    result[NK_GAMEPAD_BUTTON_DPAD_UP] =
        static_cast<std::uint8_t>((pad.wButtons & XINPUT_GAMEPAD_DPAD_UP) != 0);
    result[NK_GAMEPAD_BUTTON_DPAD_RIGHT] =
        static_cast<std::uint8_t>((pad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) != 0);
    result[NK_GAMEPAD_BUTTON_DPAD_DOWN] =
        static_cast<std::uint8_t>((pad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) != 0);
    result[NK_GAMEPAD_BUTTON_DPAD_LEFT] =
        static_cast<std::uint8_t>((pad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) != 0);
    return result;
}

std::uint8_t hat_from(const XINPUT_GAMEPAD &pad) {
    std::uint8_t result = NK_JOYSTICK_HAT_CENTERED;
    if (pad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT)
        result |= NK_JOYSTICK_HAT_LEFT;
    if (pad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT)
        result |= NK_JOYSTICK_HAT_RIGHT;
    if (pad.wButtons & XINPUT_GAMEPAD_DPAD_UP)
        result |= NK_JOYSTICK_HAT_UP;
    if (pad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN)
        result |= NK_JOYSTICK_HAT_DOWN;
    return result;
}

void update(WinJoystick &device, const XINPUT_STATE &state) {
    const auto axes = axes_from(state.Gamepad);
    const auto buttons = buttons_from(state.Gamepad);
    const auto gamepad_buttons = gamepad_buttons_from(state.Gamepad);
    const auto hats = std::array<std::uint8_t, 1>{hat_from(state.Gamepad)};
    for (std::size_t index = 0; index < axes.size(); ++index) {
        if (device.axes[index] == axes[index])
            continue;
        device.axes[index] = axes[index];
        emit_input(NK_EVENT_JOYSTICK_AXIS, device.handle,
                   nk_joystick_axis_event{static_cast<std::uint32_t>(index), axes[index]});
    }
    for (std::size_t index = 0; index < buttons.size(); ++index) {
        if (device.buttons[index] == buttons[index])
            continue;
        device.buttons[index] = buttons[index];
        emit_input(NK_EVENT_JOYSTICK_BUTTON, device.handle,
                   nk_joystick_button_event{static_cast<std::uint32_t>(index), buttons[index]});
    }
    if (device.hats[0] != hats[0]) {
        device.hats[0] = hats[0];
        emit_input(NK_EVENT_JOYSTICK_HAT, device.handle, nk_joystick_hat_event{0, hats[0]});
    }
    device.gamepad_buttons = gamepad_buttons;
}

std::shared_ptr<WinJoystick> lookup(nk_handle handle) {
    return std::dynamic_pointer_cast<WinJoystick>(
        nk::core::handles().get(handle, nk::core::ResourceType::joystick));
}

void remove_device(std::size_t index) {
    auto &device = devices[index];
    if (!device)
        return;
    XINPUT_VIBRATION stop{};
    XInputSetState(device->index, &stop);
    rumble_deadlines.erase(device->handle);
    emit(NK_EVENT_JOYSTICK_DISCONNECTED, device->handle);
    nk::core::gamepad_events::disconnect(device->handle);
    nk::core::handles().erase(device->handle, nk::core::ResourceType::joystick);
    device.reset();
}

void add_device(std::size_t index, const XINPUT_STATE &state) {
    auto device = std::make_shared<WinJoystick>();
    device->index = static_cast<DWORD>(index);
    device->name = "XInput gamepad";
    device->guid = "030000005e0400008e02000001000000";
    device->handle = nk::core::handles().insert(nk::core::ResourceType::joystick, device);
    if (!device->handle)
        return;
    devices[index] = device;
    update(*device, state);
    nk::core::gamepad_events::update(device->handle, false);
    emit(NK_EVENT_JOYSTICK_CONNECTED, device->handle);
}

template <typename T>
nk_result copy_array(const T *source, std::size_t count, T *output, std::uint32_t *inout_count) {
    if (!inout_count) {
        nk::core::set_error("inout_count is required");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    if (count == 0) {
        *inout_count = 0;
        return NK_OK;
    }
    if (!output || *inout_count < count) {
        *inout_count = static_cast<std::uint32_t>(count);
        return NK_ERROR_BUFFER_TOO_SMALL;
    }
    std::copy(source, source + count, output);
    *inout_count = static_cast<std::uint32_t>(count);
    return NK_OK;
}

nk_result copy_string(const std::string &source, char *output, std::uint32_t *inout_size) {
    if (!inout_size) {
        nk::core::set_error("inout_size is required");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    const auto required = static_cast<std::uint32_t>(source.size() + 1);
    if (!output || *inout_size < required) {
        *inout_size = required;
        return NK_ERROR_BUFFER_TOO_SMALL;
    }
    std::memcpy(output, source.c_str(), required);
    *inout_size = required;
    return NK_OK;
}

nk_result enter_ui() {
    nk::core::clear_error();
    return nk::core::require_ui_thread();
}

template <typename Function> nk_result boundary(Function &&function) noexcept {
    try {
        return function();
    } catch (const std::bad_alloc &) {
        nk::core::set_error("out of memory while accessing joysticks");
        return NK_ERROR_OUT_OF_MEMORY;
    } catch (...) {
        nk::core::set_error("unexpected error while accessing joysticks");
        return NK_ERROR_UNKNOWN;
    }
}

} // namespace

namespace nk::windows_joystick {
void pump() noexcept {
    try {
        for (std::size_t index = 0; index < devices.size(); ++index) {
            XINPUT_STATE state{};
            const bool connected =
                XInputGetState(static_cast<DWORD>(index), &state) == ERROR_SUCCESS;
            if (!connected) {
                remove_device(index);
                continue;
            }
            if (devices[index]) {
                const auto deadline = rumble_deadlines.find(devices[index]->handle);
                if (deadline != rumble_deadlines.end() && nk_time_now_ns() >= deadline->second) {
                    XINPUT_VIBRATION stop{};
                    XInputSetState(static_cast<DWORD>(index), &stop);
                    rumble_deadlines.erase(deadline);
                }
            }
            if (!devices[index])
                add_device(index, state);
            else
                update(*devices[index], state);
            if (devices[index])
                nk::core::gamepad_events::update(devices[index]->handle, true);
        }
    } catch (...) {
    }
}

void shutdown() noexcept {
    try {
        for (std::size_t index = 0; index < devices.size(); ++index)
            remove_device(index);
        rumble_deadlines.clear();
    } catch (...) {
    }
}

bool standard_gamepad(nk_handle handle) noexcept {
    return lookup(handle) != nullptr;
}

nk_result standard_gamepad_state(nk_handle handle, nk_gamepad_state *out_state) noexcept {
    const auto device = lookup(handle);
    if (!device)
        return NK_ERROR_INVALID_HANDLE;
    if (!out_state || out_state->struct_size < sizeof(*out_state))
        return NK_ERROR_INVALID_ARGUMENT;
    const auto size = out_state->struct_size;
    *out_state = {};
    out_state->struct_size = size;
    std::copy(device->axes.begin(), device->axes.end(), out_state->axes);
    std::copy(device->gamepad_buttons.begin(), device->gamepad_buttons.end(), out_state->buttons);
    return NK_OK;
}
} // namespace nk::windows_joystick

namespace nk::core::haptics_backend {

nk_result vibrate(const nk_haptic_vibration &) noexcept { return NK_ERROR_UNSUPPORTED; }
nk_result stop_vibration() noexcept { return NK_ERROR_UNSUPPORTED; }

nk_result gamepad_rumble(nk_joystick handle,
                         const nk_gamepad_rumble_options &options) noexcept {
    const auto device = lookup(handle);
    if (!device)
        return NK_ERROR_INVALID_HANDLE;
    XINPUT_VIBRATION vibration{};
    vibration.wLeftMotorSpeed = static_cast<WORD>(options.low_frequency * 65535.0f + 0.5f);
    vibration.wRightMotorSpeed = static_cast<WORD>(options.high_frequency * 65535.0f + 0.5f);
    if (XInputSetState(device->index, &vibration) != ERROR_SUCCESS)
        return NK_ERROR_UNSUPPORTED;
    rumble_deadlines[handle] = nk_time_now_ns() +
                               static_cast<std::uint64_t>(options.duration_ms) * 1000000ULL;
    return NK_OK;
}

nk_result stop_gamepad_rumble(nk_joystick handle) noexcept {
    const auto device = lookup(handle);
    if (!device)
        return NK_ERROR_INVALID_HANDLE;
    XINPUT_VIBRATION vibration{};
    if (XInputSetState(device->index, &vibration) != ERROR_SUCCESS)
        return NK_ERROR_UNSUPPORTED;
    rumble_deadlines.erase(handle);
    return NK_OK;
}

} // namespace nk::core::haptics_backend

extern "C" {
nk_result NK_CALL nk_joystick_list(nk_handle *output, uint32_t *inout_count) {
    return boundary([&]() -> nk_result {
        if (const auto result = enter_ui(); result != NK_OK)
            return result;
        nk::windows_joystick::pump();
        std::array<nk_handle, XUSER_MAX_COUNT> handles{};
        std::size_t count = 0;
        for (const auto &device : devices)
            if (device)
                handles[count++] = device->handle;
        return copy_array(handles.data(), count, output, inout_count);
    });
}

nk_result NK_CALL nk_joystick_get_name(nk_handle handle, char *buffer, uint32_t *inout_size) {
    return boundary([&]() -> nk_result {
        if (const auto result = enter_ui(); result != NK_OK)
            return result;
        const auto device = lookup(handle);
        return device ? copy_string(device->name, buffer, inout_size) : NK_ERROR_INVALID_HANDLE;
    });
}

nk_result NK_CALL nk_joystick_get_guid(nk_handle handle, char *buffer, uint32_t *inout_size) {
    return boundary([&]() -> nk_result {
        if (const auto result = enter_ui(); result != NK_OK)
            return result;
        const auto device = lookup(handle);
        return device ? copy_string(device->guid, buffer, inout_size) : NK_ERROR_INVALID_HANDLE;
    });
}

nk_result NK_CALL nk_joystick_get_axes(nk_handle handle, float *axes, uint32_t *inout_count) {
    return boundary([&]() -> nk_result {
        if (const auto result = enter_ui(); result != NK_OK)
            return result;
        const auto device = lookup(handle);
        return device ? copy_array(device->axes.data(), device->axes.size(), axes, inout_count)
                      : NK_ERROR_INVALID_HANDLE;
    });
}

nk_result NK_CALL nk_joystick_get_buttons(nk_handle handle, uint8_t *buttons,
                                          uint32_t *inout_count) {
    return boundary([&]() -> nk_result {
        if (const auto result = enter_ui(); result != NK_OK)
            return result;
        const auto device = lookup(handle);
        return device ? copy_array(device->buttons.data(), device->buttons.size(), buttons,
                                   inout_count)
                      : NK_ERROR_INVALID_HANDLE;
    });
}

nk_result NK_CALL nk_joystick_get_hats(nk_handle handle, uint8_t *hats, uint32_t *inout_count) {
    return boundary([&]() -> nk_result {
        if (const auto result = enter_ui(); result != NK_OK)
            return result;
        const auto device = lookup(handle);
        return device ? copy_array(device->hats.data(), device->hats.size(), hats, inout_count)
                      : NK_ERROR_INVALID_HANDLE;
    });
}

nk_result NK_CALL nk_joystick_get_diagnostics(char *buffer, uint32_t *inout_size) {
    return boundary([&]() -> nk_result {
        if (const auto result = enter_ui(); result != NK_OK)
            return result;
        return copy_string({}, buffer, inout_size);
    });
}
}
