#pragma once

#include "nativekit.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

namespace nk::core {

struct QueuedEvent {
    nk_event_kind kind = NK_EVENT_NONE;
    nk_handle source = NK_INVALID_HANDLE;
    std::uint32_t flags = 0;
    nk_request_id request_id = NK_INVALID_REQUEST_ID;
    nk_result result = NK_OK;
    std::uint32_t data_count = 0;
    std::vector<std::byte> data;
    /** When the OS stamped the input behind this event, on nk_time_now_ns()'s clock; 0 when unknown
     * or not an input. */
    std::uint64_t os_time_ns = 0;
};

class EventQueue {
  public:
    explicit EventQueue(std::size_t capacity, std::size_t byte_capacity = 16u * 1024u * 1024u);
    nk_result push(QueuedEvent event);
    /** Takes the oldest event. `os_time_ns`, when given, receives its OS timestamp (0 for none). */
    nk_result poll(nk_event &output, std::uint64_t *os_time_ns = nullptr);
    bool empty();
    void clear();

  private:
    std::size_t capacity_;
    std::size_t byte_capacity_;
    std::size_t queued_bytes_ = 0;
    std::mutex mutex_;
    std::deque<QueuedEvent> queue_;
    std::deque<QueuedEvent> deferred_readiness_;
    std::deque<QueuedEvent> pending_file_watch_overflows_;
};

} // namespace nk::core
