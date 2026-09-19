#pragma once

#include "nativekit.h"

#include <cstddef>

namespace nk::core {

/** One unit of work queued for the application executor. */
struct AppTask {
    /** Logical destination retained for the future physical executor queues. */
    nk_executor executor = NK_EXECUTOR_APP;
    nk_task_fn fn = nullptr;
    void *user_data = nullptr;
    /** NativeKit-owned cleanup for user_data when a task is run or discarded. */
    void (*cleanup)(void *) noexcept = nullptr;
    /** Bytes charged against the bounded executor queue. */
    std::size_t bytes = 0;
};

/** Binds the calling thread as the platform/application executor. */
void bind_main_thread() noexcept;

/** Releases the executor binding; no thread is bound afterwards. */
void unbind_main_thread() noexcept;

/** Starts and stops the physical render executor when the backend supports it. */
nk_result start_render_executor() noexcept;
void stop_render_executor() noexcept;

/** Enables strict render affinity after the UI handoff has moved to RENDER. */
void set_render_executor_exclusive(bool exclusive) noexcept;

/** Whether this runtime has a physical render thread. */
bool render_executor_physical() noexcept;

/** Returns the logical executor of the calling thread, or NK_EXECUTOR_WORKER. */
nk_executor executor_current() noexcept;

/** Reports whether the calling thread satisfies `executor`'s affinity. */
bool executor_satisfies(nk_executor executor) noexcept;

/** Validates that the calling thread satisfies `executor`'s affinity. */
nk_result require_executor(nk_executor executor) noexcept;

/** Queues one task for the application executor without invoking it. */
nk_result dispatch_to_app(nk_task_fn fn, void *user_data) noexcept;

/**
 * Queues owned work for a logical executor. RENDER is routed to its physical
 * queue when the selected backend has a dedicated render thread; otherwise it
 * remains in the application queue without changing callers or the ABI.
 */
nk_result dispatch_to_executor(nk_executor executor, nk_task_fn fn, void *user_data,
                               void (*cleanup)(void *) noexcept, std::size_t bytes) noexcept;

/** Queues owned work on the physical render executor. */
nk_result dispatch_to_render(nk_task_fn fn, void *user_data, void (*cleanup)(void *) noexcept,
                             std::size_t bytes) noexcept;

/** Runs the tasks queued so far on the application executor. */
void drain_app_tasks() noexcept;

/** Discards queued tasks without invoking them. */
void clear_app_tasks() noexcept;

} // namespace nk::core
