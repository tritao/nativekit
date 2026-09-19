#include "nativekit.h"
#include "core/executor.hpp"
#include "core/runtime.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <thread>
#include <vector>

namespace {

struct TaskRecord {
    int runs = 0;
    nk_executor executor = NK_EXECUTOR_WORKER;
    std::thread::id thread;
};

void NK_CALL record_task(void *user_data) {
    auto *record = static_cast<TaskRecord *>(user_data);
    record->runs++;
    record->executor = nk_executor_current();
    record->thread = std::this_thread::get_id();
}

int nested_runs = 0;
std::atomic<int> shutdown_hook_runs{0};

void shutdown_hook() noexcept {
    shutdown_hook_runs.fetch_add(1, std::memory_order_relaxed);
}

struct RenderRecord {
    std::atomic<int> runs{0};
    std::atomic<nk_executor> executor{NK_EXECUTOR_WORKER};
};

void NK_CALL record_render_task(void *user_data) {
    auto *record = static_cast<RenderRecord *>(user_data);
    record->executor.store(nk_executor_current(), std::memory_order_release);
    record->runs.fetch_add(1, std::memory_order_acq_rel);
}

void NK_CALL nested_task(void *) {
    nested_runs++;
}

void NK_CALL dispatch_nested_task(void *) {
    assert(nk_dispatch_to_app(&nested_task, nullptr) == NK_OK);
}

nk_event_kind poll_once() {
    nk_event event{};
    event.struct_size = sizeof(event);
    assert(nk_poll_event(&event) == NK_OK);
    const nk_event_kind kind = event.kind;
    nk_event_release(&event);
    return kind;
}

} // namespace

int main() {
    TaskRecord record;
    assert(nk_runtime_generation() == 0);
    assert(nk_executor_current() == NK_EXECUTOR_WORKER);
    assert(nk_executor_is_current(NK_EXECUTOR_APP) == 0);
    assert(nk_dispatch_to_app(&record_task, &record) == NK_ERROR_NOT_INITIALIZED);

    nk_init_options options{};
    options.struct_size = sizeof(options);
    options.api_version = NK_API_VERSION;
    options.application_id = "dev.nativekit.dispatch-tests";
    assert(nk_init(&options) == NK_OK);
    assert(nk_runtime_generation() != 0);

    const std::thread::id app_thread = std::this_thread::get_id();
    assert(nk_executor_current() == NK_EXECUTOR_APP);
    /* PLATFORM and APP stay on the init thread; aliased backends also satisfy
       RENDER there, while physical backends keep RENDER exclusive. */
    assert(nk_executor_is_current(NK_EXECUTOR_PLATFORM) == 1);
    assert(nk_executor_is_current(NK_EXECUTOR_APP) == 1);
    assert(nk_executor_is_current(NK_EXECUTOR_RENDER) == 1);
    assert(nk_executor_is_current(NK_EXECUTOR_WORKER) == 0);
    assert(nk_executor_is_current(static_cast<nk_executor>(42)) == 0);
    nk::core::register_runtime_shutdown_hook(&shutdown_hook);
    assert(nk_dispatch_to_app(nullptr, nullptr) == NK_ERROR_INVALID_ARGUMENT);

    RenderRecord render_record;
    assert(nk::core::dispatch_to_render(&record_render_task, &render_record, nullptr,
                                        sizeof(render_record)) == NK_OK);
    if (nk::core::render_executor_physical()) {
        for (int attempt = 0; attempt < 1000 && render_record.runs.load() == 0; ++attempt)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } else {
        assert(poll_once() == NK_EVENT_NONE);
    }
    assert(render_record.runs.load(std::memory_order_acquire) == 1);
    assert(render_record.executor.load(std::memory_order_acquire) ==
           (nk::core::render_executor_physical() ? NK_EXECUTOR_RENDER : NK_EXECUTOR_APP));

    std::thread worker([&] {
        assert(nk_executor_current() == NK_EXECUTOR_WORKER);
        assert(nk_executor_is_current(NK_EXECUTOR_WORKER) == 1);
        assert(nk_executor_is_current(NK_EXECUTOR_APP) == 0);
        for (int task = 0; task < 3; ++task)
            assert(nk_dispatch_to_app(&record_task, &record) == NK_OK);
        /* Queuing never runs the task on the calling thread. */
        assert(record.runs == 0);
    });
    worker.join();
    assert(record.runs == 0);

    assert(poll_once() == NK_EVENT_NONE);
    assert(record.runs == 3);
    assert(record.executor == NK_EXECUTOR_APP);
    assert(record.thread == app_thread);

    /* Work queued from inside a task runs on the following poll. */
    assert(nk_dispatch_to_app(&dispatch_nested_task, nullptr) == NK_OK);
    assert(poll_once() == NK_EVENT_NONE);
    assert(nested_runs == 0);
    assert(poll_once() == NK_EVENT_NONE);
    assert(nested_runs == 1);

    /* Shutdown discards queued work instead of running it. */
    const int runs_before_shutdown = record.runs;
    assert(nk_dispatch_to_app(&record_task, &record) == NK_OK);
    nk_shutdown();
    assert(shutdown_hook_runs.load(std::memory_order_relaxed) == 1);
    assert(record.runs == runs_before_shutdown);
    assert(nk_runtime_generation() == 0);
    assert(nk_executor_current() == NK_EXECUTOR_WORKER);
    assert(nk_dispatch_to_app(&record_task, &record) == NK_ERROR_NOT_INITIALIZED);
    return 0;
}
