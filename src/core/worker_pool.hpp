#pragma once

#include "nativekit.h"

#include <condition_variable>
#include <cstddef>
#include <deque>
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
    std::size_t queue_capacity_ = 0;
    bool accepting_ = false;
    bool stopping_ = false;
};

} // namespace nk::core
