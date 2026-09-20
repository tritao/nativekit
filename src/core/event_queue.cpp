#include "core/event_queue.hpp"

#include "nativekit_clipboard.h"
#include "nativekit_file_watch.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <new>
#include <utility>

namespace nk::core {

namespace {
bool is_terminal_request_event(const QueuedEvent &event) {
    if (event.kind == NK_EVENT_AUDIO_VOICE_COMPLETE || event.kind == NK_EVENT_AUDIO_VOICE_READY ||
        event.kind == NK_EVENT_AUDIO_VOICE_LOAD_FAILED || event.kind == NK_EVENT_AUDIO_CLIP_READY ||
        event.kind == NK_EVENT_AUDIO_CLIP_LOAD_FAILED ||
        event.kind == NK_EVENT_AUDIO_DEVICE_STARTED ||
        event.kind == NK_EVENT_AUDIO_DEVICE_STOPPED ||
        event.kind == NK_EVENT_AUDIO_DEVICE_REROUTED ||
        event.kind == NK_EVENT_AUDIO_DEVICE_INTERRUPTION_BEGAN ||
        event.kind == NK_EVENT_AUDIO_DEVICE_INTERRUPTION_ENDED ||
        event.kind == NK_EVENT_AUDIO_VOICE_STOLEN ||
        event.kind == NK_EVENT_AUDIO_VOICE_VIRTUALIZED ||
        event.kind == NK_EVENT_AUDIO_VOICE_RESUMED ||
        event.kind == NK_EVENT_AUDIO_VOICE_STREAM_FAILED || event.kind == NK_EVENT_TASK_COMPLETE ||
        event.kind == NK_EVENT_TASK_FAILED || event.kind == NK_EVENT_TASK_CANCELLED)
        return true;
    if (event.request_id == NK_INVALID_REQUEST_ID)
        return false;
    return event.kind == NK_EVENT_DIALOG_RESOURCES_COMPLETE ||
           event.kind == NK_EVENT_DIALOG_MESSAGE_COMPLETE ||
           event.kind == NK_EVENT_WEBVIEW_EVAL_COMPLETE ||
           event.kind == NK_EVENT_CLIPBOARD_TEXT_COMPLETE ||
           event.kind == NK_EVENT_CLIPBOARD_FILES_COMPLETE ||
           event.kind == NK_EVENT_CLIPBOARD_RESOURCES_COMPLETE ||
           event.kind == NK_EVENT_RESOURCE_DATA_COMPLETE ||
           event.kind == NK_EVENT_RESOURCE_CACHE_READY ||
           event.kind == NK_EVENT_RESOURCE_CACHE_LOAD_FAILED ||
           event.kind == NK_EVENT_SENSOR_PERMISSION_COMPLETE ||
           event.kind == NK_EVENT_HTTP_COMPLETE || event.kind == NK_EVENT_PLUGIN_COMPLETE ||
           event.kind == NK_EVENT_NOTIFICATION_DELIVERED ||
           event.kind == NK_EVENT_NOTIFICATION_FAILED;
}

bool is_coalescible(nk_event_kind kind) {
    return kind == NK_EVENT_WINDOW_RESIZE || kind == NK_EVENT_WINDOW_FRAMEBUFFER_RESIZE ||
           kind == NK_EVENT_WINDOW_MOVE || kind == NK_EVENT_POINTER_MOVE ||
           kind == NK_EVENT_SURFACE_RESIZE || kind == NK_EVENT_JOYSTICK_AXIS ||
           kind == NK_EVENT_GAMEPAD_AXIS || kind == NK_EVENT_DEVICE_ORIENTATION_CHANGED ||
           kind == NK_EVENT_DISPLAY_ORIENTATION_CHANGED || kind == NK_EVENT_SENSOR_UPDATE ||
           kind == NK_EVENT_CLIPBOARD_CHANGED || kind == NK_EVENT_FILE_CHANGED ||
           kind == NK_EVENT_TASK_PROGRESS;
}

bool is_persistent_readiness(nk_event_kind kind) {
    return kind == NK_EVENT_HTTP_HEADERS || kind == NK_EVENT_HTTP_DATA_AVAILABLE;
}

bool same_readiness_target(const QueuedEvent &first, const QueuedEvent &second) {
    return first.kind == second.kind && first.source == second.source &&
           first.request_id == second.request_id;
}

bool same_coalescing_target(const QueuedEvent &first, const QueuedEvent &second) {
    if (first.kind != second.kind || first.source != second.source)
        return false;
    if (first.kind == NK_EVENT_CLIPBOARD_CHANGED)
        return true;
    if (first.kind == NK_EVENT_FILE_CHANGED) {
        if (first.data.size() < sizeof(nk_file_changed_event) ||
            second.data.size() < sizeof(nk_file_changed_event))
            return false;
        nk_file_changed_event first_file{};
        nk_file_changed_event second_file{};
        std::memcpy(&first_file, first.data.data(), sizeof(first_file));
        std::memcpy(&second_file, second.data.data(), sizeof(second_file));
        if (first_file.change_type != NK_FILE_CHANGE_MODIFIED ||
            second_file.change_type != NK_FILE_CHANGE_MODIFIED ||
            first_file.path_length != second_file.path_length ||
            first_file.path_offset > first.data.size() ||
            second_file.path_offset > second.data.size() ||
            first_file.path_length > first.data.size() - first_file.path_offset ||
            second_file.path_length > second.data.size() - second_file.path_offset)
            return false;
        return std::memcmp(first.data.data() + first_file.path_offset,
                           second.data.data() + second_file.path_offset,
                           first_file.path_length) == 0;
    }
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

EventQueue::EventQueue(std::size_t capacity, std::size_t byte_capacity)
    : capacity_(capacity), byte_capacity_(byte_capacity) {}

void promote_deferred_readiness(std::deque<QueuedEvent> &queue, std::deque<QueuedEvent> &deferred,
                                std::size_t capacity) {
    /* A zero-capacity queue still has to be able to deliver a mandatory
     * event. It is represented as one over-capacity item until it is polled. */
    while ((queue.size() < capacity || queue.empty()) && !deferred.empty()) {
        queue.push_back(std::move(deferred.front()));
        deferred.pop_front();
    }
}

void promote_pending_overflow(std::deque<QueuedEvent> &queue, std::deque<QueuedEvent> &pending,
                              std::size_t capacity) {
    if (pending.empty() || (!queue.empty() && queue.size() >= capacity))
        return;
    queue.push_back(std::move(pending.front()));
    pending.pop_front();
}

void promote_readiness_for_request(std::deque<QueuedEvent> &queue,
                                   std::deque<QueuedEvent> &deferred, const QueuedEvent &terminal) {
    for (auto iterator = deferred.begin(); iterator != deferred.end();) {
        if (iterator->request_id != terminal.request_id || iterator->source != terminal.source) {
            ++iterator;
            continue;
        }
        queue.push_back(std::move(*iterator));
        iterator = deferred.erase(iterator);
    }
}

nk_result EventQueue::push(QueuedEvent event) {
    std::lock_guard lock(mutex_);
    const std::size_t event_bytes = event.data.size();
    const bool terminal = is_terminal_request_event(event);
    const bool readiness = is_persistent_readiness(event.kind);
    const bool overflow = event.kind == NK_EVENT_FILE_WATCH_OVERFLOW;
    /* Terminal outcomes, persistent readiness, and overflow recovery must not
     * disappear merely because unrelated payloads consumed the byte budget. */
    if (!terminal && !readiness && !overflow &&
        (event_bytes > byte_capacity_ || queued_bytes_ > byte_capacity_ - event_bytes))
        return NK_ERROR_QUEUE_FULL;
    if (is_coalescible(event.kind) && !queue_.empty()) {
        if (event.kind == NK_EVENT_SENSOR_UPDATE || event.kind == NK_EVENT_TASK_PROGRESS) {
            /* Sensor producers interleave several sources; each sensor gets
             * one pending coalesced record without changing existing input
             * event ordering rules for the other coalesced event kinds. */
            for (auto item = queue_.rbegin(); item != queue_.rend(); ++item) {
                if (same_coalescing_target(*item, event)) {
                    queued_bytes_ -= item->data.size();
                    *item = std::move(event);
                    queued_bytes_ += event_bytes;
                    return NK_OK;
                }
            }
        } else if (same_coalescing_target(queue_.back(), event)) {
            queued_bytes_ -= queue_.back().data.size();
            queue_.back() = std::move(event);
            queued_bytes_ += event_bytes;
            return NK_OK;
        }
    }
    if (queue_.size() >= capacity_ && !terminal) {
        if (event.kind == NK_EVENT_FILE_WATCH_OVERFLOW) {
            /* Overflow is a recovery instruction, not an ordinary best-effort
             * notification. Make room by discarding only a disposable item. */
            auto victim = std::find_if(queue_.begin(), queue_.end(), [](const QueuedEvent &queued) {
                return is_coalescible(queued.kind);
            });
            if (victim == queue_.end()) {
                victim = std::find_if(queue_.begin(), queue_.end(), [](const QueuedEvent &queued) {
                    return !is_terminal_request_event(queued) &&
                           !is_persistent_readiness(queued.kind);
                });
            }
            if (victim != queue_.end()) {
                queued_bytes_ -= victim->data.size();
                queue_.erase(victim);
            } else {
                /* Do not evict an accepted completion or readiness record.
                 * Coalesce overflow per watcher until polling makes room. */
                for (auto &pending : pending_file_watch_overflows_) {
                    if (pending.source == event.source) {
                        queued_bytes_ -= pending.data.size();
                        pending = std::move(event);
                        queued_bytes_ += event_bytes;
                        return NK_OK;
                    }
                }
                pending_file_watch_overflows_.push_back(std::move(event));
                queued_bytes_ += event_bytes;
                return NK_OK;
            }
        } else if (!is_persistent_readiness(event.kind)) {
            return NK_ERROR_QUEUE_FULL;
        } else {
            for (const auto &queued : queue_)
                if (same_readiness_target(queued, event))
                    return NK_OK;
            for (const auto &queued : deferred_readiness_)
                if (same_readiness_target(queued, event))
                    return NK_OK;
            deferred_readiness_.push_back(std::move(event));
            queued_bytes_ += event_bytes;
            return NK_OK;
        }
    }
    if (terminal)
        promote_readiness_for_request(queue_, deferred_readiness_, event);
    queue_.push_back(std::move(event));
    queued_bytes_ += event_bytes;
    return NK_OK;
}

nk_result EventQueue::poll(nk_event &output) {
    std::lock_guard lock(mutex_);
    promote_deferred_readiness(queue_, deferred_readiness_, capacity_);
    promote_pending_overflow(queue_, pending_file_watch_overflows_, capacity_);
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
    queued_bytes_ -= event.data.size();
    queue_.pop_front();
    promote_deferred_readiness(queue_, deferred_readiness_, capacity_);
    promote_pending_overflow(queue_, pending_file_watch_overflows_, capacity_);
    return NK_OK;
}

bool EventQueue::empty() {
    std::lock_guard lock(mutex_);
    return queue_.empty() && deferred_readiness_.empty() && pending_file_watch_overflows_.empty();
}

void EventQueue::clear() {
    std::lock_guard lock(mutex_);
    queue_.clear();
    queued_bytes_ = 0;
    deferred_readiness_.clear();
    pending_file_watch_overflows_.clear();
}

} // namespace nk::core
