#include "nativekit.h"

#include <cassert>
#include <thread>

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
    /* PLATFORM and APP stay on the init thread. */
    assert(nk_executor_is_current(NK_EXECUTOR_PLATFORM) == 1);
    assert(nk_executor_is_current(NK_EXECUTOR_APP) == 1);
    assert(nk_executor_is_current(NK_EXECUTOR_WORKER) == 0);
    assert(nk_executor_is_current(static_cast<nk_executor>(42)) == 0);
    assert(nk_dispatch_to_app(nullptr, nullptr) == NK_ERROR_INVALID_ARGUMENT);

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
    assert(record.runs == runs_before_shutdown);
    assert(nk_runtime_generation() == 0);
    assert(nk_executor_current() == NK_EXECUTOR_WORKER);
    assert(nk_dispatch_to_app(&record_task, &record) == NK_ERROR_NOT_INITIALIZED);
    return 0;
}
