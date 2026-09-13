#include "core/boundary.hpp"
#include "core/event_queue.hpp"
#include "core/gamepad_mapping.hpp"
#include "core/gamepad_mappings_generated.hpp"
#include "core/handle_registry.hpp"
#include "core/vulkan_internal.hpp"
#include "nativekit_joystick.h"
#include "nativekit_window.h"

#include <cassert>
#include <cstddef>
#include <cstring>
#include <memory>
#include <stdexcept>

namespace {
struct Dummy final : nk::core::Resource {};
} // namespace

int main() {
    assert(std::strcmp(nk::core::vulkan::platform_extension(NK_NATIVE_WINDOW_X11),
                       "VK_KHR_xlib_surface") == 0);
    assert(std::strcmp(nk::core::vulkan::platform_extension(NK_NATIVE_WINDOW_WAYLAND),
                       "VK_KHR_wayland_surface") == 0);
    assert(nk::core::vulkan::platform_extension(NK_NATIVE_WINDOW_COCOA) == nullptr);
    assert(nk::core::result_boundary("unexpected boundary exception", []() -> nk_result {
               throw std::bad_alloc{};
           }) == NK_ERROR_OUT_OF_MEMORY);
    assert(nk::core::result_boundary("unexpected boundary exception", []() -> nk_result {
               throw std::runtime_error("test");
           }) == NK_ERROR_UNKNOWN);
    bool callback_returned = false;
    nk::core::callback_boundary([&] {
        callback_returned = true;
        throw std::runtime_error("test");
    });
    assert(callback_returned);
    assert(
        !nk::core::callback_boundary_or(false, []() -> bool { throw std::runtime_error("test"); }));
    assert(nk::core::callback_boundary_or(false, [] { return true; }));

    nk::core::gamepad::Mapping mapping;
    assert(nk::core::gamepad::parse_mapping(
        "03000000112200003344000055660000,Test Pad,a:b0,b:+a1,x:-a1~,dpup:h0.1,"
        "leftx:a0,lefty:a0~,lefttrigger:+a2,righttrigger:b1,platform:Linux,",
        mapping));
    assert(mapping.name == "Test Pad");
    assert(mapping.platform == "Linux");
    nk_gamepad_state gamepad_state{};
    gamepad_state.struct_size = sizeof(gamepad_state);
    assert(nk::core::gamepad::apply_mapping(mapping, {0.25f, 0.75f, 0.5f}, {1, 0}, {1},
                                            gamepad_state));
    assert(gamepad_state.buttons[NK_GAMEPAD_BUTTON_A] == 1);
    assert(gamepad_state.buttons[NK_GAMEPAD_BUTTON_B] == 1);
    assert(gamepad_state.buttons[NK_GAMEPAD_BUTTON_X] == 0);
    assert(gamepad_state.buttons[NK_GAMEPAD_BUTTON_DPAD_UP] == 1);
    assert(gamepad_state.axes[NK_GAMEPAD_AXIS_LEFT_X] == 0.25f);
    assert(gamepad_state.axes[NK_GAMEPAD_AXIS_LEFT_Y] == -0.25f);
    assert(gamepad_state.axes[NK_GAMEPAD_AXIS_LEFT_TRIGGER] == 0.f);
    assert(gamepad_state.axes[NK_GAMEPAD_AXIS_RIGHT_TRIGGER] == -1.f);
    gamepad_state.axes[NK_GAMEPAD_AXIS_LEFT_X] = 0.1f;
    gamepad_state.axes[NK_GAMEPAD_AXIS_LEFT_Y] = 0.f;
    gamepad_state.axes[NK_GAMEPAD_AXIS_RIGHT_X] = 0.6f;
    gamepad_state.axes[NK_GAMEPAD_AXIS_RIGHT_Y] = 0.f;
    gamepad_state.axes[NK_GAMEPAD_AXIS_LEFT_TRIGGER] = 0.f;
    gamepad_state.axes[NK_GAMEPAD_AXIS_RIGHT_TRIGGER] = -1.f;
    nk::core::gamepad::normalize_state(gamepad_state, 0.2f, 0.1f, NK_GAMEPAD_TRIGGER_ZERO_TO_ONE);
    assert(gamepad_state.axes[NK_GAMEPAD_AXIS_LEFT_X] == 0.f);
    assert(gamepad_state.axes[NK_GAMEPAD_AXIS_RIGHT_X] > 0.499f);
    assert(gamepad_state.axes[NK_GAMEPAD_AXIS_RIGHT_X] < 0.501f);
    assert(gamepad_state.axes[NK_GAMEPAD_AXIS_LEFT_TRIGGER] > 0.44f);
    assert(gamepad_state.axes[NK_GAMEPAD_AXIS_LEFT_TRIGGER] < 0.45f);
    assert(gamepad_state.axes[NK_GAMEPAD_AXIS_RIGHT_TRIGGER] == 0.f);
    assert(!nk::core::gamepad::parse_mapping("not-a-guid,Pad,a:b0", mapping));
    assert(!nk::core::gamepad::parse_mapping("03000000112200003344000055660000,Pad,a:q0", mapping));
    bool has_xbox = false;
    bool has_playstation = false;
    bool has_switch = false;
    bool has_steam = false;
    std::size_t builtin_count = 0;
    for (const auto *text : nk_builtin_gamepad_mappings) {
        nk::core::gamepad::Mapping builtin;
        assert(nk::core::gamepad::parse_mapping(text, builtin));
        assert(builtin.platform == "Linux");
        has_xbox = has_xbox || builtin.name.find("Xbox") != std::string::npos;
        has_playstation = has_playstation || builtin.name.find("PlayStation") != std::string::npos;
        has_switch = has_switch || builtin.name.find("Switch") != std::string::npos;
        has_steam = has_steam || builtin.name.find("Steam") != std::string::npos;
        ++builtin_count;
    }
    assert(builtin_count > 700);
    assert(has_xbox && has_playstation && has_switch && has_steam);

    nk::core::HandleRegistry handles;
    const auto first = handles.insert(nk::core::ResourceType::window, std::make_shared<Dummy>());
    assert(first != NK_INVALID_HANDLE);
    assert(handles.get(first, nk::core::ResourceType::window));
    assert(!handles.get(first, nk::core::ResourceType::webview));
    assert(handles.erase(first, nk::core::ResourceType::window));
    assert(!handles.get(first, nk::core::ResourceType::window));
    const auto second = handles.insert(nk::core::ResourceType::window, std::make_shared<Dummy>());
    assert(second != first);

    nk::core::EventQueue queue(1);
    nk::core::QueuedEvent queued;
    queued.kind = NK_EVENT_WEBVIEW_MESSAGE;
    const char payload[] = "hello";
    const auto *begin = reinterpret_cast<const std::byte *>(payload);
    queued.data.assign(begin, begin + sizeof(payload) - 1);
    assert(queue.push(std::move(queued)) == NK_OK);
    assert(queue.push({}) == NK_ERROR_QUEUE_FULL);
    nk::core::QueuedEvent terminal;
    terminal.kind = NK_EVENT_WEBVIEW_EVAL_COMPLETE;
    terminal.request_id = 42;
    terminal.result = NK_ERROR_INVALID_REQUEST;
    assert(queue.push(std::move(terminal)) == NK_OK);
    nk::core::QueuedEvent notification_terminal;
    notification_terminal.kind = NK_EVENT_NOTIFICATION_FAILED;
    notification_terminal.request_id = 43;
    assert(queue.push(std::move(notification_terminal)) == NK_OK);

    nk_event event{};
    event.struct_size = sizeof(event);
    assert(queue.poll(event) == NK_OK);
    assert(event.kind == NK_EVENT_WEBVIEW_MESSAGE);
    assert(event.data_size == 5);
    assert(std::memcmp(event.data, "hello", 5) == 0);
    nk_event_release(&event);
    event.struct_size = sizeof(event);
    assert(queue.poll(event) == NK_OK);
    assert(event.kind == NK_EVENT_WEBVIEW_EVAL_COMPLETE);
    assert(event.request_id == 42);
    assert(event.result == NK_ERROR_INVALID_REQUEST);
    nk_event_release(&event);

    nk::core::EventQueue resize_queue(1);
    nk::core::QueuedEvent first_resize;
    first_resize.kind = NK_EVENT_WINDOW_RESIZE;
    first_resize.source = first;
    nk::core::QueuedEvent latest_resize = first_resize;
    const nk_window_resize_event latest_size{800, 600};
    const auto *size_begin = reinterpret_cast<const std::byte *>(&latest_size);
    latest_resize.data.assign(size_begin, size_begin + sizeof(latest_size));
    assert(resize_queue.push(std::move(first_resize)) == NK_OK);
    assert(resize_queue.push(std::move(latest_resize)) == NK_OK);
    event = {};
    event.struct_size = sizeof(event);
    assert(resize_queue.poll(event) == NK_OK);
    assert(event.data_size == sizeof(latest_size));
    assert(std::memcmp(event.data, &latest_size, sizeof(latest_size)) == 0);
    nk_event_release(&event);

    nk::core::EventQueue motion_queue(1);
    nk::core::QueuedEvent first_motion;
    first_motion.kind = NK_EVENT_POINTER_MOVE;
    first_motion.source = first;
    nk::core::QueuedEvent latest_motion = first_motion;
    assert(motion_queue.push(std::move(first_motion)) == NK_OK);
    assert(motion_queue.push(std::move(latest_motion)) == NK_OK);
    event = {};
    event.struct_size = sizeof(event);
    assert(motion_queue.poll(event) == NK_OK);
    assert(event.kind == NK_EVENT_POINTER_MOVE);
    nk_event_release(&event);

    nk::core::EventQueue axis_queue(3);
    const nk_joystick_axis_event axis_zero{0, 0.25f};
    const nk_joystick_axis_event axis_zero_latest{0, 0.75f};
    const nk_joystick_axis_event axis_one{1, -0.5f};
    auto queue_axis = [&](const nk_joystick_axis_event &payload) {
        nk::core::QueuedEvent item;
        item.kind = NK_EVENT_JOYSTICK_AXIS;
        item.source = first;
        const auto *payload_begin = reinterpret_cast<const std::byte *>(&payload);
        item.data.assign(payload_begin, payload_begin + sizeof(payload));
        assert(axis_queue.push(std::move(item)) == NK_OK);
    };
    queue_axis(axis_zero);
    queue_axis(axis_zero_latest);
    queue_axis(axis_one);
    event = {};
    event.struct_size = sizeof(event);
    assert(axis_queue.poll(event) == NK_OK);
    assert(static_cast<const nk_joystick_axis_event *>(event.data)->value == 0.75f);
    nk_event_release(&event);
    event.struct_size = sizeof(event);
    assert(axis_queue.poll(event) == NK_OK);
    assert(static_cast<const nk_joystick_axis_event *>(event.data)->axis == 1);
    nk_event_release(&event);
    return 0;
}
