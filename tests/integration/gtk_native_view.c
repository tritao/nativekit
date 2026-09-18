#include "nativekit.h"
#include "nativekit_view.h"
#include "nativekit_window.h"

#include <gtk/gtk.h>

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>

static void require_ok(const char *operation, nk_result result) {
    if (result != NK_OK) {
        fprintf(stderr, "%s returned %d: %s\n", operation, result, nk_last_error());
        assert(0);
    }
}

static nk_view_bounds committed_bounds(nk_view view) {
    nk_view_bounds bounds = {0};
    bounds.struct_size = sizeof(bounds);
    require_ok("nk_view_get_bounds", nk_view_get_bounds(view, &bounds));
    return bounds;
}

static void require_size_request(nk_view view, int expected_width, int expected_height) {
    nk_native_view native = {0};
    native.struct_size = sizeof(native);
    require_ok("nk_view_get_native", nk_view_get_native(view, &native));
    gint width = 0;
    gint height = 0;
    gtk_widget_get_size_request(GTK_WIDGET(native.view), &width, &height);
    if (width != expected_width || height != expected_height) {
        fprintf(stderr, "size request was %dx%d, expected %dx%d\n", width, height, expected_width,
                expected_height);
        assert(0);
    }
}

static void require_allocation(nk_view view, int expected_x, int expected_y, int expected_width,
                               int expected_height) {
    nk_native_view native = {0};
    native.struct_size = sizeof(native);
    require_ok("nk_view_get_native", nk_view_get_native(view, &native));
    GtkAllocation allocation;
    gtk_widget_get_allocation(GTK_WIDGET(native.view), &allocation);
    if (allocation.x != expected_x || allocation.y != expected_y ||
        allocation.width != expected_width || allocation.height != expected_height) {
        fprintf(stderr, "allocation was (%d,%d %dx%d), expected (%d,%d %dx%d)\n", allocation.x,
                allocation.y, allocation.width, allocation.height, expected_x, expected_y,
                expected_width, expected_height);
        assert(0);
    }
}

int main(void) {
    nk_init_options init = {0};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    assert(nk_init(&init) == NK_OK);
    assert((nk_get_capabilities() & NK_CAP_NATIVE_VIEW) != 0);

    nk_window_options window_options = {0};
    window_options.struct_size = sizeof(window_options);
    window_options.width = 640;
    window_options.height = 480;
    window_options.title = "NativeKit native view test";
    nk_window window = NK_INVALID_HANDLE;
    require_ok("nk_window_create", nk_window_create(&window_options, &window));

    nk_view_options options = {0};
    options.struct_size = sizeof(options);
    options.x = 10;
    options.y = 20;
    options.width = 200;
    options.height = 150;
    nk_view view = NK_INVALID_HANDLE;
    require_ok("nk_view_create", nk_view_create(window, &options, &view));
    assert(view != NK_INVALID_HANDLE);

    /* The native handle is a GTK container the application can populate. */
    nk_native_view native = {0};
    native.struct_size = sizeof(native);
    require_ok("nk_view_get_native", nk_view_get_native(view, &native));
    assert(native.kind == NK_NATIVE_VIEW_GTK);
    assert(native.view != 0);
    assert(GTK_IS_FIXED(GTK_WIDGET(native.view)));

    /* Creation is already committed. */
    nk_view_bounds bounds = committed_bounds(view);
    assert(bounds.x == 10 && bounds.y == 20 && bounds.width == 200 && bounds.height == 150);
    require_size_request(view, 200, 150);

    /* Pending edits are invisible until commit. */
    require_ok("nk_view_set_bounds", nk_view_set_bounds(view, 30, 40, 220, 160));
    bounds = committed_bounds(view);
    assert(bounds.x == 10 && bounds.width == 200);
    require_size_request(view, 200, 150);
    require_ok("nk_view_commit", nk_view_commit(view));
    bounds = committed_bounds(view);
    assert(bounds.x == 30 && bounds.y == 40 && bounds.width == 220 && bounds.height == 160);
    require_size_request(view, 220, 160);

    /*
     * A clip constrains the rectangle the view may occupy to the intersection
     * of its bounds and the clip.
     */
    require_ok("nk_view_set_bounds", nk_view_set_bounds(view, 300, 40, 100, 160));
    require_ok("nk_view_set_clip", nk_view_set_clip(view, 1, 350, 40, 100, 80));
    require_ok("nk_view_commit", nk_view_commit(view));
    /* Intersection is x 350..400, y 40..120. */
    require_size_request(view, 50, 80);
    /* Bounds still report the unclipped rectangle. */
    bounds = committed_bounds(view);
    assert(bounds.x == 300 && bounds.y == 40 && bounds.width == 100 && bounds.height == 160);
    /* Disabling the clip restores the full bounds. */
    require_ok("nk_view_set_clip", nk_view_set_clip(view, 0, 0, 0, 0, 0));
    require_ok("nk_view_commit", nk_view_commit(view));
    require_size_request(view, 100, 160);

    /* A fully clipped view is hidden instead of allocated a zero-size rectangle. */
    require_ok("nk_view_set_bounds", nk_view_set_bounds(view, 500, 400, 100, 100));
    require_ok("nk_view_set_clip", nk_view_set_clip(view, 1, 0, 0, 50, 50));
    require_ok("nk_view_commit", nk_view_commit(view));
    assert(!gtk_widget_get_visible(GTK_WIDGET(native.view)));

    /* Visibility is pending state too. */
    require_ok("nk_view_set_clip", nk_view_set_clip(view, 0, 0, 0, 0, 0));
    require_ok("nk_view_set_visible", nk_view_set_visible(view, 1));
    require_ok("nk_view_commit", nk_view_commit(view));
    assert(gtk_widget_get_visible(GTK_WIDGET(native.view)));

    /* The platform places the committed rectangle once GTK allocates the window. */
    bool allocated = false;
    for (int attempt = 0; attempt < 500 && !allocated; ++attempt) {
        nk_event event = {0};
        event.struct_size = sizeof(event);
        require_ok("nk_poll_event", nk_poll_event(&event));
        nk_event_release(&event);
        GtkAllocation allocation;
        gtk_widget_get_allocation(GTK_WIDGET(native.view), &allocation);
        allocated = allocation.x == 500 && allocation.y == 400 && allocation.width == 100 &&
                    allocation.height == 100;
        if (!allocated)
            usleep(10000);
    }
    require_allocation(view, 500, 400, 100, 100);

    /* Invalid arguments and stale handles are rejected. */
    assert(nk_view_set_bounds(view, 0, 0, 0, 10) == NK_ERROR_INVALID_ARGUMENT);
    assert(nk_view_set_clip(view, 1, 0, 0, 0, 0) == NK_ERROR_INVALID_ARGUMENT);
    nk_view probe = NK_INVALID_HANDLE;
    assert(nk_view_create(window, NULL, &probe) == NK_ERROR_INVALID_ARGUMENT);
    nk_view_bounds undersized = {0};
    undersized.struct_size = sizeof(undersized) - sizeof(uint64_t);
    assert(nk_view_get_bounds(view, &undersized) == NK_ERROR_INVALID_ARGUMENT);
    assert(nk_view_create(NK_INVALID_HANDLE, &options, &probe) == NK_ERROR_INVALID_HANDLE);

    require_ok("nk_view_destroy", nk_view_destroy(view));
    assert(nk_view_destroy(view) == NK_ERROR_INVALID_HANDLE);
    assert(nk_view_get_bounds(view, &bounds) == NK_ERROR_INVALID_HANDLE);
    assert(nk_view_commit(view) == NK_ERROR_INVALID_HANDLE);
    assert(nk_view_get_native(view, &native) == NK_ERROR_INVALID_HANDLE);

    /* Destroying the parent destroys its views. */
    nk_view child = NK_INVALID_HANDLE;
    require_ok("nk_view_create", nk_view_create(window, &options, &child));
    require_ok("nk_window_destroy", nk_window_destroy(window));
    assert(nk_view_get_bounds(child, &bounds) == NK_ERROR_INVALID_HANDLE);

    nk_shutdown();
    return 0;
}
