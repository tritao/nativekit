#include "core/worker_pool.hpp"

#include "core/boundary.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <new>
#include <utility>

namespace nk::core {

WorkerPool::~WorkerPool() {
    shutdown();
}

nk_result WorkerPool::start(std::size_t worker_count, std::size_t queue_capacity) {
#if defined(NK_BACKEND_WEB)
    (void)worker_count;
    (void)queue_capacity;
    return NK_OK;
#else
    if (worker_count == 0 || queue_capacity == 0)
        return NK_ERROR_INVALID_ARGUMENT;
    std::unique_lock lock(mutex_);
    if (accepting_ || !workers_.empty())
        return NK_ERROR_ALREADY_INITIALIZED;
    queue_capacity_ = queue_capacity;
    stopping_ = false;
    accepting_ = true;
#if NK_ENABLE_NO_EXCEPTIONS
    {
#else
    try {
#endif
        workers_.reserve(worker_count);
        for (std::size_t index = 0; index < worker_count; ++index)
            workers_.emplace_back([this] { worker_loop(); });
#if !NK_ENABLE_NO_EXCEPTIONS
    }
    catch (const std::bad_alloc &) {
        accepting_ = false;
        stopping_ = true;
        lock.unlock();
        condition_.notify_all();
        for (auto &worker : workers_)
            if (worker.joinable())
                worker.join();
        workers_.clear();
        queue_capacity_ = 0;
        return NK_ERROR_OUT_OF_MEMORY;
    }
    catch (...) {
        accepting_ = false;
        stopping_ = true;
        lock.unlock();
        condition_.notify_all();
        for (auto &worker : workers_)
            if (worker.joinable())
                worker.join();
        workers_.clear();
        queue_capacity_ = 0;
        return NK_ERROR_UNKNOWN;
    }
#else
    }
#endif
    return NK_OK;
#endif
}

nk_result WorkerPool::submit(std::function<void()> work) {
    if (!work)
        return NK_ERROR_INVALID_ARGUMENT;
#if defined(NK_BACKEND_WEB)
    return NK_ERROR_UNSUPPORTED;
#else
    {
        std::lock_guard lock(mutex_);
        if (!accepting_ || stopping_)
            return NK_ERROR_INVALID_REQUEST;
        if (queue_.size() >= queue_capacity_)
            return NK_ERROR_QUEUE_FULL;
        queue_.push_back(std::move(work));
    }
    condition_.notify_one();
    return NK_OK;
#endif
}

void WorkerPool::shutdown() noexcept {
#if !defined(NK_BACKEND_WEB)
    {
        std::lock_guard lock(mutex_);
        accepting_ = false;
        stopping_ = true;
        /* Queued work is discarded after its task has been asked to cancel by
         * the task runtime. Work already executing observes that request. */
        queue_.clear();
    }
    condition_.notify_all();
    for (auto &worker : workers_)
        if (worker.joinable())
            worker.join();
    std::lock_guard lock(mutex_);
    workers_.clear();
    queue_capacity_ = 0;
#else
    std::lock_guard lock(mutex_);
    accepting_ = false;
    stopping_ = true;
    queue_.clear();
#endif
}

bool WorkerPool::accepting() const noexcept {
    std::lock_guard lock(mutex_);
    return accepting_ && !stopping_;
}

void WorkerPool::worker_loop() noexcept {
#if !defined(NK_BACKEND_WEB)
    for (;;) {
        std::function<void()> work;
        {
            std::unique_lock lock(mutex_);
            condition_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (stopping_)
                return;
            work = std::move(queue_.front());
            queue_.pop_front();
        }
        callback_boundary([&] { work(); });
    }
#endif
}

namespace {

constexpr std::uint8_t worker_task_idle = 0;
constexpr std::uint8_t worker_task_queued = 1;
constexpr std::uint8_t worker_task_running = 2;

struct QueuedWorkerTask {
    nk::core::WorkerTask run;
    nk::core::WorkerTask cleanup;
};

class SignalWorkerPool final {
  public:
    ~SignalWorkerPool() {
        shutdown();
    }

    nk_result submit(nk::core::WorkerTask run, nk::core::WorkerTask cleanup) noexcept {
        if (!run)
            return NK_ERROR_INVALID_ARGUMENT;
        try {
            std::unique_lock lock(mutex_);
            if (stopping_.load(std::memory_order_acquire))
                return NK_ERROR_INVALID_REQUEST;
            const auto start_result = start_workers(lock);
            if (start_result != NK_OK)
                return start_result;
            queued_.push_back({std::move(run), std::move(cleanup)});
            condition_.notify_one();
            return NK_OK;
        } catch (...) {
            return NK_ERROR_OUT_OF_MEMORY;
        }
    }

    nk_result initialize(nk::core::WorkerTaskState &task, nk::core::WorkerTask run,
                         nk::core::WorkerTask cleanup) noexcept {
        if (!run)
            return NK_ERROR_INVALID_ARGUMENT;
        if (task.state.load(std::memory_order_acquire) != worker_task_idle)
            return NK_ERROR_INVALID_REQUEST;
        try {
            task.run = std::move(run);
            task.cleanup = std::move(cleanup);
            task.next.store(nullptr, std::memory_order_release);
            task.reschedule.store(false, std::memory_order_release);
            return NK_OK;
        } catch (...) {
            return NK_ERROR_OUT_OF_MEMORY;
        }
    }

    nk_result schedule(nk::core::WorkerTaskState &task) noexcept {
        if (!task.run)
            return NK_ERROR_INVALID_ARGUMENT;
        if (stopping_.load(std::memory_order_acquire))
            return NK_ERROR_INVALID_REQUEST;

        auto state = task.state.load(std::memory_order_acquire);
        for (;;) {
            if (state == worker_task_running) {
                task.reschedule.store(true, std::memory_order_release);
                return NK_OK;
            }
            if (state == worker_task_queued)
                return NK_OK;
            if (state != worker_task_idle)
                state = task.state.load(std::memory_order_acquire);
            else if (task.state.compare_exchange_weak(
                         state, worker_task_queued, std::memory_order_acq_rel))
                break;
        }

        if (!workers_started_.load(std::memory_order_acquire)) {
            std::unique_lock lock(mutex_);
            if (stopping_.load(std::memory_order_acquire)) {
                task.state.store(worker_task_idle, std::memory_order_release);
                return NK_ERROR_INVALID_REQUEST;
            }
            const auto start_result = start_workers(lock);
            if (start_result != NK_OK) {
                task.state.store(worker_task_idle, std::memory_order_release);
                return start_result;
            }
        }

        if (stopping_.load(std::memory_order_acquire)) {
            std::uint8_t expected = worker_task_queued;
            if (task.state.compare_exchange_strong(expected, worker_task_idle,
                                                    std::memory_order_acq_rel))
                return NK_ERROR_INVALID_REQUEST;
            return NK_OK;
        }

        auto *head = signal_queue_.load(std::memory_order_acquire);
        do {
            task.next.store(head, std::memory_order_relaxed);
        } while (!signal_queue_.compare_exchange_weak(head, &task, std::memory_order_release,
                                                       std::memory_order_acquire));
        condition_.notify_one();

        if (stopping_.load(std::memory_order_acquire)) {
            std::uint8_t expected = worker_task_queued;
            if (task.state.compare_exchange_strong(expected, worker_task_idle,
                                                    std::memory_order_acq_rel))
                return NK_ERROR_INVALID_REQUEST;
        }
        return NK_OK;
    }

    void shutdown() noexcept {
        std::deque<QueuedWorkerTask> queued;
        {
            std::lock_guard lock(mutex_);
            if (workers_.empty())
                return;
            stopping_.store(true, std::memory_order_release);
            queued.swap(queued_);
        }

        auto *signal_tasks = signal_queue_.exchange(nullptr, std::memory_order_acq_rel);
        for (auto &task : queued)
            run_cleanup(task.cleanup);
        cancel_signal_tasks(signal_tasks);
        condition_.notify_all();

        std::vector<std::thread> workers;
        {
            std::lock_guard lock(mutex_);
            workers = std::move(workers_);
        }
        for (auto &worker : workers) {
            if (worker.joinable())
                worker.join();
        }

        std::lock_guard lock(mutex_);
        workers_started_.store(false, std::memory_order_release);
        stopping_.store(false, std::memory_order_release);
    }

  private:
    static void run_cleanup(nk::core::WorkerTask &cleanup) noexcept {
        if (!cleanup)
            return;
        try {
            cleanup();
        } catch (...) {
        }
    }

    static void cancel_signal_tasks(nk::core::WorkerTaskState *task) noexcept {
        while (task) {
            auto *next = task->next.load(std::memory_order_relaxed);
            std::uint8_t expected = worker_task_queued;
            if (task->state.compare_exchange_strong(expected, worker_task_idle,
                                                     std::memory_order_acq_rel))
                run_cleanup(task->cleanup);
            task = next;
        }
    }

    nk_result start_workers(std::unique_lock<std::mutex> &lock) noexcept {
        if (!workers_.empty())
            return NK_OK;
        const auto hardware_threads = std::thread::hardware_concurrency();
        const auto available_threads = hardware_threads > 1 ? hardware_threads - 1 : 1;
        const auto worker_count = std::min<unsigned>(available_threads, 4);
        try {
            workers_.reserve(worker_count);
            for (unsigned index = 0; index < worker_count; ++index)
                workers_.emplace_back([this] { run_worker(); });
        } catch (...) {
            workers_started_.store(false, std::memory_order_release);
            stopping_.store(true, std::memory_order_release);
            condition_.notify_all();
            auto workers = std::move(workers_);
            lock.unlock();
            for (auto &worker : workers) {
                if (worker.joinable())
                    worker.join();
            }
            lock.lock();
            stopping_.store(false, std::memory_order_release);
            return NK_ERROR_OUT_OF_MEMORY;
        }
        workers_started_.store(true, std::memory_order_release);
        return NK_OK;
    }

    nk::core::WorkerTaskState *take_signal_tasks() noexcept {
        return signal_queue_.exchange(nullptr, std::memory_order_acq_rel);
    }

    void run_signal_task(nk::core::WorkerTaskState *task) noexcept {
        std::uint8_t expected = worker_task_queued;
        if (!task->state.compare_exchange_strong(expected, worker_task_running,
                                                  std::memory_order_acq_rel))
            return;

        try {
            task->run();
        } catch (...) {
        }
        task->state.store(worker_task_idle, std::memory_order_release);
        if (task->reschedule.exchange(false, std::memory_order_acq_rel) &&
            schedule(*task) != NK_OK)
            run_cleanup(task->cleanup);
        run_cleanup(task->cleanup);
    }

    void run_worker() noexcept {
        for (;;) {
            if (auto *signal_tasks = take_signal_tasks()) {
                while (signal_tasks) {
                    auto *task = signal_tasks;
                    signal_tasks = task->next.load(std::memory_order_relaxed);
                    run_signal_task(task);
                }
                continue;
            }

            QueuedWorkerTask task;
            {
                std::unique_lock lock(mutex_);
                condition_.wait(lock, [this] {
                    return stopping_.load(std::memory_order_acquire) || !queued_.empty() ||
                           signal_queue_.load(std::memory_order_acquire) != nullptr;
                });
                if (stopping_.load(std::memory_order_acquire))
                    return;
                if (signal_queue_.load(std::memory_order_acquire) != nullptr)
                    continue;
                task = std::move(queued_.front());
                queued_.pop_front();
            }

            try {
                task.run();
            } catch (...) {
            }
            run_cleanup(task.cleanup);
        }
    }

    std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<QueuedWorkerTask> queued_;
    std::atomic<nk::core::WorkerTaskState *> signal_queue_{nullptr};
    std::vector<std::thread> workers_;
    std::atomic<bool> workers_started_{false};
    std::atomic<bool> stopping_{false};
};

SignalWorkerPool signal_worker_pool;
} // namespace

namespace nk::core {

nk_result submit_worker_task(WorkerTask task, WorkerTask cleanup) noexcept {
    return signal_worker_pool.submit(std::move(task), std::move(cleanup));
}

nk_result initialize_worker_task(WorkerTaskState &task, WorkerTask run,
                                 WorkerTask cleanup) noexcept {
    return signal_worker_pool.initialize(task, std::move(run), std::move(cleanup));
}

nk_result schedule_worker_task(WorkerTaskState &task) noexcept {
    return signal_worker_pool.schedule(task);
}

void shutdown_worker_pool() noexcept {
    signal_worker_pool.shutdown();
}

} // namespace nk::core
