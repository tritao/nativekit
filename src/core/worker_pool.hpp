#pragma once

#include "nativekit.h"

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace nk::core {

/** Bounded native worker queue used by the task scheduler. */
class WorkerPool final {
  public:
    WorkerPool() = default;
    WorkerPool(const WorkerPool &) = delete;
    WorkerPool &operator=(const WorkerPool &) = delete;
    ~WorkerPool();

    nk_result start(std::size_t worker_count, std::size_t queue_capacity);
    nk_result submit(std::function<void()> work);
    void shutdown() noexcept;
    bool accepting() const noexcept;

  private:
    void worker_loop() noexcept;

    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<std::function<void()>> queue_;
    std::vector<std::thread> workers_;
#if !defined(NK_BACKEND_WEB)
    std::size_t queue_capacity_ = 0;
#endif
    bool accepting_ = false;
    bool stopping_ = false;
};

using WorkerTask = std::function<void()>;

/**
 * Reusable work item for signal-heavy producers. Scheduling an already queued
 * or running item is coalesced without allocation; once the pool is started,
 * signaling does not take the pool mutex.
 */
struct WorkerTaskState final {
    WorkerTask run;
    WorkerTask cleanup;
    std::atomic<WorkerTaskState *> next{nullptr};
    std::atomic<std::uint8_t> state{0};
    std::atomic<bool> reschedule{false};

    WorkerTaskState() = default;
    WorkerTaskState(const WorkerTaskState &) = delete;
    WorkerTaskState &operator=(const WorkerTaskState &) = delete;
};

/** Queues a short background task. The task must not call UI-only NativeKit APIs. */
nk_result submit_worker_task(WorkerTask task, WorkerTask cleanup = {}) noexcept;

/** Initializes a reusable work item. The item must remain alive until cleanup runs. */
nk_result initialize_worker_task(WorkerTaskState &task, WorkerTask run,
                                 WorkerTask cleanup = {}) noexcept;

/** Signals a reusable work item without allocating when it is already active. */
nk_result schedule_worker_task(WorkerTaskState &task) noexcept;

/** Stops accepting work, drops queued tasks, and joins all worker threads. */
void shutdown_worker_pool() noexcept;

} // namespace nk::core
