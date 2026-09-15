#import <GameController/GameController.h>

#include "nativekit_joystick.h"

#include "core/error.hpp"
#include "core/gamepad_events.hpp"
#include "core/handle_registry.hpp"
#include "core/runtime.hpp"
#include "macos/joystick.hpp"

#include <CoreFoundation/CoreFoundation.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

struct MacJoystick final : nk::core::Resource {
    CFTypeRef controller_ref = nullptr;
    nk_handle handle = NK_INVALID_HANDLE;
    std::string name;
    std::string guid;
    std::array<float, NK_GAMEPAD_AXIS_COUNT> axes{};
    std::array<std::uint8_t, NK_GAMEPAD_BUTTON_COUNT> buttons{};
    std::array<std::uint8_t, 1> hats{};

    ~MacJoystick() override {
        if (controller_ref)
            CFRelease(controller_ref);
    }

    GCController *controller() const { return (__bridge GCController *)controller_ref; }
};

std::unordered_map<void *, std::shared_ptr<MacJoystick>> devices;

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

std::string utf8(NSString *value) {
    if (!value)
        return {};
    const auto *text = value.UTF8String;
    return text ? text : "";
}

std::string controller_guid(GCController *controller) {
    const auto descriptor = utf8(controller.vendorName) + "\n" + utf8(controller.productCategory);
    auto hash = [&](std::uint64_t seed) {
        std::uint64_t value = seed;
        for (const unsigned char byte : descriptor) {
            value ^= byte;
            value *= UINT64_C(1099511628211);
        }
        return value;
    };
    char result[33]{};
    std::snprintf(result, sizeof(result), "%016llx%016llx",
                  static_cast<unsigned long long>(hash(UINT64_C(1469598103934665603))),
                  static_cast<unsigned long long>(hash(UINT64_C(1099511628211))));
    return result;
}

std::uint8_t hat_value(GCControllerDirectionPad *pad) {
    if (!pad)
        return NK_JOYSTICK_HAT_CENTERED;
    std::uint8_t result = NK_JOYSTICK_HAT_CENTERED;
    if (pad.up.pressed)
        result |= NK_JOYSTICK_HAT_UP;
    if (pad.right.pressed)
        result |= NK_JOYSTICK_HAT_RIGHT;
    if (pad.down.pressed)
        result |= NK_JOYSTICK_HAT_DOWN;
    if (pad.left.pressed)
        result |= NK_JOYSTICK_HAT_LEFT;
    return result;
}

std::array<float, NK_GAMEPAD_AXIS_COUNT> axes_from(GCController *controller) {
    std::array<float, NK_GAMEPAD_AXIS_COUNT> result{};
    if (GCExtendedGamepad *pad = controller.extendedGamepad) {
        result[NK_GAMEPAD_AXIS_LEFT_X] = pad.leftThumbstick.xAxis.value;
        result[NK_GAMEPAD_AXIS_LEFT_Y] = pad.leftThumbstick.yAxis.value;
        result[NK_GAMEPAD_AXIS_RIGHT_X] = pad.rightThumbstick.xAxis.value;
        result[NK_GAMEPAD_AXIS_RIGHT_Y] = pad.rightThumbstick.yAxis.value;
        result[NK_GAMEPAD_AXIS_LEFT_TRIGGER] = pad.leftTrigger.value * 2.f - 1.f;
        result[NK_GAMEPAD_AXIS_RIGHT_TRIGGER] = pad.rightTrigger.value * 2.f - 1.f;
    }
    return result;
}

std::array<std::uint8_t, NK_GAMEPAD_BUTTON_COUNT> buttons_from(GCController *controller) {
    std::array<std::uint8_t, NK_GAMEPAD_BUTTON_COUNT> result{};
    if (GCExtendedGamepad *pad = controller.extendedGamepad) {
        result[NK_GAMEPAD_BUTTON_A] = pad.buttonA.pressed;
        result[NK_GAMEPAD_BUTTON_B] = pad.buttonB.pressed;
        result[NK_GAMEPAD_BUTTON_X] = pad.buttonX.pressed;
        result[NK_GAMEPAD_BUTTON_Y] = pad.buttonY.pressed;
        result[NK_GAMEPAD_BUTTON_LEFT_BUMPER] = pad.leftShoulder.pressed;
        result[NK_GAMEPAD_BUTTON_RIGHT_BUMPER] = pad.rightShoulder.pressed;
        result[NK_GAMEPAD_BUTTON_BACK] = pad.buttonOptions.pressed;
        result[NK_GAMEPAD_BUTTON_START] = pad.buttonMenu.pressed;
        result[NK_GAMEPAD_BUTTON_GUIDE] = pad.buttonHome.pressed;
        result[NK_GAMEPAD_BUTTON_LEFT_THUMB] = pad.leftThumbstickButton.pressed;
        result[NK_GAMEPAD_BUTTON_RIGHT_THUMB] = pad.rightThumbstickButton.pressed;
        result[NK_GAMEPAD_BUTTON_DPAD_UP] = pad.dpad.up.pressed;
        result[NK_GAMEPAD_BUTTON_DPAD_RIGHT] = pad.dpad.right.pressed;
        result[NK_GAMEPAD_BUTTON_DPAD_DOWN] = pad.dpad.down.pressed;
        result[NK_GAMEPAD_BUTTON_DPAD_LEFT] = pad.dpad.left.pressed;
    } else if (GCMicroGamepad *pad = controller.microGamepad) {
        result[NK_GAMEPAD_BUTTON_A] = pad.buttonA.pressed;
        result[NK_GAMEPAD_BUTTON_B] = pad.buttonX.pressed;
        result[NK_GAMEPAD_BUTTON_DPAD_UP] = pad.dpad.up.pressed;
        result[NK_GAMEPAD_BUTTON_DPAD_RIGHT] = pad.dpad.right.pressed;
        result[NK_GAMEPAD_BUTTON_DPAD_DOWN] = pad.dpad.down.pressed;
        result[NK_GAMEPAD_BUTTON_DPAD_LEFT] = pad.dpad.left.pressed;
    }
    return result;
}

void update(MacJoystick &device) {
    GCController *controller = device.controller();
    const auto axes = axes_from(controller);
    const auto buttons = buttons_from(controller);
    GCControllerDirectionPad *pad =
        controller.extendedGamepad ? controller.extendedGamepad.dpad : controller.microGamepad.dpad;
    const auto hats = std::array<std::uint8_t, 1>{hat_value(pad)};
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
}

std::shared_ptr<MacJoystick> lookup(nk_handle handle) {
    return std::dynamic_pointer_cast<MacJoystick>(
        nk::core::handles().get(handle, nk::core::ResourceType::joystick));
}

void remove_device(void *key) {
    const auto found = devices.find(key);
    if (found == devices.end())
        return;
    const auto handle = found->second->handle;
    emit(NK_EVENT_JOYSTICK_DISCONNECTED, handle);
    nk::core::gamepad_events::disconnect(handle);
    nk::core::handles().erase(handle, nk::core::ResourceType::joystick);
    devices.erase(found);
}

void add_device(GCController *controller) {
    void *key = (__bridge void *)controller;
    if (devices.find(key) != devices.end())
        return;
    auto device = std::make_shared<MacJoystick>();
    device->controller_ref = (__bridge_retained CFTypeRef)controller;
    device->name = utf8(controller.vendorName);
    if (device->name.empty())
        device->name = "Apple game controller";
    device->guid = controller_guid(controller);
    device->handle = nk::core::handles().insert(nk::core::ResourceType::joystick, device);
    if (!device->handle)
        return;
    devices.emplace(key, device);
    update(*device);
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

namespace nk::macos_joystick {
void pump() noexcept {
    @autoreleasepool {
        try {
            GCController.shouldMonitorBackgroundEvents = YES;
            NSMutableSet *present = [NSMutableSet set];
            for (GCController *controller in GCController.controllers) {
                void *key = (__bridge void *)controller;
                [present addObject:[NSValue valueWithPointer:key]];
                if (devices.find(key) == devices.end())
                    add_device(controller);
                else
                    update(*devices[key]);
                if (devices.find(key) != devices.end())
                    nk::core::gamepad_events::update(devices[key]->handle, true);
            }
            std::vector<void *> removed;
            for (const auto &[key, device] : devices) {
                (void)device;
                if (![present containsObject:[NSValue valueWithPointer:key]])
                    removed.push_back(key);
            }
            for (void *key : removed)
                remove_device(key);
        } catch (...) {
        }
    }
}

void shutdown() noexcept {
    @autoreleasepool {
        try {
            std::vector<void *> keys;
            keys.reserve(devices.size());
            for (const auto &[key, device] : devices) {
                (void)device;
                keys.push_back(key);
            }
            for (void *key : keys)
                remove_device(key);
        } catch (...) {
        }
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
    std::copy(device->buttons.begin(), device->buttons.end(), out_state->buttons);
    return NK_OK;
}
} // namespace nk::macos_joystick

extern "C" {
nk_result NK_CALL nk_joystick_list(nk_handle *output, uint32_t *inout_count) {
    return boundary([&]() -> nk_result {
        if (const auto result = enter_ui(); result != NK_OK)
            return result;
        nk::macos_joystick::pump();
        std::vector<nk_handle> handles;
        handles.reserve(devices.size());
        for (const auto &[key, device] : devices) {
            (void)key;
            handles.push_back(device->handle);
        }
        std::sort(handles.begin(), handles.end());
        return copy_array(handles.data(), handles.size(), output, inout_count);
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
