#include "nativekit.h"
#include "nativekit_window.h"

#include <gtk/gtk.h>
#include <cassert>
#include <initializer_list>

static int crossing_events(nk_window window, bool entered) {
    int count = 0;
    for (;;) {
        nk_event event{};
        event.struct_size = sizeof(event);
        assert(nk_poll_event(&event) == NK_OK);
        const auto kind = event.kind;
        if (kind == NK_EVENT_POINTER_ENTER && event.source == window) {
            assert(event.flags == (entered ? 1u : 0u));
            ++count;
        }
        nk_event_release(&event);
        if (kind == NK_EVENT_NONE) return count;
    }
}

int main() {
    nk_init_options init{};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    assert(nk_init(&init) == NK_OK);
    nk_window_options options{};
    options.struct_size = sizeof(options);
    options.flags = NK_WINDOW_HIDDEN;
    options.width = 320;
    options.height = 240;
    options.title = "GTK pointer crossing regression";
    nk_window window{};
    assert(nk_window_create(&options, &window) == NK_OK);
    GList *windows = gtk_window_list_toplevels();
    assert(windows && !windows->next);
    GtkWidget *widget = GTK_WIDGET(windows->data);
    g_list_free(windows);
    crossing_events(window, false);

    const auto crossing = [&](bool enter, GdkCrossingMode mode,
                              GdkNotifyType detail, bool hovered, int expected_events) {
        GdkEventCrossing event{};
        event.type = enter ? GDK_ENTER_NOTIFY : GDK_LEAVE_NOTIFY;
        event.window = gtk_widget_get_window(widget);
        event.mode = mode;
        event.detail = detail;
        gboolean handled = FALSE;
        g_signal_emit_by_name(widget, enter ? "enter-notify-event" : "leave-notify-event",
                              &event, &handled);
        uint32_t actual = 0;
        assert(nk_window_get_hovered(window, &actual) == NK_OK);
        assert(actual == (hovered ? 1u : 0u));
        assert(crossing_events(window, hovered) == expected_events);
    };

    crossing(true, GDK_CROSSING_NORMAL, GDK_NOTIFY_NONLINEAR, true, 1);
    // Clicking/capturing and releasing must preserve both hover and event state.
    for (auto mode : {GDK_CROSSING_GRAB, GDK_CROSSING_UNGRAB,
                      GDK_CROSSING_GTK_GRAB, GDK_CROSSING_GTK_UNGRAB,
                      GDK_CROSSING_STATE_CHANGED, GDK_CROSSING_TOUCH_BEGIN,
                      GDK_CROSSING_TOUCH_END, GDK_CROSSING_DEVICE_SWITCH}) {
        crossing(false, mode, GDK_NOTIFY_NONLINEAR, true, 0);
        crossing(true, mode, GDK_NOTIFY_NONLINEAR, true, 0);
    }
    crossing(false, GDK_CROSSING_NORMAL, GDK_NOTIFY_INFERIOR, true, 0);
    crossing(true, GDK_CROSSING_NORMAL, GDK_NOTIFY_INFERIOR, true, 0);
    // Physical exit/re-entry still works after synthetic grab notifications.
    crossing(false, GDK_CROSSING_NORMAL, GDK_NOTIFY_NONLINEAR, false, 1);
    crossing(true, GDK_CROSSING_GRAB, GDK_NOTIFY_NONLINEAR, false, 0);
    crossing(true, GDK_CROSSING_NORMAL, GDK_NOTIFY_NONLINEAR, true, 1);
    assert(nk_window_destroy(window) == NK_OK);
    nk_shutdown();
    return 0;
}
