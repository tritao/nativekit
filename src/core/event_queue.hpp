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
};

class EventQueue {
public:
    explicit EventQueue(std::size_t capacity);
    nk_result push(QueuedEvent event);
    nk_result poll(nk_event& output);
    void clear();

private:
    std::size_t capacity_;
    std::mutex mutex_;
    std::deque<QueuedEvent> queue_;
};

}

