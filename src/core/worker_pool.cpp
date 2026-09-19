#include "core/worker_pool.hpp"

#include "core/boundary.hpp"

#include <new>

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
    } catch (const std::bad_alloc &) {
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
    } catch (...) {
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

} // namespace nk::core
