#include "core/worker_pool.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

int main() {
    nk::core::WorkerPool pool;
    assert(pool.start(1, 1) == NK_OK);

    std::mutex mutex;
    std::condition_variable condition;
    bool release = false;
    std::atomic<bool> started{false};
    std::atomic<int> ran{0};
    assert(pool.submit([&] {
        started.store(true, std::memory_order_release);
        std::unique_lock lock(mutex);
        condition.wait(lock, [&] { return release; });
        ran.fetch_add(1, std::memory_order_release);
    }) == NK_OK);
    for (int attempt = 0; attempt < 100 && !started.load(std::memory_order_acquire); ++attempt)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    assert(started.load(std::memory_order_acquire));
    /* The first work item is executing, so one more item fits in the queue. */
    assert(pool.submit([&] { ran.fetch_add(1, std::memory_order_release); }) == NK_OK);
    assert(pool.submit([] {}) == NK_ERROR_QUEUE_FULL);
    {
        std::lock_guard lock(mutex);
        release = true;
    }
    condition.notify_all();
    for (int attempt = 0; attempt < 100 && ran.load(std::memory_order_acquire) != 2; ++attempt)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    assert(ran.load(std::memory_order_acquire) == 2);
    pool.shutdown();
    assert(!pool.accepting());
    assert(pool.submit([] {}) == NK_ERROR_INVALID_REQUEST);
    return 0;
}
