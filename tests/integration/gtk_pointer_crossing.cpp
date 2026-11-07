#include "nativekit.h"
#include "nativekit_window.h"
#include "nativekit_input.h"

#include <gtk/gtk.h>
#include <cassert>
#include <initializer_list>

static int crossing_events(nk_window window, bool entered, double *move_x = nullptr,
                           double *move_y = nullptr, bool *moved = nullptr) {
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
        if (kind == NK_EVENT_POINTER_MOVE && event.source == window && moved) {
            assert(event.data_size == sizeof(nk_pointer_move_event));
            const auto *position = static_cast<const nk_pointer_move_event *>(event.data);
            *move_x = position->x;
            *move_y = position->y;
            *moved = true;
        }
        nk_event_release(&event);
        if (kind == NK_EVENT_NONE)
            return count;
    }
}

int main() {
    nk_init_options init{};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    assert(nk_init(&init) == NK_OK);
    nk_window_options options{};
    options.struct_size = sizeof(options);
    options.flags = NK_WINDOW_HIDDEN | NK_WINDOW_BORDERLESS | NK_WINDOW_RESIZABLE;
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

    const auto crossing = [&](bool enter, GdkCrossingMode mode, GdkNotifyType detail, bool hovered,
                              int expected_events, double x = 100.0, double y = 100.0) {
        GdkEventCrossing event{};
        event.type = enter ? GDK_ENTER_NOTIFY : GDK_LEAVE_NOTIFY;
        event.window = gtk_widget_get_window(widget);
        event.mode = mode;
        event.detail = detail;
        event.x = x;
        event.y = y;
        gboolean handled = FALSE;
        g_signal_emit_by_name(widget, enter ? "enter-notify-event" : "leave-notify-event", &event,
                              &handled);
        uint32_t actual = 0;
        assert(nk_window_get_hovered(window, &actual) == NK_OK);
        assert(actual == (hovered ? 1u : 0u));
        if (mode == GDK_CROSSING_NORMAL && detail != GDK_NOTIFY_INFERIOR) {
            double pointer_x = 0, pointer_y = 0;
            assert(nk_pointer_get_position(window, &pointer_x, &pointer_y) == NK_OK);
            assert(pointer_x == x && pointer_y == y);
        }
        double move_x = 0, move_y = 0;
        bool moved = false;
        assert(crossing_events(window, hovered, &move_x, &move_y, &moved) == expected_events);
        if (enter && mode == GDK_CROSSING_NORMAL && detail != GDK_NOTIFY_INFERIOR) {
            assert(moved);
            assert(move_x == x && move_y == y);
        }
    };

    crossing(true, GDK_CROSSING_NORMAL, GDK_NOTIFY_NONLINEAR, true, 1);
    // Clicking/capturing and releasing must preserve both hover and event state.
    for (auto mode : {GDK_CROSSING_GRAB, GDK_CROSSING_UNGRAB, GDK_CROSSING_GTK_GRAB,
                      GDK_CROSSING_GTK_UNGRAB, GDK_CROSSING_STATE_CHANGED, GDK_CROSSING_TOUCH_BEGIN,
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
    // The client cursor is intent, not the currently displayed decoration cursor.
    nk_cursor hand{};
    assert(nk_cursor_create_standard(NK_CURSOR_HAND, &hand) == NK_OK);
    assert(nk_window_set_cursor(window, hand) == NK_OK);
    auto *native = gtk_widget_get_window(widget);
    auto *client_cursor = gdk_window_get_cursor(native);
    assert(client_cursor);
    nk_window_decoration_region edge{};
    edge.x = 0;
    edge.y = 0;
    edge.width = 10;
    edge.height = 240;
    edge.kind = NK_WINDOW_DECORATION_RESIZE_WEST;
    assert(nk_window_set_decoration_regions(window, &edge, 1) == NK_OK);
    crossing(true, GDK_CROSSING_NORMAL, GDK_NOTIFY_NONLINEAR, true, 1, 2, 100);
    assert(gdk_window_get_cursor(native) != client_cursor);

    // Leaving clears decoration ownership; re-entry must use its new position,
    // even when no motion event or changed client cursor intent follows it.
    crossing(false, GDK_CROSSING_NORMAL, GDK_NOTIFY_NONLINEAR, false, 1, 2, 100);
    assert(gdk_window_get_cursor(native) == client_cursor);
    crossing(true, GDK_CROSSING_NORMAL, GDK_NOTIFY_NONLINEAR, true, 1, 100, 100);
    assert(gdk_window_get_cursor(native) == client_cursor);

    crossing(true, GDK_CROSSING_NORMAL, GDK_NOTIFY_NONLINEAR, true, 1, 2, 100);
    assert(nk_window_set_cursor_mode(window, NK_CURSOR_MODE_HIDDEN) == NK_OK);
    assert(nk_window_set_cursor_mode(window, NK_CURSOR_MODE_NORMAL) == NK_OK);
    // Returning to normal mode resolves border intent immediately, without motion.
    assert(gdk_window_get_cursor(native) != client_cursor);
    // Native resize completion may provide only an ungrab crossing. The backend
    // must query the current pointer rather than retain the pre-resize edge.
    auto *display = gdk_window_get_display(native);
    auto *pointer = gdk_seat_get_pointer(gdk_display_get_default_seat(display));
    int origin_x = 0, origin_y = 0;
    gdk_window_get_origin(native, &origin_x, &origin_y);
    gdk_device_warp(pointer, gdk_window_get_screen(native), origin_x + 100, origin_y + 100);
    gdk_display_sync(display);
    crossing(true, GDK_CROSSING_UNGRAB, GDK_NOTIFY_NONLINEAR, true, 0);
    assert(gdk_window_get_cursor(native) == client_cursor);

    // A geometry event also reconciles a stationary pointer after a resize.
    crossing(true, GDK_CROSSING_NORMAL, GDK_NOTIFY_NONLINEAR, true, 1, 2, 100);
    assert(gdk_window_get_cursor(native) != client_cursor);
    GdkEventConfigure configure{};
    configure.type = GDK_CONFIGURE;
    configure.window = native;
    configure.width = 320;
    configure.height = 240;
    gboolean handled = FALSE;
    g_signal_emit_by_name(widget, "configure-event", &configure, &handled);
    assert(gdk_window_get_cursor(native) == client_cursor);

    // Removing a hovered region immediately restores client intent.
    crossing(true, GDK_CROSSING_NORMAL, GDK_NOTIFY_NONLINEAR, true, 1, 2, 100);
    assert(gdk_window_get_cursor(native) != client_cursor);
    assert(nk_window_set_decoration_regions(window, nullptr, 0) == NK_OK);
    assert(gdk_window_get_cursor(native) == client_cursor);
    assert(nk_cursor_destroy(hand) == NK_OK);
    assert(nk_window_destroy(window) == NK_OK);
    nk_shutdown();
    return 0;
}
