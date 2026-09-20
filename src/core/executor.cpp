#include "core/executor.hpp"

#include "core/boundary.hpp"
#include "core/error.hpp"
#include "core/runtime.hpp"

#include <atomic>
#include <cstddef>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <new>
#include <thread>
#include <utility>

#if defined(NK_BACKEND_WEB) && defined(NK_WEB_THREADED_RENDER)
#include "nativekit_web_config.h"

#include <emscripten/threading.h>
#include <pthread.h>
#endif

namespace {

std::mutex executor_mutex;
bool executor_bound = false;
std::thread::id main_thread_id;
std::thread::id render_thread_id;
bool render_executor_exclusive = false;
std::atomic_bool render_surface_api_guard = false;
std::atomic<std::uint64_t> render_surface_api_violation_count = 0;
std::atomic_bool fail_next_platform_dispatch_once = false;

std::mutex render_task_mutex;
std::condition_variable render_task_condition;
std::condition_variable render_started_condition;
std::deque<nk::core::AppTask> render_tasks;
#if defined(NK_BACKEND_WEB) && defined(NK_WEB_THREADED_RENDER)
pthread_t render_thread{};
bool render_thread_joinable = false;
#else
std::thread render_thread;
#endif
bool render_started = false;
bool render_accepting = false;
bool render_stopping = false;
std::size_t render_task_bytes = 0;

std::mutex task_mutex;
std::deque<nk::core::AppTask> pending_tasks;
constexpr std::size_t app_task_capacity = 4096;
constexpr std::size_t app_task_byte_capacity = 4u * 1024u * 1024u;
std::size_t pending_task_bytes = 0;

constexpr bool physical_render_backend =
#if defined(NK_BACKEND_GTK) || (defined(NK_BACKEND_WEB) && !defined(NK_WEB_THREADED_RENDER))
    false;
#else
    true;
#endif

bool bound_to_main_thread(nk_executor executor) noexcept {
    return executor == NK_EXECUTOR_PLATFORM || executor == NK_EXECUTOR_APP ||
           executor == NK_EXECUTOR_RENDER;
}

bool known_executor(nk_executor executor) noexcept {
    return bound_to_main_thread(executor) || executor == NK_EXECUTOR_WORKER;
}

void render_loop() noexcept {
    {
        std::lock_guard lock(executor_mutex);
        render_thread_id = std::this_thread::get_id();
    }
    {
        std::lock_guard lock(render_task_mutex);
        render_started = true;
    }
    render_started_condition.notify_all();

    for (;;) {
        nk::core::AppTask task;
        {
            std::unique_lock lock(render_task_mutex);
            render_task_condition.wait(lock,
                                       [] { return render_stopping || !render_tasks.empty(); });
            if (render_stopping)
                break;
            task = render_tasks.front();
            render_tasks.pop_front();
            render_task_bytes -= task.bytes;
        }
        if (task.fn)
            nk::core::callback_boundary([&task] { task.fn(task.user_data); });
        if (task.cleanup)
            task.cleanup(task.user_data);
    }

    std::deque<nk::core::AppTask> discarded;
    {
        std::lock_guard lock(render_task_mutex);
        discarded.swap(render_tasks);
        render_task_bytes = 0;
        render_accepting = false;
    }
    for (auto &task : discarded)
        if (task.cleanup)
            task.cleanup(task.user_data);
    {
        std::lock_guard lock(executor_mutex);
        render_thread_id = std::thread::id{};
    }
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

nk_result start_render_executor() noexcept {
    if (!physical_render_backend)
        return NK_OK;
    {
        std::lock_guard lock(render_task_mutex);
        if (render_accepting)
            return NK_ERROR_ALREADY_INITIALIZED;
        render_stopping = false;
        render_started = false;
        render_accepting = true;
        render_task_bytes = 0;
    }
#if defined(NK_BACKEND_WEB) && defined(NK_WEB_THREADED_RENDER)
    pthread_attr_t attributes;
    if (pthread_attr_init(&attributes) != 0) {
        std::lock_guard lock(render_task_mutex);
        render_accepting = false;
        return NK_ERROR_UNKNOWN;
    }
    /* The canvas must be transferred before the worker creates its WebGL
       context.  The selector string is held by the generated configuration
       header for the lifetime of the process. */
    if (emscripten_pthread_attr_settransferredcanvases(&attributes,
                                                       NK_WEB_CANVAS_SELECTOR) != 0) {
        pthread_attr_destroy(&attributes);
        std::lock_guard lock(render_task_mutex);
        render_accepting = false;
        return NK_ERROR_UNSUPPORTED;
    }
    const auto thread_entry = [](void *) -> void * {
        render_loop();
        return nullptr;
    };
    const int created = pthread_create(&render_thread, &attributes, thread_entry, nullptr);
    pthread_attr_destroy(&attributes);
    if (created != 0) {
        std::lock_guard lock(render_task_mutex);
        render_accepting = false;
        return NK_ERROR_UNKNOWN;
    }
    render_thread_joinable = true;
#else
    render_thread = std::thread(render_loop);
#endif
    std::unique_lock lock(render_task_mutex);
    render_started_condition.wait(lock, [] { return render_started; });
    {
        std::lock_guard executor_lock(executor_mutex);
        render_executor_exclusive = true;
    }
    return NK_OK;
}

void stop_render_executor() noexcept {
    if (!physical_render_backend)
        return;
    {
        std::lock_guard lock(render_task_mutex);
#if defined(NK_BACKEND_WEB) && defined(NK_WEB_THREADED_RENDER)
        if (!render_accepting && !render_thread_joinable)
#else
        if (!render_accepting && !render_thread.joinable())
#endif
            return;
        render_accepting = false;
        render_stopping = true;
    }
    render_task_condition.notify_all();
#if defined(NK_BACKEND_WEB) && defined(NK_WEB_THREADED_RENDER)
    if (render_thread_joinable) {
        pthread_join(render_thread, nullptr);
        render_thread = {};
        render_thread_joinable = false;
    }
#else
    if (render_thread.joinable())
        render_thread.join();
#endif
    std::lock_guard lock(executor_mutex);
    render_executor_exclusive = false;
}

void set_render_executor_exclusive(bool exclusive) noexcept {
    std::lock_guard lock(executor_mutex);
    render_executor_exclusive = exclusive;
}

bool render_executor_physical() noexcept {
    return physical_render_backend;
}

void set_render_surface_api_guard(bool enabled) noexcept {
    render_surface_api_guard.store(enabled, std::memory_order_release);
}

void reset_render_surface_api_violations() noexcept {
    render_surface_api_violation_count.store(0, std::memory_order_release);
}

std::uint64_t render_surface_api_violations() noexcept {
    return render_surface_api_violation_count.load(std::memory_order_acquire);
}

nk_executor executor_current() noexcept {
    std::lock_guard lock(executor_mutex);
    if (physical_render_backend && executor_bound && std::this_thread::get_id() == render_thread_id)
        return NK_EXECUTOR_RENDER;
    if (executor_bound && std::this_thread::get_id() == main_thread_id)
        return NK_EXECUTOR_APP;
    return NK_EXECUTOR_WORKER;
}

bool executor_satisfies(nk_executor executor) noexcept {
    std::lock_guard lock(executor_mutex);
    if (!executor_bound)
        return false;
    const bool on_main_thread = std::this_thread::get_id() == main_thread_id;
    const bool on_render_thread =
        physical_render_backend && std::this_thread::get_id() == render_thread_id;
    if (executor == NK_EXECUTOR_WORKER)
        return !on_main_thread && !on_render_thread;
    if (executor == NK_EXECUTOR_RENDER)
        return on_render_thread || (!render_executor_exclusive && on_main_thread);
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
    const bool on_main_thread = std::this_thread::get_id() == main_thread_id;
    const bool on_render_thread =
        physical_render_backend && std::this_thread::get_id() == render_thread_id;
    if (on_render_thread && executor != NK_EXECUTOR_RENDER &&
        render_surface_api_guard.load(std::memory_order_acquire))
        render_surface_api_violation_count.fetch_add(1, std::memory_order_relaxed);
    const bool satisfied =
        executor == NK_EXECUTOR_RENDER
            ? (on_render_thread || (!render_executor_exclusive && on_main_thread))
            : (executor == NK_EXECUTOR_WORKER ? !on_main_thread && !on_render_thread
                                              : on_main_thread);
    if (!satisfied) {
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
    if (executor == NK_EXECUTOR_PLATFORM && executor_current() == NK_EXECUTOR_RENDER &&
        fail_next_platform_dispatch_once.exchange(false, std::memory_order_acq_rel)) {
        set_error("injected platform dispatch failure");
        return NK_ERROR_QUEUE_FULL;
    }
    if (executor == NK_EXECUTOR_RENDER && physical_render_backend)
        return dispatch_to_render(fn, user_data, cleanup, bytes);
    {
        std::lock_guard lock(executor_mutex);
        if (!executor_bound) {
            set_error("NativeKit is not initialized");
            return NK_ERROR_NOT_INITIALIZED;
        }
    }
#if NK_ENABLE_NO_EXCEPTIONS
    {
#else
    try {
#endif
        std::lock_guard lock(task_mutex);
        if (pending_tasks.size() >= app_task_capacity ||
            pending_task_bytes > app_task_byte_capacity - bytes) {
            set_error("the application dispatch queue is full");
            return NK_ERROR_QUEUE_FULL;
        }
        pending_tasks.push_back(AppTask{executor, fn, user_data, cleanup, bytes});
        pending_task_bytes += bytes;
#if !NK_ENABLE_NO_EXCEPTIONS
    }
    catch (const std::bad_alloc &) {
        set_error("out of memory while queuing application work");
        return NK_ERROR_OUT_OF_MEMORY;
    }
    catch (...) {
        set_error("unexpected error while queuing application work");
        return NK_ERROR_UNKNOWN;
    }
#else
    }
#endif
    wake_events();
    return NK_OK;
}

nk_result dispatch_to_render(nk_task_fn fn, void *user_data, void (*cleanup)(void *) noexcept,
                             std::size_t bytes) noexcept {
    if (!fn) {
        set_error("nk_task_fn is null");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    if (!physical_render_backend)
        return dispatch_to_executor(NK_EXECUTOR_APP, fn, user_data, cleanup, bytes);
    if (bytes == 0)
        bytes = sizeof(AppTask);
    if (bytes > app_task_byte_capacity) {
        set_error("the render task exceeds the byte budget");
        return NK_ERROR_QUEUE_FULL;
    }
    {
        std::lock_guard lock(executor_mutex);
        if (!executor_bound) {
            set_error("NativeKit is not initialized");
            return NK_ERROR_NOT_INITIALIZED;
        }
    }
    {
        std::lock_guard lock(render_task_mutex);
        if (!render_accepting || render_stopping || render_tasks.size() >= app_task_capacity ||
            render_task_bytes > app_task_byte_capacity - bytes) {
            set_error("the render dispatch queue is full");
            return NK_ERROR_QUEUE_FULL;
        }
        render_tasks.push_back(AppTask{NK_EXECUTOR_RENDER, fn, user_data, cleanup, bytes});
        render_task_bytes += bytes;
    }
    render_task_condition.notify_one();
    return NK_OK;
}

namespace {

struct SynchronousRenderTask {
    std::mutex mutex;
    std::condition_variable condition;
    nk_task_fn fn = nullptr;
    void *user_data = nullptr;
    bool completed = false;
};

void run_synchronous_render_task(void *user_data) {
    auto *task = static_cast<SynchronousRenderTask *>(user_data);
    if (!task)
        return;
    if (task->fn)
        nk::core::callback_boundary([&] { task->fn(task->user_data); });
    {
        std::lock_guard lock(task->mutex);
        task->completed = true;
    }
    task->condition.notify_one();
}

} // namespace

nk_result dispatch_to_render_sync(nk_task_fn fn, void *user_data, std::size_t bytes) noexcept {
    if (!fn) {
        set_error("nk_task_fn is null");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    if (executor_current() == NK_EXECUTOR_RENDER || executor_satisfies(NK_EXECUTOR_RENDER)) {
        nk::core::callback_boundary([&] { fn(user_data); });
        return NK_OK;
    }
    SynchronousRenderTask task;
    task.fn = fn;
    task.user_data = user_data;
    const nk_result queued = dispatch_to_render(&run_synchronous_render_task, &task, nullptr,
                                                bytes == 0 ? sizeof(task) : bytes);
    if (queued != NK_OK)
        return queued;
    std::unique_lock lock(task.mutex);
    task.condition.wait(lock, [&] { return task.completed; });
    return NK_OK;
}

void fail_next_platform_dispatch() noexcept {
    fail_next_platform_dispatch_once.store(true, std::memory_order_release);
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
