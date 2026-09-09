#include "core/event_queue.hpp"

#include <cstring>
#include <memory>
#include <new>
#include <utility>

namespace nk::core {

EventQueue::EventQueue(std::size_t capacity) : capacity_(capacity) {}

nk_result EventQueue::push(QueuedEvent event) {
    std::lock_guard lock(mutex_);
    if (event.kind == NK_EVENT_WINDOW_RESIZE && !queue_.empty()) {
        auto& tail = queue_.back();
        if (tail.kind == event.kind && tail.source == event.source) {
            tail = std::move(event);
            return NK_OK;
        }
    }
    if (queue_.size() >= capacity_) return NK_ERROR_QUEUE_FULL;
    queue_.push_back(std::move(event));
    return NK_OK;
}

nk_result EventQueue::poll(nk_event& output) {
    std::lock_guard lock(mutex_);
    if (queue_.empty()) {
        output.kind = NK_EVENT_NONE;
        return NK_OK;
    }

    const QueuedEvent& event = queue_.front();
    std::byte* payload = nullptr;
    if (!event.data.empty()) {
        payload = new (std::nothrow) std::byte[event.data.size()];
        if (!payload) return NK_ERROR_OUT_OF_MEMORY;
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

void EventQueue::clear() {
    std::lock_guard lock(mutex_);
    queue_.clear();
}

}
