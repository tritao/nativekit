#include "nativekit.h"
#include "nativekit_webview.h"
#include "nativekit_window.h"

#include <assert.h>
#include <unistd.h>

int main(void) {
    nk_init_options init = {0};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    assert(nk_init(&init) == NK_OK);

    nk_window_options window_options = {0};
    window_options.struct_size = sizeof(window_options);
    window_options.flags = NK_WINDOW_RESIZABLE;
    window_options.width = 1000;
    window_options.height = 700;
    window_options.title = "NativeKit resize test";
    nk_handle window = NK_INVALID_HANDLE;
    assert(nk_window_create(&window_options, &window) == NK_OK);

    nk_webview_options webview_options = {0};
    webview_options.struct_size = sizeof(webview_options);
    webview_options.width = 1000;
    webview_options.height = 700;
    nk_handle webview = NK_INVALID_HANDLE;
    assert(nk_webview_create(window, &webview_options, &webview) == NK_OK);

    // A GtkFixed containing a child with a 1000x700 size request must not turn
    // that request into the top-level window's minimum size.
    assert(nk_window_set_bounds(window, 0, 0, 320, 240) == NK_OK);
    int resized_down = 0;
    for (int attempt = 0; attempt < 500 && !resized_down; ++attempt) {
        nk_event event = {0};
        event.struct_size = sizeof(event);
        assert(nk_poll_event(&event) == NK_OK);
        if (event.kind == NK_EVENT_WINDOW_RESIZE && event.source == window &&
            event.data_size >= sizeof(nk_window_resize_event)) {
            const nk_window_resize_event *size = event.data;
            resized_down = size->width <= 320 && size->height <= 240;
        }
        nk_event_release(&event);
        if (!resized_down)
            usleep(10000);
    }
    assert(resized_down);

    assert(nk_webview_destroy(webview) == NK_OK);
    assert(nk_window_destroy(window) == NK_OK);
    nk_shutdown();
    return 0;
}
