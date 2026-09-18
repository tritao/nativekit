#include "core/executor.hpp"

#include "core/boundary.hpp"
#include "core/error.hpp"
#include "core/runtime.hpp"

#include <cstddef>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>

namespace {

std::mutex executor_mutex;
bool executor_bound = false;
std::thread::id main_thread_id;

std::mutex task_mutex;
std::deque<nk::core::AppTask> pending_tasks;
constexpr std::size_t app_task_capacity = 4096;
constexpr std::size_t app_task_byte_capacity = 4u * 1024u * 1024u;
std::size_t pending_task_bytes = 0;

bool bound_to_main_thread(nk_executor executor) noexcept {
    return executor == NK_EXECUTOR_PLATFORM || executor == NK_EXECUTOR_APP ||
           executor == NK_EXECUTOR_RENDER;
}

bool known_executor(nk_executor executor) noexcept {
    return bound_to_main_thread(executor) || executor == NK_EXECUTOR_WORKER;
}

} // namespace

namespace nk::core {

void bind_main_thread() noexcept {
    std::lock_guard lock(executor_mutex);
    main_thread_id = std::this_thread::get_id();
    executor_bound = true;
}

void unbind_main_thread() noexcept {
    std::lock_guard lock(executor_mutex);
    executor_bound = false;
    main_thread_id = std::thread::id{};
}

nk_executor executor_current() noexcept {
    std::lock_guard lock(executor_mutex);
    /* The platform, application, and render executors still share one thread. */
    if (executor_bound && std::this_thread::get_id() == main_thread_id)
        return NK_EXECUTOR_APP;
    return NK_EXECUTOR_WORKER;
}

bool executor_satisfies(nk_executor executor) noexcept {
    std::lock_guard lock(executor_mutex);
    if (!executor_bound)
        return false;
    const bool on_main_thread = std::this_thread::get_id() == main_thread_id;
    if (executor == NK_EXECUTOR_WORKER)
        return !on_main_thread;
    return on_main_thread;
}

nk_result require_executor(nk_executor executor) noexcept {
    if (!known_executor(executor)) {
        set_error("unknown NativeKit executor");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    std::lock_guard lock(executor_mutex);
    if (!executor_bound) {
        set_error("NativeKit is not initialized");
        return NK_ERROR_NOT_INITIALIZED;
    }
    if (bound_to_main_thread(executor) && std::this_thread::get_id() != main_thread_id) {
        set_error("NativeKit executor API called from the wrong thread");
        return NK_ERROR_WRONG_THREAD;
    }
    return NK_OK;
}

nk_result dispatch_to_executor(nk_executor executor, nk_task_fn fn, void *user_data,
                               void (*cleanup)(void *) noexcept, std::size_t bytes) noexcept {
    if (!fn) {
        set_error("nk_task_fn is null");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    if (!known_executor(executor) || executor == NK_EXECUTOR_WORKER) {
        set_error("tasks require a bound NativeKit executor");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    if (bytes == 0)
        bytes = sizeof(AppTask);
    if (bytes > app_task_byte_capacity) {
        set_error("the executor task exceeds the byte budget");
        return NK_ERROR_QUEUE_FULL;
    }
    try {
        {
            std::lock_guard lock(executor_mutex);
            if (!executor_bound) {
                set_error("NativeKit is not initialized");
                return NK_ERROR_NOT_INITIALIZED;
            }
        }
        {
            std::lock_guard lock(task_mutex);
            if (pending_tasks.size() >= app_task_capacity ||
                pending_task_bytes > app_task_byte_capacity - bytes) {
                set_error("the application dispatch queue is full");
                return NK_ERROR_QUEUE_FULL;
            }
            pending_tasks.push_back(AppTask{executor, fn, user_data, cleanup, bytes});
            pending_task_bytes += bytes;
        }
    } catch (...) {
        set_error("out of memory while dispatching to the application executor");
        return NK_ERROR_OUT_OF_MEMORY;
    }
    wake_events();
    return NK_OK;
}

nk_result dispatch_to_app(nk_task_fn fn, void *user_data) noexcept {
    return dispatch_to_executor(NK_EXECUTOR_APP, fn, user_data, nullptr, sizeof(AppTask));
}

void drain_app_tasks() noexcept {
    std::deque<AppTask> batch;
    {
        std::lock_guard lock(task_mutex);
        batch.swap(pending_tasks);
        pending_task_bytes = 0;
    }
    for (auto &task : batch) {
        if (task.fn)
            callback_boundary([&task] { task.fn(task.user_data); });
        if (task.cleanup)
            task.cleanup(task.user_data);
    }
}

void clear_app_tasks() noexcept {
    std::deque<AppTask> discarded;
    {
        std::lock_guard lock(task_mutex);
        discarded.swap(pending_tasks);
        pending_task_bytes = 0;
    }
    for (auto &task : discarded)
        if (task.cleanup)
            task.cleanup(task.user_data);
}

} // namespace nk::core

extern "C" {

nk_executor NK_CALL nk_executor_current(void) {
    return nk::core::executor_current();
}

nk_bool NK_CALL nk_executor_is_current(nk_executor executor) {
    switch (executor) {
    case NK_EXECUTOR_PLATFORM:
    case NK_EXECUTOR_APP:
    case NK_EXECUTOR_RENDER:
    case NK_EXECUTOR_WORKER:
        break;
    default:
        return 0;
    }
    return nk::core::executor_satisfies(executor) ? 1u : 0u;
}

nk_result NK_CALL nk_dispatch_to_app(nk_task_fn fn, void *user_data) {
    return nk::core::result_boundary(
        "unexpected exception while dispatching to the application executor", [&]() -> nk_result {
            nk::core::clear_error();
            return nk::core::dispatch_to_app(fn, user_data);
        });
}

} // extern "C"
