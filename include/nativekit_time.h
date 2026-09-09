#ifndef NATIVEKIT_TIME_H
#define NATIVEKIT_TIME_H

#include "nativekit.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Monotonic time since an unspecified epoch. These calls do not require init. */
NK_API uint64_t NK_CALL nk_time_now_ns(void);
NK_API double NK_CALL nk_time_seconds(void);

/*
 * Blocks on the UI thread until an event is queued or nk_wake_events() is
 * called. The timeout variant also returns after `timeout_seconds` elapses.
 * Call nk_poll_event() afterwards; explicit wakeups need not produce an event.
 */
NK_API nk_result NK_CALL nk_wait_events(void);
NK_API nk_result NK_CALL nk_wait_events_timeout(double timeout_seconds);

/* Thread-safe. Wakes a currently blocked event wait. */
NK_API void NK_CALL nk_wake_events(void);

#ifdef __cplusplus
}
#endif

#endif
