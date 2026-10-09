#include "nativekit.h"
#include "nativekit_time.h"

#include <assert.h>
#include <glib.h>

static gboolean timer_wake(gpointer data) {
    int *calls = data;
    ++*calls;
    nk_wake_events();
    return G_SOURCE_REMOVE;
}

static gpointer thread_wake(gpointer data) {
    (void)data;
    g_usleep(2000);
    nk_wake_events();
    return NULL;
}

static void drain_events(void) {
    for (;;) {
        nk_event event = {0};
        event.struct_size = sizeof(event);
        assert(nk_poll_event(&event) == NK_OK);
        const nk_event_kind kind = event.kind;
        nk_event_release(&event);
        if (kind == NK_EVENT_NONE) return;
    }
}

int main(void) {
    nk_init_options options = {0};
    options.struct_size = sizeof(options);
    options.api_version = NK_API_VERSION;
    assert(nk_init(&options) == NK_OK);
    drain_events();

    /* GTK sources and explicit cross-thread wakes both interrupt a long wait. */
    int calls = 0;
    g_timeout_add(2, timer_wake, &calls);
    double started = nk_time_seconds();
    assert(nk_wait_events_timeout(1.0) == NK_OK);
    assert(calls == 1);
    assert(nk_time_seconds() - started < 0.5);

    GThread *waker = g_thread_new("event-wake", thread_wake, NULL);
    started = nk_time_seconds();
    assert(nk_wait_events_timeout(1.0) == NK_OK);
    assert(nk_time_seconds() - started < 0.5);
    g_thread_join(waker);

    /* Expiring one wait must not leave its temporary source in later waits. */
    for (int iteration = 0; iteration < 3; ++iteration) {
        started = nk_time_seconds();
        assert(nk_wait_events_timeout(0.04) == NK_OK);
        const double elapsed = nk_time_seconds() - started;
        assert(elapsed >= 0.025 && elapsed < 0.3);
    }
    assert(calls == 1);
    assert(nk_wait_events_timeout(0.0) == NK_OK);
    nk_shutdown();
    return 0;
}
