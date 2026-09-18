#include "nativekit_task.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <thread>

namespace {

struct StepState {
    std::atomic<int> calls{0};
    std::atomic<bool> worker{false};
    int complete_after = 1;
    std::uint32_t progress = 0;
    std::uint32_t result = 0;
};

nk_task_step_result NK_CALL counting_step(const nk_task_step_context *context,
                                          nk_task_step_output *output) {
    auto *state = static_cast<StepState *>(context->state);
    if (!nk_executor_is_current(NK_EXECUTOR_APP))
        state->worker.store(true, std::memory_order_release);
    const auto call = state->calls.fetch_add(1, std::memory_order_acq_rel) + 1;
    state->progress = static_cast<std::uint32_t>(call);
    output->progress_data = &state->progress;
    output->progress_size = sizeof(state->progress);
    output->progress_count = 1;
    if (call < state->complete_after)
        return NK_TASK_STEP_YIELD;
    state->result = 0xfeedbeefu;
    output->result_data = &state->result;
    output->result_size = sizeof(state->result);
    output->result_count = 1;
    return NK_TASK_STEP_COMPLETE;
}

nk_task_step_result NK_CALL cooperative_step(const nk_task_step_context *context,
                                             nk_task_step_output *) {
    auto *state = static_cast<StepState *>(context->state);
    assert(nk_executor_is_current(NK_EXECUTOR_APP) == 1);
    state->calls.fetch_add(1, std::memory_order_acq_rel);
    return NK_TASK_STEP_YIELD;
}

bool poll_until_terminal(nk_task task, nk_event_kind expected, int *progress_count = nullptr) {
    for (int attempt = 0; attempt < 1000; ++attempt) {
        nk_event event{};
        event.struct_size = sizeof(event);
        assert(nk_poll_event(&event) == NK_OK);
        if (event.kind == NK_EVENT_TASK_PROGRESS && progress_count)
            ++*progress_count;
        const bool terminal = event.source == task && event.kind == expected;
        nk_event_release(&event);
        if (terminal)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

} // namespace

int main() {
    nk_init_options init{};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    init.application_id = "dev.nativekit.task-tests";
    assert(nk_init(&init) == NK_OK);

    nk_task_options options{};
    options.struct_size = sizeof(options);
    options.execution_mode = NK_TASK_EXECUTION_BACKGROUND;
    StepState background_state;
    background_state.complete_after = 4;
    nk_task background = NK_INVALID_HANDLE;
    assert(nk_task_start(&options, &counting_step, &background_state, &background) == NK_OK);
    int progress_events = 0;
    assert(poll_until_terminal(background, NK_EVENT_TASK_COMPLETE, &progress_events));
    assert(background_state.calls.load(std::memory_order_acquire) == 4);
    assert(background_state.worker.load(std::memory_order_acquire));
    nk_task_state state = NK_TASK_STATE_RUNNING;
    assert(nk_task_get_state(background, &state) == NK_OK);
    assert(state == NK_TASK_STATE_COMPLETED);
    assert(nk_task_destroy(background) == NK_OK);
    assert(progress_events <= 4);

    options.execution_mode = NK_TASK_EXECUTION_COOPERATIVE;
    StepState cooperative_state;
    cooperative_state.complete_after = 3;
    nk_task cooperative = NK_INVALID_HANDLE;
    assert(nk_task_start(&options, &counting_step, &cooperative_state, &cooperative) == NK_OK);
    assert(cooperative_state.calls.load(std::memory_order_acquire) == 0);
    assert(poll_until_terminal(cooperative, NK_EVENT_TASK_COMPLETE));
    assert(cooperative_state.calls.load(std::memory_order_acquire) == 3);
    assert(!cooperative_state.worker.load(std::memory_order_acquire));
    assert(nk_task_destroy(cooperative) == NK_OK);

    nk_task_options cancel_options{};
    cancel_options.struct_size = sizeof(cancel_options);
    cancel_options.execution_mode = NK_TASK_EXECUTION_COOPERATIVE;
    StepState cancel_state;
    nk_task cancelled = NK_INVALID_HANDLE;
    assert(nk_task_start(&cancel_options, &cooperative_step, &cancel_state, &cancelled) == NK_OK);
    assert(nk_task_cancel(cancelled) == NK_OK);
    assert(poll_until_terminal(cancelled, NK_EVENT_TASK_CANCELLED));
    assert(nk_task_get_state(cancelled, &state) == NK_OK);
    assert(state == NK_TASK_STATE_CANCELLED);
    assert(nk_task_destroy(cancelled) == NK_OK);

    assert(nk_task_get_state(NK_INVALID_HANDLE, &state) == NK_ERROR_INVALID_HANDLE);

    StepState shutdown_state;
    shutdown_state.complete_after = 1000000;
    nk_task shutdown_task = NK_INVALID_HANDLE;
    options.execution_mode = NK_TASK_EXECUTION_BACKGROUND;
    assert(nk_task_start(&options, &counting_step, &shutdown_state, &shutdown_task) == NK_OK);
    for (int attempt = 0; attempt < 100 && shutdown_state.calls.load() == 0; ++attempt)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    assert(shutdown_state.calls.load() > 0);
    nk_shutdown();
    assert(nk_runtime_generation() == 0);
    return 0;
}
