#include "core/event_queue.hpp"

#include <cstring>
#include <memory>
#include <new>
#include <utility>

namespace nk::core {

namespace {
bool is_terminal_request_event(const QueuedEvent &event) {
    if (event.request_id == NK_INVALID_REQUEST_ID)
        return false;
    return event.kind == NK_EVENT_DIALOG_PATHS_COMPLETE ||
           event.kind == NK_EVENT_DIALOG_RESOURCES_COMPLETE ||
           event.kind == NK_EVENT_DIALOG_MESSAGE_COMPLETE ||
           event.kind == NK_EVENT_WEBVIEW_EVAL_COMPLETE ||
           event.kind == NK_EVENT_CLIPBOARD_TEXT_COMPLETE ||
           event.kind == NK_EVENT_CLIPBOARD_FILES_COMPLETE ||
           event.kind == NK_EVENT_CLIPBOARD_RESOURCES_COMPLETE ||
           event.kind == NK_EVENT_RESOURCE_DATA_COMPLETE ||
           event.kind == NK_EVENT_NOTIFICATION_DELIVERED ||
           event.kind == NK_EVENT_NOTIFICATION_FAILED;
}

bool is_coalescible(nk_event_kind kind) {
    return kind == NK_EVENT_WINDOW_RESIZE || kind == NK_EVENT_WINDOW_FRAMEBUFFER_RESIZE ||
           kind == NK_EVENT_WINDOW_MOVE || kind == NK_EVENT_POINTER_MOVE ||
           kind == NK_EVENT_SURFACE_RESIZE || kind == NK_EVENT_JOYSTICK_AXIS ||
           kind == NK_EVENT_GAMEPAD_AXIS;
}

bool same_coalescing_target(const QueuedEvent &first, const QueuedEvent &second) {
    if (first.kind != second.kind || first.source != second.source)
        return false;
    if (first.kind != NK_EVENT_JOYSTICK_AXIS && first.kind != NK_EVENT_GAMEPAD_AXIS)
        return true;
    if (first.data.size() < sizeof(std::uint32_t) || second.data.size() < sizeof(std::uint32_t))
        return false;
    std::uint32_t first_axis = 0;
    std::uint32_t second_axis = 0;
    std::memcpy(&first_axis, first.data.data(), sizeof(first_axis));
    std::memcpy(&second_axis, second.data.data(), sizeof(second_axis));
    return first_axis == second_axis;
}
} // namespace

EventQueue::EventQueue(std::size_t capacity) : capacity_(capacity) {}

nk_result EventQueue::push(QueuedEvent event) {
    std::lock_guard lock(mutex_);
    if (is_coalescible(event.kind) && !queue_.empty()) {
        auto &tail = queue_.back();
        if (same_coalescing_target(tail, event)) {
            tail = std::move(event);
            return NK_OK;
        }
    }
    if (queue_.size() >= capacity_ && !is_terminal_request_event(event))
        return NK_ERROR_QUEUE_FULL;
    queue_.push_back(std::move(event));
    return NK_OK;
}

nk_result EventQueue::poll(nk_event &output) {
    std::lock_guard lock(mutex_);
    if (queue_.empty()) {
        output.kind = NK_EVENT_NONE;
        return NK_OK;
    }

    const QueuedEvent &event = queue_.front();
    std::byte *payload = nullptr;
    if (!event.data.empty()) {
        payload = new (std::nothrow) std::byte[event.data.size()];
        if (!payload)
            return NK_ERROR_OUT_OF_MEMORY;
        std::memcpy(payload, event.data.data(), event.data.size());
    }
    output.kind = event.kind;
    output.source = event.source;
    output.flags = event.flags;
    output.request_id = event.request_id;
    output.result = event.result;
    output.data_count = event.data_count;
    output.data = payload;
    output.data_size = event.data.size();
    queue_.pop_front();
    return NK_OK;
}

bool EventQueue::empty() {
    std::lock_guard lock(mutex_);
    return queue_.empty();
}

void EventQueue::clear() {
    std::lock_guard lock(mutex_);
    queue_.clear();
}

} // namespace nk::core
