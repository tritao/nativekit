#ifndef NATIVEKIT_TASK_H
#define NATIVEKIT_TASK_H

#include "nativekit.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Generation-checked handle for a native resumable task. */
typedef uint32_t nk_task NK_HANDLE NK_HANDLE_DESTROY(nk_task_destroy);

/** Scheduler selected for a task. */
typedef uint32_t nk_task_execution_mode;

enum NK_ENUM(nk_task_execution_mode) {
    /** Background execution on native targets and cooperative execution on Web. */
    NK_TASK_EXECUTION_AUTO = 0,
    /** Native worker-pool execution, with cooperative Web fallback. */
    NK_TASK_EXECUTION_BACKGROUND = 1,
    /** Bounded execution on the application executor. */
    NK_TASK_EXECUTION_COOPERATIVE = 2
};

/** Observable lifecycle state of a task. */
typedef uint32_t nk_task_state;

enum NK_ENUM(nk_task_state) {
    NK_TASK_STATE_RUNNING = 0,
    NK_TASK_STATE_YIELDED = 1,
    NK_TASK_STATE_COMPLETED = 2,
    NK_TASK_STATE_FAILED = 3,
    NK_TASK_STATE_CANCELLED = 4
};

/** Action returned by one bounded task step. */
typedef uint32_t nk_task_step_result;

enum NK_ENUM(nk_task_step_result) {
    /** The task has more work and must be scheduled again. */
    NK_TASK_STEP_YIELD = 0,
    /** The task has completed successfully. */
    NK_TASK_STEP_COMPLETE = 1,
    /** The task failed; output.result describes the failure. */
    NK_TASK_STEP_FAILED = 2
};

/** Maximum copied progress or result payload accepted from one task step. */
enum { NK_TASK_MAX_PAYLOAD = 1024u * 1024u };

/** Options used when starting one task. */
typedef struct nk_task_options {
    /** Size of this structure in bytes; old prefixes remain valid. */
    uint32_t struct_size NK_STRUCT_SIZE;
    /** Requested scheduler. AUTO is the default when this field is zero. */
    nk_task_execution_mode execution_mode;
    /** Maximum time requested for one step, in microseconds; zero selects 2 ms. */
    uint32_t step_budget_us;
    /** Reserved for compatible extensions; initialize to zero. */
    uint32_t reserved[2];
} nk_task_options;

/** Read-only context supplied to one native task step. */
typedef struct nk_task_step_context {
    /** Size of this structure in bytes. */
    uint32_t struct_size NK_STRUCT_SIZE;
    /** Opaque native state supplied to nk_task_start(). */
    void *NK_NULLABLE state;
    /** Non-zero when cancellation has been requested. */
    nk_bool cancelled;
    /** Requested maximum duration for this step in nanoseconds. */
    uint64_t budget_ns;
    /** Runtime generation in which this task was started. */
    uint64_t runtime_generation;
} nk_task_step_context;

/** Progress and terminal payloads returned by one task step. */
typedef struct nk_task_step_output {
    /** Size of this structure in bytes. */
    uint32_t struct_size NK_STRUCT_SIZE;
    /** Byte-oriented progress payload copied before the step returns. */
    const void *NK_NULLABLE progress_data;
    uint32_t progress_size;
    uint32_t progress_count;
    /** Byte-oriented terminal/result payload copied before the step returns. */
    const void *NK_NULLABLE result_data;
    uint32_t result_size;
    uint32_t result_count;
    /** Failure result for NK_TASK_STEP_FAILED; NK_ERROR_UNKNOWN by default. */
    nk_result result;
} nk_task_step_output;

/**
 * One bounded native step. The callback must be a native function and must
 * not call UI-only APIs. It may retain no pointer to the context or output
 * structures after returning. Managed Haxe callbacks are not safe here.
 */
typedef nk_task_step_result(NK_CALL *nk_task_step_fn)(
    const nk_task_step_context *context, nk_task_step_output *output NK_INOUT);

/** Starts a resumable native task. Completion is delivered as a NativeKit event. */
NK_API nk_result NK_CALL nk_task_start(const nk_task_options *options,
                                       nk_task_step_fn step,
                                       void *NK_NULLABLE user_data,
                                       nk_task *out_task NK_OUT);

/** Requests cooperative cancellation; a terminal cancelled event is emitted. */
NK_API nk_result NK_CALL nk_task_cancel(nk_task task);

/** Reads the current generation-checked task state. */
NK_API nk_result NK_CALL nk_task_get_state(nk_task task, nk_task_state *out_state NK_OUT);

/** Destroys a task handle and suppresses any later task events for it. */
NK_API nk_result NK_CALL nk_task_destroy(nk_task task);

#ifdef __cplusplus
}
#endif

#endif
