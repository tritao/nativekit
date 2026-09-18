#include "core/boundary.hpp"
#include "core/event_queue.hpp"
#include "core/frame_request.hpp"
#include "core/gamepad_mapping.hpp"
#include "core/gamepad_mappings_generated.hpp"
#include "core/graphics_frame_target.hpp"
#include "core/handle_registry.hpp"
#include "core/vulkan_internal.hpp"
#include "nativekit_accessibility.h"
#include "nativekit_joystick.h"
#include "nativekit_sensor.h"
#include "nativekit_system.h"
#include "nativekit_window.h"

#include <cassert>
#include <cstddef>
#include <cstring>
#include <memory>
#include <stdexcept>

namespace {
struct Dummy final : nk::core::Resource {};

static_assert(NK_ACCESSIBILITY_SCROLL_AREA == 12);
static_assert(NK_ACCESSIBILITY_DIALOG == 13);
static_assert(NK_ACCESSIBILITY_ALERT == 35);
static_assert(NK_ACCESSIBILITY_EXPANDED == (1u << 8));
static_assert(NK_ACCESSIBILITY_MODAL == (1u << 9));
static_assert(NK_ACCESSIBILITY_HAS_POPUP == (1u << 13));
static_assert(NK_ACCESSIBILITY_CAN_MOVE_PREVIOUS == (1u << 9));
static_assert(NK_ACCESSIBILITY_CAN_TOGGLE == (1u << 10));
static_assert(NK_ACCESSIBILITY_CAN_SCROLL_INTO_VIEW == (1u << 17));
static_assert(NK_ACCESSIBILITY_ACTION_MOVE_PREVIOUS == 11);
static_assert(NK_ACCESSIBILITY_ACTION_TOGGLE == 12);
static_assert(NK_ACCESSIBILITY_ACTION_SCROLL_INTO_VIEW == 19);
static_assert(offsetof(nk_accessibility_node, set_size) ==
              offsetof(nk_accessibility_node, selection_end) + sizeof(uint32_t));
static_assert(offsetof(nk_accessibility_node, orientation) >
              offsetof(nk_accessibility_node, hierarchy_level));
} // namespace

int main() {
    nk_accessibility_node accessibility_node{};
    accessibility_node.struct_size = sizeof(accessibility_node);
    accessibility_node.role = NK_ACCESSIBILITY_COLLECTION_ITEM;
    accessibility_node.set_size = 10000;
    accessibility_node.position_in_set = 4231;
    accessibility_node.row_index = NK_ACCESSIBILITY_INDEX_NONE;
    accessibility_node.column_index = NK_ACCESSIBILITY_INDEX_NONE;
    accessibility_node.orientation = NK_ACCESSIBILITY_ORIENTATION_VERTICAL;
    assert(accessibility_node.struct_size == sizeof(accessibility_node));
    assert(accessibility_node.position_in_set <= accessibility_node.set_size);
    assert(accessibility_node.row_index == NK_ACCESSIBILITY_INDEX_NONE);
    assert(accessibility_node.orientation == NK_ACCESSIBILITY_ORIENTATION_VERTICAL);
    nk_accessibility_update invalid_update{};
    invalid_update.struct_size = sizeof(invalid_update);
    const uint8_t malformed_removed_ids[] = {1, 2, 3};
    assert(nk_surface_accessibility_update_with_removed_ids(
               NK_INVALID_HANDLE, &invalid_update, malformed_removed_ids,
               sizeof(malformed_removed_ids)) == NK_ERROR_INVALID_ARGUMENT);

    nk_surface_frame_target old_frame_target{};
    old_frame_target.struct_size = nk::core::surface_frame_target_v1_size;
    old_frame_target.native_device = 0xfeedbeef;
    assert(nk::core::surface_frame_target_output_valid(&old_frame_target));
    nk_surface_frame_target frame_target{};
    frame_target.api = NK_GRAPHICS_OPENGL;
    frame_target.width = 640;
    frame_target.height = 480;
    nk::core::write_surface_frame_target(&old_frame_target, frame_target);
    assert(old_frame_target.api == NK_GRAPHICS_OPENGL);
    assert(old_frame_target.width == 640 && old_frame_target.height == 480);
    assert(old_frame_target.native_device == 0xfeedbeef);
    old_frame_target.struct_size = nk::core::surface_frame_target_v1_size - 1;
    assert(!nk::core::surface_frame_target_output_valid(&old_frame_target));

    nk::core::FrameRequestState frame_requests;
    assert(frame_requests.continuous());
    assert(frame_requests.should_draw());
    frame_requests.set_continuous(false);
    assert(!frame_requests.pending());
    assert(!frame_requests.should_draw());
    frame_requests.request();
    frame_requests.request();
    assert(frame_requests.pending());
    assert(frame_requests.should_draw());
    frame_requests.begin_frame();
    assert(!frame_requests.pending());
    assert(!frame_requests.should_draw());
    /* A request recorded while a frame renders schedules the next frame. */
    frame_requests.begin_frame();
    frame_requests.request();
    assert(frame_requests.should_draw());
    frame_requests.set_continuous(true);
    assert(frame_requests.should_draw());

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

    nk::core::EventQueue orientation_queue(1);
    const nk_orientation_event portrait{
        sizeof(nk_orientation_event), NK_ORIENTATION_PORTRAIT, 0, {0, 0}};
    const nk_orientation_event landscape{
        sizeof(nk_orientation_event), NK_ORIENTATION_LANDSCAPE_RIGHT, 0, {0, 0}};
    auto queue_orientation = [&](const nk_orientation_event &payload) {
        nk::core::QueuedEvent item;
        item.kind = NK_EVENT_DISPLAY_ORIENTATION_CHANGED;
        item.source = first;
        const auto *payload_begin = reinterpret_cast<const std::byte *>(&payload);
        item.data.assign(payload_begin, payload_begin + sizeof(payload));
        assert(orientation_queue.push(std::move(item)) == NK_OK);
    };
    queue_orientation(portrait);
    queue_orientation(landscape);
    event = {};
    event.struct_size = sizeof(event);
    assert(orientation_queue.poll(event) == NK_OK);
    assert(event.kind == NK_EVENT_DISPLAY_ORIENTATION_CHANGED);
    assert(static_cast<const nk_orientation_event *>(event.data)->orientation ==
           NK_ORIENTATION_LANDSCAPE_RIGHT);
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
    nk::core::EventQueue sensor_queue(2);
    auto queue_sensor = [&](nk_sensor sensor, std::uint64_t sequence) {
        nk_sensor_sample sample{sizeof(sample),
                                NK_SENSOR_ACCELEROMETER,
                                NK_SENSOR_ACCURACY_HIGH,
                                NK_SENSOR_COORDINATE_DEVICE,
                                100 + sequence,
                                sequence,
                                {static_cast<float>(sequence), 0, 0, 0},
                                {0, 0}};
        nk::core::QueuedEvent item;
        item.kind = NK_EVENT_SENSOR_UPDATE;
        item.source = sensor;
        const auto *payload_begin = reinterpret_cast<const std::byte *>(&sample);
        item.data.assign(payload_begin, payload_begin + sizeof(sample));
        assert(sensor_queue.push(std::move(item)) == NK_OK);
    };
    queue_sensor(11, 1);
    queue_sensor(12, 1);
    queue_sensor(11, 2);
    event = {};
    event.struct_size = sizeof(event);
    assert(sensor_queue.poll(event) == NK_OK);
    assert(event.source == 11);
    assert(static_cast<const nk_sensor_sample *>(event.data)->sequence == 2);
    nk_event_release(&event);
    event.struct_size = sizeof(event);
    assert(sensor_queue.poll(event) == NK_OK);
    assert(event.source == 12);
    nk_event_release(&event);
    return 0;
}
