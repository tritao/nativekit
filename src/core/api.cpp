#include "nativekit.h"

#include "core/error.hpp"
#include "core/event_queue.hpp"
#include "core/runtime.hpp"

#include <cstddef>
#include <atomic>
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
constexpr std::uint32_t default_queue_capacity = 1024;

bool valid_event_struct(const nk_event* event) {
    return event && event->struct_size >= sizeof(nk_event);
}
}

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

HandleRegistry& handles() noexcept { return handle_registry; }

nk_result push_event(QueuedEvent event) noexcept {
    try {
        std::lock_guard lock(state_mutex);
        if (!event_queue) return NK_ERROR_NOT_INITIALIZED;
        return event_queue->push(std::move(event));
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

}

extern "C" {

uint32_t NK_CALL nk_api_version(void) { return NK_API_VERSION; }

nk_result NK_CALL nk_init(const nk_init_options* options) {
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
        const auto capacity = options->event_queue_capacity == 0
            ? default_queue_capacity : options->event_queue_capacity;
        event_queue = std::make_unique<nk::core::EventQueue>(capacity);
        ui_thread = std::this_thread::get_id();
        return NK_OK;
    } catch (const std::bad_alloc&) {
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
        nk::core::clear_error();
    } catch (...) {
        nk::core::set_error("unexpected exception while shutting down NativeKit");
    }
}

const char* NK_CALL nk_last_error(void) { return nk::core::last_error(); }

nk_result NK_CALL nk_poll_event(nk_event* event) {
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
        if (thread_result != NK_OK) return thread_result;
        nk::backend::pump_events();
        std::lock_guard lock(state_mutex);
        return event_queue->poll(*event);
    } catch (const std::bad_alloc&) {
        nk::core::set_error("out of memory while polling an event");
        return NK_ERROR_OUT_OF_MEMORY;
    } catch (...) {
        nk::core::set_error("unexpected exception while polling an event");
        return NK_ERROR_UNKNOWN;
    }
}

void NK_CALL nk_event_release(nk_event* event) {
    if (!event) return;
    delete[] static_cast<const std::byte*>(event->data);
    const auto size = event->struct_size;
    *event = {};
    event->struct_size = size;
}

}
