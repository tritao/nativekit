#include "nativekit.h"
#include "nativekit_graphics.h"
#include "nativekit_time.h"
#include "nativekit_window.h"

#include <gtk/gtk.h>
#include <cassert>
#include <cstdio>

static void drain_events() {
    for (;;) {
        nk_event event{};
        event.struct_size = sizeof(event);
        assert(nk_poll_event(&event) == NK_OK);
        const auto kind = event.kind;
        nk_event_release(&event);
        if (kind == NK_EVENT_NONE) return;
    }
}

int main() {
    nk_init_options init{};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    assert(nk_init(&init) == NK_OK);
    drain_events();

    // Unrelated GLib activity must not turn a timed wait into a busy poll.
    bool unrelated = false, explicitly_woken = false;
    g_timeout_add(1, [](gpointer data) -> gboolean {
        *static_cast<bool *>(data) = true;
        return G_SOURCE_REMOVE;
    }, &unrelated);
    g_timeout_add(20, [](gpointer data) -> gboolean {
        *static_cast<bool *>(data) = true;
        nk_wake_events();
        return G_SOURCE_REMOVE;
    }, &explicitly_woken);
    assert(nk_wait_events_timeout(1.0) == NK_OK);
    assert(unrelated && explicitly_woken);

    nk_window_options window_options{};
    window_options.struct_size = sizeof(window_options);
    window_options.title = "GTK event wait regression";
    window_options.width = 160;
    window_options.height = 120;
    nk_window window{};
    assert(nk_window_create(&window_options, &window) == NK_OK);
    nk_surface_options surface_options{};
    surface_options.struct_size = sizeof(surface_options);
    surface_options.api = NK_GRAPHICS_OPENGL;
    surface_options.major_version = 3;
    surface_options.minor_version = 3;
    surface_options.width = 160;
    surface_options.height = 120;
    nk_surface surface{};
    assert(nk_surface_create(window, &surface_options, &surface) == NK_OK);
    const double settled_at = nk_time_seconds() + 0.2;
    while (nk_time_seconds() < settled_at) {
        drain_events();
        assert(nk_wait_events_timeout(0.01) == NK_OK);
    }
    drain_events();

    int frames = 0;
    assert(nk_surface_set_frame_callback(surface,
        [](nk_surface, int32_t, int32_t, void *data) {
            ++*static_cast<int *>(data);
        }, &frames) == NK_OK);
    assert(nk_surface_set_frame_mode(surface, NK_SURFACE_FRAME_ON_DEMAND) == NK_OK);
    // Request from inside the wait, so polling beforehand cannot render it.
    g_timeout_add(5, [](gpointer data) -> gboolean {
        assert(nk_surface_request_frame(*static_cast<nk_surface *>(data)) == NK_OK);
        return G_SOURCE_REMOVE;
    }, &surface);
    bool watchdog_fired = false;
    const guint watchdog = g_timeout_add(1000, [](gpointer data) -> gboolean {
        *static_cast<bool *>(data) = true;
        nk_wake_events();
        return G_SOURCE_REMOVE;
    }, &watchdog_fired);
    while (!frames && !watchdog_fired)
        assert(nk_wait_events_timeout(2.0) == NK_OK);
    assert(frames == 1 && !watchdog_fired);
    g_source_remove(watchdog);
    assert(nk_surface_set_frame_callback(surface, nullptr, nullptr) == NK_OK);
    assert(nk_surface_destroy(surface) == NK_OK);
    assert(nk_window_destroy(window) == NK_OK);
    nk_shutdown();
    std::puts("PASS: GTK waits preserve explicit wakes and return after a frame callback");
}
