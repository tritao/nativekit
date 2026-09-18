#include "nativekit.h"
#include "nativekit_task.h"

#include <assert.h>

static int calls;

static nk_task_step_result NK_CALL web_task_step(const nk_task_step_context *context,
                                                 nk_task_step_output *output) {
    (void)output;
    assert(nk_executor_is_current(NK_EXECUTOR_APP) == 1);
    assert(context->budget_ns > 0);
    ++calls;
    if (calls == 1)
        return NK_TASK_STEP_YIELD;
    return NK_TASK_STEP_COMPLETE;
}

int main(void) {
    nk_init_options init = {0};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    assert(nk_init(&init) == NK_OK);

    nk_task_options options = {0};
    options.struct_size = sizeof(options);
    options.execution_mode = NK_TASK_EXECUTION_AUTO;
    nk_task task = NK_INVALID_HANDLE;
    assert(nk_task_start(&options, &web_task_step, NULL, &task) == NK_OK);
    nk_event event = {0};
    event.struct_size = sizeof(event);
    int complete = 0;
    for (int attempt = 0; attempt < 8 && !complete; ++attempt) {
        assert(nk_poll_event(&event) == NK_OK);
        if (event.kind == NK_EVENT_TASK_COMPLETE) {
            assert(event.source == task);
            assert(event.result == NK_OK);
            complete = 1;
        }
        nk_event_release(&event);
        event.struct_size = sizeof(event);
    }
    assert(complete && calls == 2);
    assert(nk_task_destroy(task) == NK_OK);
    nk_shutdown();
    return 0;
}
