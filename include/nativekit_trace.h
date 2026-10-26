#ifndef NATIVEKIT_TRACE_H
#define NATIVEKIT_TRACE_H

/* ------------------------------------------------------------------------- */
/* Dependencies                                                              */
/* ------------------------------------------------------------------------- */

#include "nativekit.h"

/* ------------------------------------------------------------------------- */
/* C linkage                                                                 */
/* ------------------------------------------------------------------------- */

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/* Trace sink                                                                */
/* ------------------------------------------------------------------------- */

/*
 * NativeKit marks the work that decides how fast an application reacts to
 * input: waits for events, on-demand frames, and each OS input message it takes
 * off the platform queue. A host that records a timeline (a profiler, Tracy,
 * ETW, a file) installs a sink to receive those marks; NativeKit itself keeps
 * no clock, buffer or file for them. The sink timestamps each call.
 *
 * This header is meant for native hosts. It is not part of the generated
 * language bindings: a sink is a table of C function pointers.
 */

/**
 * A static description of one trace point. NativeKit defines one per trace
 * point with static storage duration, so a sink may key a location by its
 * address and describe it once.
 */
typedef struct nk_trace_location {
    /** Zone name, UTF-8, such as "nk.wait". */
    const char *name;
    /** Enclosing C or C++ function, UTF-8. */
    const char *function;
    /** Source file, UTF-8. */
    const char *file;
    /** Source line. */
    uint32_t line;
    /** Reserved; zero. */
    uint32_t reserved;
} nk_trace_location;

/** An instant recorded with nk_trace_sink.event. */
typedef uint32_t nk_trace_event_kind;
enum {
    /** The UI thread dequeued a pointer move. `a`, `b`: screen position in physical pixels. */
    NK_TRACE_EVENT_INPUT_MOVE = 1,
    /** The UI thread dequeued a pointer button transition. `a`, `b`: screen position in physical
       pixels. */
    NK_TRACE_EVENT_INPUT_BUTTON = 2,
    /** The UI thread dequeued a key transition. `a`: platform key code; `b`: 1 pressed, 0 released.
     */
    NK_TRACE_EVENT_INPUT_KEY = 3,
    /**
     * nk_poll_event() handed the application a pointer move, a pointer button transition or a key
     * transition. These come after the NK_TRACE_EVENT_INPUT_* of the same input: `os_time_ns` is
     * again when the OS stamped it, so the difference between the two is the time the event waited
     * in NativeKit's queue and the work done before the poll. A coalesced move carries the stamp of
     * the oldest input it stands for. `a`, `b`: zero.
     */
    NK_TRACE_EVENT_POLLED_MOVE = 4,
    NK_TRACE_EVENT_POLLED_BUTTON = 5,
    NK_TRACE_EVENT_POLLED_KEY = 6
};

/**
 * Receives NativeKit's trace points. Every callback may run on any thread and
 * must not call back into NativeKit. Zones nest per thread: each zone_begin is
 * matched by one zone_end on the same thread, sent to the same sink.
 */
typedef struct nk_trace_sink {
    /** Size of this structure in bytes; old prefixes remain valid. */
    uint32_t struct_size;
    /** Reserved; zero. */
    uint32_t reserved;
    /** Passed unchanged to every callback. */
    void *user_data;
    /**
     * Optional switch owned by the host. While it points at zero, trace points
     * cost one load each and call nothing. NULL means always on.
     */
    const volatile int *enabled;
    /** A zone starts at `location` on the calling thread. */
    void(NK_CALL *zone_begin)(void *user_data, const nk_trace_location *location);
    /** The calling thread's innermost zone ends. */
    void(NK_CALL *zone_end)(void *user_data);
    /**
     * An instant of `kind`. `os_time_ns` is when the OS stamped the input, on
     * the clock of nk_time_now_ns(), or 0 when the platform gives no stamp. The
     * stamp is coarse: 1 ms on GTK, the system tick (usually 15.6 ms) on Windows.
     */
    void(NK_CALL *event)(void *user_data, nk_trace_event_kind kind, uint64_t os_time_ns, int64_t a,
                         int64_t b);
} nk_trace_sink;

/**
 * Installs `sink`, or removes the current one when NULL. NativeKit keeps the
 * pointer, not a copy: the sink must stay valid while any trace call may still
 * reach it, which in practice means a sink with static storage. Callable before
 * nk_init() and from any thread.
 *
 * Without a host sink, setting the environment variable NK_LATENCY_TRACE to a
 * file path makes NativeKit write its trace points to that file as text.
 */
NK_API void NK_CALL nk_set_trace_sink(const nk_trace_sink *NK_NULLABLE sink);

#ifdef __cplusplus
}
#endif

#endif
