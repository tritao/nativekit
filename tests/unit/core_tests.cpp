#include "core/boundary.hpp"
#include "core/event_queue.hpp"
#include "core/frame_request.hpp"
#include "core/gamepad_mapping.hpp"
#include "core/gamepad_mappings_generated.hpp"
#include "core/graphics_frame_target.hpp"
#include "core/handle_registry.hpp"
#include "core/vulkan_internal.hpp"
#include "nativekit_accessibility.h"
#include "nativekit_clipboard.h"
#include "nativekit_file_watch.h"
#include "nativekit_joystick.h"
#include "nativekit_sensor.h"
#include "nativekit_system.h"
#include "nativekit_task.h"
#include "nativekit_window.h"

#include <cstddef>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

#define NK_CHECK(expression)                                                                       \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            std::fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #expression);  \
            std::abort();                                                                          \
        }                                                                                          \
    } while (false)

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
    NK_CHECK(accessibility_node.struct_size == sizeof(accessibility_node));
    NK_CHECK(accessibility_node.position_in_set <= accessibility_node.set_size);
    NK_CHECK(accessibility_node.row_index == NK_ACCESSIBILITY_INDEX_NONE);
    NK_CHECK(accessibility_node.orientation == NK_ACCESSIBILITY_ORIENTATION_VERTICAL);
    nk_accessibility_update invalid_update{};
    invalid_update.struct_size = sizeof(invalid_update);
    const uint8_t malformed_removed_ids[] = {1, 2, 3};
    NK_CHECK(nk_surface_accessibility_update_with_removed_ids(
                 NK_INVALID_HANDLE, &invalid_update, malformed_removed_ids,
                 sizeof(malformed_removed_ids)) == NK_ERROR_INVALID_ARGUMENT);

    nk_surface_frame_target old_frame_target{};
    old_frame_target.struct_size = nk::core::surface_frame_target_v1_size;
    old_frame_target.native_device = 0xfeedbeef;
    NK_CHECK(nk::core::surface_frame_target_output_valid(&old_frame_target));
    nk_surface_frame_target frame_target{};
    frame_target.api = NK_GRAPHICS_OPENGL;
    frame_target.width = 640;
    frame_target.height = 480;
    nk::core::write_surface_frame_target(&old_frame_target, frame_target);
    NK_CHECK(old_frame_target.api == NK_GRAPHICS_OPENGL);
    NK_CHECK(old_frame_target.width == 640 && old_frame_target.height == 480);
    NK_CHECK(old_frame_target.native_device == 0xfeedbeef);
    old_frame_target.struct_size = nk::core::surface_frame_target_v1_size - 1;
    NK_CHECK(!nk::core::surface_frame_target_output_valid(&old_frame_target));

    nk::core::FrameRequestState frame_requests;
    NK_CHECK(frame_requests.continuous());
    NK_CHECK(frame_requests.should_draw());
    frame_requests.set_continuous(false);
    NK_CHECK(!frame_requests.pending());
    NK_CHECK(!frame_requests.should_draw());
    frame_requests.request();
    frame_requests.request();
    NK_CHECK(frame_requests.pending());
    NK_CHECK(frame_requests.should_draw());
    frame_requests.begin_frame();
    NK_CHECK(!frame_requests.pending());
    NK_CHECK(!frame_requests.should_draw());
    /* A request recorded while a frame renders schedules the next frame. */
    frame_requests.begin_frame();
    frame_requests.request();
    NK_CHECK(frame_requests.should_draw());
    frame_requests.set_continuous(true);
    NK_CHECK(frame_requests.should_draw());

    NK_CHECK(std::strcmp(nk::core::vulkan::platform_extension(NK_NATIVE_WINDOW_X11),
                         "VK_KHR_xlib_surface") == 0);
    NK_CHECK(std::strcmp(nk::core::vulkan::platform_extension(NK_NATIVE_WINDOW_WAYLAND),
                         "VK_KHR_wayland_surface") == 0);
    NK_CHECK(nk::core::vulkan::platform_extension(NK_NATIVE_WINDOW_COCOA) == nullptr);
    NK_CHECK(nk::core::result_boundary("direct result propagation", []() -> nk_result {
                 return NK_ERROR_INVALID_ARGUMENT;
             }) == NK_ERROR_INVALID_ARGUMENT);
    bool callback_returned = false;
    nk::core::callback_boundary([&] { callback_returned = true; });
    NK_CHECK(callback_returned);
    NK_CHECK(!nk::core::callback_boundary_or(false, []() -> bool { return false; }));
    NK_CHECK(nk::core::callback_boundary_or(false, [] { return true; }));

    nk::core::gamepad::Mapping mapping;
    NK_CHECK(nk::core::gamepad::parse_mapping(
        "03000000112200003344000055660000,Test Pad,a:b0,b:+a1,x:-a1~,dpup:h0.1,"
        "leftx:a0,lefty:a0~,lefttrigger:+a2,righttrigger:b1,platform:Linux,",
        mapping));
    NK_CHECK(mapping.name == "Test Pad");
    NK_CHECK(mapping.platform == "Linux");
    nk_gamepad_state gamepad_state{};
    gamepad_state.struct_size = sizeof(gamepad_state);
    NK_CHECK(nk::core::gamepad::apply_mapping(mapping, {0.25f, 0.75f, 0.5f}, {1, 0}, {1},
                                              gamepad_state));
    NK_CHECK(gamepad_state.buttons[NK_GAMEPAD_BUTTON_A] == 1);
    NK_CHECK(gamepad_state.buttons[NK_GAMEPAD_BUTTON_B] == 1);
    NK_CHECK(gamepad_state.buttons[NK_GAMEPAD_BUTTON_X] == 0);
    NK_CHECK(gamepad_state.buttons[NK_GAMEPAD_BUTTON_DPAD_UP] == 1);
    NK_CHECK(gamepad_state.axes[NK_GAMEPAD_AXIS_LEFT_X] == 0.25f);
    NK_CHECK(gamepad_state.axes[NK_GAMEPAD_AXIS_LEFT_Y] == -0.25f);
    NK_CHECK(gamepad_state.axes[NK_GAMEPAD_AXIS_LEFT_TRIGGER] == 0.f);
    NK_CHECK(gamepad_state.axes[NK_GAMEPAD_AXIS_RIGHT_TRIGGER] == -1.f);
    gamepad_state.axes[NK_GAMEPAD_AXIS_LEFT_X] = 0.1f;
    gamepad_state.axes[NK_GAMEPAD_AXIS_LEFT_Y] = 0.f;
    gamepad_state.axes[NK_GAMEPAD_AXIS_RIGHT_X] = 0.6f;
    gamepad_state.axes[NK_GAMEPAD_AXIS_RIGHT_Y] = 0.f;
    gamepad_state.axes[NK_GAMEPAD_AXIS_LEFT_TRIGGER] = 0.f;
    gamepad_state.axes[NK_GAMEPAD_AXIS_RIGHT_TRIGGER] = -1.f;
    nk::core::gamepad::normalize_state(gamepad_state, 0.2f, 0.1f, NK_GAMEPAD_TRIGGER_ZERO_TO_ONE);
    NK_CHECK(gamepad_state.axes[NK_GAMEPAD_AXIS_LEFT_X] == 0.f);
    NK_CHECK(gamepad_state.axes[NK_GAMEPAD_AXIS_RIGHT_X] > 0.499f);
    NK_CHECK(gamepad_state.axes[NK_GAMEPAD_AXIS_RIGHT_X] < 0.501f);
    NK_CHECK(gamepad_state.axes[NK_GAMEPAD_AXIS_LEFT_TRIGGER] > 0.44f);
    NK_CHECK(gamepad_state.axes[NK_GAMEPAD_AXIS_LEFT_TRIGGER] < 0.45f);
    NK_CHECK(gamepad_state.axes[NK_GAMEPAD_AXIS_RIGHT_TRIGGER] == 0.f);
    NK_CHECK(!nk::core::gamepad::parse_mapping("not-a-guid,Pad,a:b0", mapping));
    NK_CHECK(
        !nk::core::gamepad::parse_mapping("03000000112200003344000055660000,Pad,a:q0", mapping));
    bool has_xbox = false;
    bool has_playstation = false;
    bool has_switch = false;
    bool has_steam = false;
    std::size_t builtin_count = 0;
    for (const auto *text : nk_builtin_gamepad_mappings) {
        nk::core::gamepad::Mapping builtin;
        NK_CHECK(nk::core::gamepad::parse_mapping(text, builtin));
        NK_CHECK(builtin.platform == "Linux");
        has_xbox = has_xbox || builtin.name.find("Xbox") != std::string::npos;
        has_playstation = has_playstation || builtin.name.find("PlayStation") != std::string::npos;
        has_switch = has_switch || builtin.name.find("Switch") != std::string::npos;
        has_steam = has_steam || builtin.name.find("Steam") != std::string::npos;
        ++builtin_count;
    }
    NK_CHECK(builtin_count > 700);
    NK_CHECK(has_xbox && has_playstation && has_switch && has_steam);

    nk::core::HandleRegistry handles;
    const auto first = handles.insert(nk::core::ResourceType::window, std::make_shared<Dummy>());
    NK_CHECK(first != NK_INVALID_HANDLE);
    NK_CHECK(handles.get(first, nk::core::ResourceType::window));
    NK_CHECK(!handles.get(first, nk::core::ResourceType::webview));
    NK_CHECK(handles.erase(first, nk::core::ResourceType::window));
    NK_CHECK(!handles.get(first, nk::core::ResourceType::window));
    const auto second = handles.insert(nk::core::ResourceType::window, std::make_shared<Dummy>());
    NK_CHECK(second != first);

    nk::core::HandleRegistry generation_handles;
    const auto stale =
        generation_handles.insert(nk::core::ResourceType::window, std::make_shared<Dummy>());
    NK_CHECK(generation_handles.erase(stale, nk::core::ResourceType::window));
    for (int cycle = 0; cycle != 4094; ++cycle) {
        const auto current =
            generation_handles.insert(nk::core::ResourceType::window, std::make_shared<Dummy>());
        NK_CHECK(current != NK_INVALID_HANDLE);
        NK_CHECK(current != stale);
        NK_CHECK(generation_handles.erase(current, nk::core::ResourceType::window));
    }
    NK_CHECK(!generation_handles.get(stale, nk::core::ResourceType::window));
    const auto after_exhaustion =
        generation_handles.insert(nk::core::ResourceType::window, std::make_shared<Dummy>());
    NK_CHECK(after_exhaustion != NK_INVALID_HANDLE);
    NK_CHECK(after_exhaustion != stale);
    NK_CHECK(!generation_handles.get(stale, nk::core::ResourceType::window));

    nk::core::EventQueue queue(1);
    nk::core::QueuedEvent queued;
    queued.kind = NK_EVENT_WEBVIEW_MESSAGE;
    const char payload[] = "hello";
    const auto *begin = reinterpret_cast<const std::byte *>(payload);
    queued.data.assign(begin, begin + sizeof(payload) - 1);
    NK_CHECK(queue.push(std::move(queued)) == NK_OK);
    NK_CHECK(queue.push({}) == NK_ERROR_QUEUE_FULL);
    nk::core::QueuedEvent terminal;
    terminal.kind = NK_EVENT_WEBVIEW_EVAL_COMPLETE;
    terminal.request_id = 42;
    terminal.result = NK_ERROR_INVALID_REQUEST;
    NK_CHECK(queue.push(std::move(terminal)) == NK_OK);
    nk::core::QueuedEvent notification_terminal;
    notification_terminal.kind = NK_EVENT_NOTIFICATION_FAILED;
    notification_terminal.request_id = 43;
    NK_CHECK(queue.push(std::move(notification_terminal)) == NK_OK);

    nk_event event{};

    nk::core::EventQueue readiness_queue(1);
    nk::core::QueuedEvent occupied;
    occupied.kind = NK_EVENT_WEBVIEW_MESSAGE;
    NK_CHECK(readiness_queue.push(std::move(occupied)) == NK_OK);
    nk::core::QueuedEvent readiness;
    readiness.kind = NK_EVENT_HTTP_DATA_AVAILABLE;
    readiness.source = first;
    readiness.request_id = 99;
    NK_CHECK(readiness_queue.push(std::move(readiness)) == NK_OK);
    NK_CHECK(!readiness_queue.empty());
    event = {};
    event.struct_size = sizeof(event);
    NK_CHECK(readiness_queue.poll(event) == NK_OK);
    NK_CHECK(event.kind == NK_EVENT_WEBVIEW_MESSAGE);
    nk_event_release(&event);
    event.struct_size = sizeof(event);
    NK_CHECK(readiness_queue.poll(event) == NK_OK);
    NK_CHECK(event.kind == NK_EVENT_HTTP_DATA_AVAILABLE);
    NK_CHECK(event.source == first);
    nk_event_release(&event);

    event.struct_size = sizeof(event);
    NK_CHECK(queue.poll(event) == NK_OK);
    NK_CHECK(event.kind == NK_EVENT_WEBVIEW_MESSAGE);
    NK_CHECK(event.data_size == 5);
    NK_CHECK(std::memcmp(event.data, "hello", 5) == 0);
    nk_event_release(&event);
    event.struct_size = sizeof(event);
    NK_CHECK(queue.poll(event) == NK_OK);
    NK_CHECK(event.kind == NK_EVENT_WEBVIEW_EVAL_COMPLETE);
    NK_CHECK(event.request_id == 42);
    NK_CHECK(event.result == NK_ERROR_INVALID_REQUEST);
    nk_event_release(&event);

    nk::core::EventQueue resize_queue(1);
    nk::core::QueuedEvent first_resize;
    first_resize.kind = NK_EVENT_WINDOW_RESIZE;
    first_resize.source = first;
    nk::core::QueuedEvent latest_resize = first_resize;
    const nk_window_resize_event latest_size{800, 600};
    const auto *size_begin = reinterpret_cast<const std::byte *>(&latest_size);
    latest_resize.data.assign(size_begin, size_begin + sizeof(latest_size));
    NK_CHECK(resize_queue.push(std::move(first_resize)) == NK_OK);
    NK_CHECK(resize_queue.push(std::move(latest_resize)) == NK_OK);
    event = {};
    event.struct_size = sizeof(event);
    NK_CHECK(resize_queue.poll(event) == NK_OK);
    NK_CHECK(event.data_size == sizeof(latest_size));
    NK_CHECK(std::memcmp(event.data, &latest_size, sizeof(latest_size)) == 0);
    nk_event_release(&event);

    nk::core::EventQueue motion_queue(1);
    nk::core::QueuedEvent first_motion;
    first_motion.kind = NK_EVENT_POINTER_MOVE;
    first_motion.source = first;
    nk::core::QueuedEvent latest_motion = first_motion;
    NK_CHECK(motion_queue.push(std::move(first_motion)) == NK_OK);
    NK_CHECK(motion_queue.push(std::move(latest_motion)) == NK_OK);
    event = {};
    event.struct_size = sizeof(event);
    NK_CHECK(motion_queue.poll(event) == NK_OK);
    NK_CHECK(event.kind == NK_EVENT_POINTER_MOVE);
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
        NK_CHECK(orientation_queue.push(std::move(item)) == NK_OK);
    };
    queue_orientation(portrait);
    queue_orientation(landscape);
    event = {};
    event.struct_size = sizeof(event);
    NK_CHECK(orientation_queue.poll(event) == NK_OK);
    NK_CHECK(event.kind == NK_EVENT_DISPLAY_ORIENTATION_CHANGED);
    NK_CHECK(static_cast<const nk_orientation_event *>(event.data)->orientation ==
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
        NK_CHECK(axis_queue.push(std::move(item)) == NK_OK);
    };
    queue_axis(axis_zero);
    queue_axis(axis_zero_latest);
    queue_axis(axis_one);
    event = {};
    event.struct_size = sizeof(event);
    NK_CHECK(axis_queue.poll(event) == NK_OK);
    NK_CHECK(static_cast<const nk_joystick_axis_event *>(event.data)->value == 0.75f);
    nk_event_release(&event);
    event.struct_size = sizeof(event);
    NK_CHECK(axis_queue.poll(event) == NK_OK);
    NK_CHECK(static_cast<const nk_joystick_axis_event *>(event.data)->axis == 1);
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
        NK_CHECK(sensor_queue.push(std::move(item)) == NK_OK);
    };
    queue_sensor(11, 1);
    queue_sensor(12, 1);
    queue_sensor(11, 2);
    event = {};
    event.struct_size = sizeof(event);
    NK_CHECK(sensor_queue.poll(event) == NK_OK);
    NK_CHECK(event.source == 11);
    NK_CHECK(static_cast<const nk_sensor_sample *>(event.data)->sequence == 2);
    nk_event_release(&event);
    event.struct_size = sizeof(event);
    NK_CHECK(sensor_queue.poll(event) == NK_OK);
    NK_CHECK(event.source == 12);
    nk_event_release(&event);

    nk::core::EventQueue task_progress_queue(2);
    nk::core::QueuedEvent task_progress_first;
    task_progress_first.kind = NK_EVENT_TASK_PROGRESS;
    task_progress_first.source = 77;
    task_progress_first.data = {std::byte{1}};
    nk::core::QueuedEvent task_progress_latest = task_progress_first;
    task_progress_latest.data = {std::byte{2}};
    nk::core::QueuedEvent task_progress_other;
    task_progress_other.kind = NK_EVENT_TASK_PROGRESS;
    task_progress_other.source = 78;
    task_progress_other.data = {std::byte{3}};
    assert(task_progress_queue.push(std::move(task_progress_first)) == NK_OK);
    assert(task_progress_queue.push(std::move(task_progress_other)) == NK_OK);
    assert(task_progress_queue.push(std::move(task_progress_latest)) == NK_OK);
    event = {};
    event.struct_size = sizeof(event);
    assert(task_progress_queue.poll(event) == NK_OK);
    assert(event.kind == NK_EVENT_TASK_PROGRESS && event.source == 77);
    assert(static_cast<const std::byte *>(event.data)[0] == std::byte{2});
    nk_event_release(&event);

    nk::core::EventQueue task_terminal_queue(1);
    nk::core::QueuedEvent task_ordinary;
    task_ordinary.kind = NK_EVENT_TASK_PROGRESS;
    task_ordinary.source = 88;
    assert(task_terminal_queue.push(std::move(task_ordinary)) == NK_OK);
    nk::core::QueuedEvent task_terminal;
    task_terminal.kind = NK_EVENT_TASK_COMPLETE;
    task_terminal.source = 88;
    assert(task_terminal_queue.push(std::move(task_terminal)) == NK_OK);
    event = {};
    event.struct_size = sizeof(event);
    assert(task_terminal_queue.poll(event) == NK_OK);
    nk_event_release(&event);
    event.struct_size = sizeof(event);
    assert(task_terminal_queue.poll(event) == NK_OK);
    assert(event.kind == NK_EVENT_TASK_COMPLETE);
    nk_event_release(&event);

    nk::core::EventQueue clipboard_queue(1);
    nk::core::QueuedEvent clipboard_first;
    clipboard_first.kind = NK_EVENT_CLIPBOARD_CHANGED;
    clipboard_first.source = first;
    const nk_clipboard_changed_event clipboard_payload{1, NK_CLIPBOARD_FORMAT_TEXT, 0};
    const auto *clipboard_begin = reinterpret_cast<const std::byte *>(&clipboard_payload);
    clipboard_first.data.assign(clipboard_begin, clipboard_begin + sizeof(clipboard_payload));
    nk::core::QueuedEvent clipboard_latest = clipboard_first;
    const nk_clipboard_changed_event clipboard_latest_payload{2, NK_CLIPBOARD_FORMAT_FILES, 0};
    const auto *clipboard_latest_begin =
        reinterpret_cast<const std::byte *>(&clipboard_latest_payload);
    clipboard_latest.data.assign(clipboard_latest_begin,
                                 clipboard_latest_begin + sizeof(clipboard_latest_payload));
    NK_CHECK(clipboard_queue.push(std::move(clipboard_first)) == NK_OK);
    NK_CHECK(clipboard_queue.push(std::move(clipboard_latest)) == NK_OK);
    event = {};
    event.struct_size = sizeof(event);
    NK_CHECK(clipboard_queue.poll(event) == NK_OK);
    NK_CHECK(static_cast<const nk_clipboard_changed_event *>(event.data)->sequence == 2);
    nk_event_release(&event);

    nk::core::EventQueue file_queue(1);
    nk::core::QueuedEvent file_first;
    file_first.kind = NK_EVENT_FILE_CHANGED;
    file_first.source = first;
    nk_file_changed_event file_header{
        NK_FILE_CHANGE_MODIFIED, NK_FILE_ITEM_FILE, 0, sizeof(nk_file_changed_event), 9, 0, 0};
    const char file_path[] = "/tmp/item";
    const auto *file_begin = reinterpret_cast<const std::byte *>(&file_header);
    file_first.data.assign(file_begin, file_begin + sizeof(file_header));
    file_first.data.insert(file_first.data.end(), reinterpret_cast<const std::byte *>(file_path),
                           reinterpret_cast<const std::byte *>(file_path) + sizeof(file_path));
    nk::core::QueuedEvent file_latest = file_first;
    NK_CHECK(file_queue.push(std::move(file_first)) == NK_OK);
    NK_CHECK(file_queue.push(std::move(file_latest)) == NK_OK);
    event = {};
    event.struct_size = sizeof(event);
    NK_CHECK(file_queue.poll(event) == NK_OK);
    NK_CHECK(event.kind == NK_EVENT_FILE_CHANGED);
    nk_event_release(&event);

    nk::core::EventQueue overflow_queue(1);
    nk::core::QueuedEvent ordinary;
    ordinary.kind = NK_EVENT_WEBVIEW_MESSAGE;
    NK_CHECK(overflow_queue.push(std::move(ordinary)) == NK_OK);
    nk::core::QueuedEvent overflow;
    overflow.kind = NK_EVENT_FILE_WATCH_OVERFLOW;
    NK_CHECK(overflow_queue.push(std::move(overflow)) == NK_OK);
    event = {};
    event.struct_size = sizeof(event);
    NK_CHECK(overflow_queue.poll(event) == NK_OK);
    NK_CHECK(event.kind == NK_EVENT_FILE_WATCH_OVERFLOW);
    nk_event_release(&event);
    nk::core::EventQueue empty_overflow_queue(0);
    nk::core::QueuedEvent empty_overflow;
    empty_overflow.kind = NK_EVENT_FILE_WATCH_OVERFLOW;
    NK_CHECK(empty_overflow_queue.push(std::move(empty_overflow)) == NK_OK);
    return 0;
}
