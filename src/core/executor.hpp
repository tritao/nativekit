#pragma once

#include "nativekit.h"

namespace nk::core {

/** One unit of work queued for the application executor. */
struct AppTask {
    nk_task_fn fn = nullptr;
    void *user_data = nullptr;
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

/** Runs the tasks queued so far on the application executor. */
void drain_app_tasks() noexcept;

/** Discards queued tasks without invoking them. */
void clear_app_tasks() noexcept;

} // namespace nk::core
