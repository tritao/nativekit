#include "nativekit.h"
#include "nativekit_webview.h"
#include "nativekit_window.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    const char *url = argc > 1 ? argv[1] : "https://example.com";
    nk_init_options init = {0};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    if (nk_init(&init) != NK_OK) {
        fprintf(stderr, "init: %s\n", nk_last_error());
        return 1;
    }

    nk_window_options window_options = {0};
    window_options.struct_size = sizeof(window_options);
    window_options.flags = NK_WINDOW_RESIZABLE;
    window_options.width = 1000;
    window_options.height = 700;
    window_options.title = "NativeKit Browser";
    nk_handle window = NK_INVALID_HANDLE;
    if (nk_window_create(&window_options, &window) != NK_OK) {
        fprintf(stderr, "window: %s\n", nk_last_error());
        nk_shutdown();
        return 1;
    }

    nk_webview_options webview_options = {0};
    webview_options.struct_size = sizeof(webview_options);
    webview_options.width = 1000;
    webview_options.height = 700;
    webview_options.initial_url = url;
    nk_handle webview = NK_INVALID_HANDLE;
    if (nk_webview_create(window, &webview_options, &webview) != NK_OK) {
        fprintf(stderr, "webview: %s\n", nk_last_error());
        nk_shutdown();
        return 1;
    }

    int running = 1;
    while (running) {
        nk_event event = {0};
        event.struct_size = sizeof(event);
        if (nk_poll_event(&event) != NK_OK) {
            fprintf(stderr, "event: %s\n", nk_last_error());
            break;
        }
        if (event.kind == NK_EVENT_WINDOW_CLOSE) running = 0;
        if (event.kind == NK_EVENT_WEBVIEW_TITLE_CHANGED && event.data) {
            printf("title: %.*s\n", (int)event.data_size, (const char *)event.data);
        }
        nk_event_release(&event);
    }

    nk_shutdown();
    return 0;
}
