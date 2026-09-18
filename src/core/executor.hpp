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

/** Binds the calling thread as the platform/application/render executor. */
void bind_main_thread() noexcept;

/** Releases the executor binding; no thread is bound afterwards. */
void unbind_main_thread() noexcept;

/** Returns the logical executor of the calling thread, or NK_EXECUTOR_WORKER. */
nk_executor executor_current() noexcept;

/** Reports whether the calling thread satisfies `executor`'s affinity. */
bool executor_satisfies(nk_executor executor) noexcept;

/** Validates that the calling thread satisfies `executor`'s affinity. */
nk_result require_executor(nk_executor executor) noexcept;

/** Queues one task for the application executor without invoking it. */
nk_result dispatch_to_app(nk_task_fn fn, void *user_data) noexcept;

/**
 * Queues owned work for a logical executor. PLATFORM, APP, and RENDER are
 * aliases today, but the target is retained so dispatch can become physical
 * without changing callers or the ABI.
 */
nk_result dispatch_to_executor(nk_executor executor, nk_task_fn fn, void *user_data,
                               void (*cleanup)(void *) noexcept, std::size_t bytes) noexcept;

/** Runs the tasks queued so far on the application executor. */
void drain_app_tasks() noexcept;

/** Discards queued tasks without invoking them. */
void clear_app_tasks() noexcept;

} // namespace nk::core
