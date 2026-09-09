#include "nativekit.h"

#include "core/error.hpp"
#include "core/event_queue.hpp"
#include "core/runtime.hpp"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <new>
#include <thread>
#include <utility>

namespace {
std::mutex state_mutex;
std::unique_ptr<nk::core::EventQueue> event_queue;
std::thread::id ui_thread;
nk::core::HandleRegistry handle_registry;
std::atomic<nk_request_id> next_request{1};
std::atomic<std::uint64_t> generation_counter{0};
std::atomic<std::uint64_t> active_generation{0};
std::atomic<std::uint64_t> event_wake_sequence{0};
std::mutex event_wake_mutex;
std::condition_variable event_wake_condition;
constexpr std::uint32_t default_queue_capacity = 1024;

bool valid_event_struct(const nk_event *event) {
    return event && event->struct_size >= sizeof(nk_event);
}
} // namespace

namespace nk::core {

nk_result require_ui_thread() noexcept {
    std::lock_guard lock(state_mutex);
    if (!event_queue) {
        set_error("NativeKit is not initialized");
        return NK_ERROR_NOT_INITIALIZED;
    }
    if (std::this_thread::get_id() != ui_thread) {
        set_error("NativeKit UI API called from the wrong thread");
        return NK_ERROR_WRONG_THREAD;
    }
    return NK_OK;
}

HandleRegistry &handles() noexcept {
    return handle_registry;
}

nk_result push_event(QueuedEvent event) noexcept {
    try {
        nk_result result = NK_ERROR_NOT_INITIALIZED;
        {
            std::lock_guard lock(state_mutex);
            if (event_queue)
                result = event_queue->push(std::move(event));
        }
        if (result == NK_OK)
            event_wake_condition.notify_all();
        return result;
    } catch (...) {
        return NK_ERROR_OUT_OF_MEMORY;
    }
}

nk_request_id next_request_id() noexcept {
    auto request = next_request.fetch_add(1, std::memory_order_relaxed);
    if (request == NK_INVALID_REQUEST_ID)
        request = next_request.fetch_add(1, std::memory_order_relaxed);
    return request;
}

std::uint64_t runtime_generation() noexcept {
    return active_generation.load(std::memory_order_acquire);
}

bool is_runtime_generation(std::uint64_t generation) noexcept {
    return generation != 0 && generation == runtime_generation();
}

bool events_pending() noexcept {
    std::lock_guard lock(state_mutex);
    return event_queue && !event_queue->empty();
}

std::uint64_t wake_sequence() noexcept {
    return event_wake_sequence.load(std::memory_order_acquire);
}

bool wait_for_wake(std::uint64_t sequence, std::chrono::milliseconds timeout) noexcept {
    std::unique_lock lock(event_wake_mutex);
    return event_wake_condition.wait_for(lock, timeout, [sequence] {
        return event_wake_sequence.load(std::memory_order_acquire) != sequence ||
               events_pending();
    });
}

void wake_events() noexcept {
    event_wake_sequence.fetch_add(1, std::memory_order_release);
    event_wake_condition.notify_all();
}

} // namespace nk::core

extern "C" {

uint32_t NK_CALL nk_api_version(void) {
    return NK_API_VERSION;
}

nk_result NK_CALL nk_init(const nk_init_options *options) {
    try {
        nk::core::clear_error();
        if (!options || options->struct_size < sizeof(nk_init_options)) {
            nk::core::set_error("nk_init_options is missing or too small");
            return NK_ERROR_INVALID_ARGUMENT;
        }
        if (options->api_version != NK_API_VERSION) {
            nk::core::set_error("unsupported NativeKit API version");
            return NK_ERROR_UNSUPPORTED;
        }
        std::lock_guard lock(state_mutex);
        if (event_queue) {
            nk::core::set_error("NativeKit is already initialized");
            return NK_ERROR_ALREADY_INITIALIZED;
        }
        const auto capacity = options->event_queue_capacity == 0 ? default_queue_capacity
                                                                 : options->event_queue_capacity;
        event_queue = std::make_unique<nk::core::EventQueue>(capacity);
        ui_thread = std::this_thread::get_id();
        auto generation = generation_counter.fetch_add(1, std::memory_order_relaxed) + 1;
        if (generation == 0)
            generation = generation_counter.fetch_add(1, std::memory_order_relaxed) + 1;
        active_generation.store(generation, std::memory_order_release);
        return NK_OK;
    } catch (const std::bad_alloc &) {
        nk::core::set_error("out of memory while initializing NativeKit");
        return NK_ERROR_OUT_OF_MEMORY;
    } catch (...) {
        nk::core::set_error("unexpected exception while initializing NativeKit");
        return NK_ERROR_UNKNOWN;
    }
}

void NK_CALL nk_shutdown(void) {
    try {
        nk::backend::shutdown();
        std::lock_guard lock(state_mutex);
        handle_registry.clear();
        event_queue.reset();
        ui_thread = {};
        active_generation.store(0, std::memory_order_release);
        nk::core::clear_error();
    } catch (...) {
        nk::core::set_error("unexpected exception while shutting down NativeKit");
    }
}

const char *NK_CALL nk_last_error(void) {
    return nk::core::last_error();
}

nk_result NK_CALL nk_poll_event(nk_event *event) {
    try {
        nk::core::clear_error();
        if (!valid_event_struct(event)) {
            nk::core::set_error("nk_event is missing or too small");
            return NK_ERROR_INVALID_ARGUMENT;
        }
        if (event->data) {
            nk::core::set_error("nk_event contains unreleased data");
            return NK_ERROR_INVALID_ARGUMENT;
        }
        const auto thread_result = nk::core::require_ui_thread();
        if (thread_result != NK_OK)
            return thread_result;
        nk::backend::pump_events();
        std::lock_guard lock(state_mutex);
        return event_queue->poll(*event);
    } catch (const std::bad_alloc &) {
        nk::core::set_error("out of memory while polling an event");
        return NK_ERROR_OUT_OF_MEMORY;
    } catch (...) {
        nk::core::set_error("unexpected exception while polling an event");
        return NK_ERROR_UNKNOWN;
    }
}

void NK_CALL nk_event_release(nk_event *event) {
    if (!event)
        return;
    delete[] static_cast<const std::byte *>(event->data);
    const auto size = event->struct_size;
    *event = {};
    event->struct_size = size;
}
}
