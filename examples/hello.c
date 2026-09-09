#include "nativekit.h"
#include "nativekit_webview.h"
#include "nativekit_window.h"

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <time.h>
#endif

static const char page[] =
    "<!doctype html><meta charset=utf-8><meta name=viewport "
    "content='width=device-width,initial-scale=1'><title>Hello NativeKit</title>"
    "<style>html{color-scheme:light dark;font:18px system-ui}body{min-height:100vh;margin:0;"
    "display:grid;place-items:center;background:#18212f;color:#edf3fa}.card{max-width:32rem;"
    "padding:3rem;border-radius:1.2rem;background:#243247;box-shadow:0 1rem 4rem #0006}"
    "h1{margin-top:0;color:#70d6ff}button{font:inherit;padding:.65rem 1rem;border:0;"
    "border-radius:.5rem;background:#70d6ff;color:#102030;cursor:pointer}</style>"
    "<main class=card><h1>Hello from NativeKit</h1><p>This interface is HTML in a native "
    "WebView. The button sends a JSON message back to the C event loop.</p>"
    "<button onclick=\"window.webkit.messageHandlers.nativekit.postMessage({command:'quit'})\">"
    "Close window</button></main>";

static void sleep_milliseconds(unsigned milliseconds) {
#if defined(_WIN32)
    Sleep(milliseconds);
#else
    struct timespec delay = {(time_t)(milliseconds / 1000),
                             (long)(milliseconds % 1000) * 1000000L};
    nanosleep(&delay, NULL);
#endif
}

static int failed(const char *operation, nk_result result) {
    if (result == NK_OK)
        return 0;
    fprintf(stderr, "%s failed (%d): %s\n", operation, result, nk_last_error());
    return 1;
}

int main(void) {
    nk_init_options init = {0};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    if (failed("nk_init", nk_init(&init)))
        return 1;

    nk_window_options window_options = {0};
    window_options.struct_size = sizeof(window_options);
    window_options.flags = NK_WINDOW_RESIZABLE;
    window_options.width = 720;
    window_options.height = 480;
    window_options.title = "Hello NativeKit";
    nk_handle window = NK_INVALID_HANDLE;
    if (failed("nk_window_create", nk_window_create(&window_options, &window))) {
        nk_shutdown();
        return 1;
    }

    nk_webview_options webview_options = {0};
    webview_options.struct_size = sizeof(webview_options);
    webview_options.width = window_options.width;
    webview_options.height = window_options.height;
    nk_handle webview = NK_INVALID_HANDLE;
    if (failed("nk_webview_create", nk_webview_create(window, &webview_options, &webview)) ||
        failed("nk_webview_set_html", nk_webview_set_html(webview, page, NULL))) {
        nk_window_destroy(window);
        nk_shutdown();
        return 1;
    }

    int running = 1;
    while (running) {
        nk_event event = {0};
        event.struct_size = sizeof(event);
        if (failed("nk_poll_event", nk_poll_event(&event)))
            break;
        if (event.kind == NK_EVENT_NONE) {
            sleep_milliseconds(8);
        } else if (event.kind == NK_EVENT_WINDOW_CLOSE && event.source == window) {
            running = 0;
        } else if (event.kind == NK_EVENT_WINDOW_RESIZE && event.source == window &&
                   event.data_size >= sizeof(nk_window_resize_event)) {
            const nk_window_resize_event *size = event.data;
            if (failed("nk_webview_set_bounds",
                       nk_webview_set_bounds(webview, 0, 0, size->width, size->height)))
                running = 0;
        } else if (event.kind == NK_EVENT_WEBVIEW_MESSAGE && event.source == webview &&
                   event.data_size == strlen("{\"command\":\"quit\"}") &&
                   memcmp(event.data, "{\"command\":\"quit\"}", event.data_size) == 0) {
            running = 0;
        }
        nk_event_release(&event);
    }

    nk_webview_destroy(webview);
    nk_window_destroy(window);
    nk_shutdown();
    return 0;
}
