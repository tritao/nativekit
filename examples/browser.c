#include "nativekit.h"
#include "nativekit_webview.h"
#include "nativekit_window.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <time.h>
#endif

static void sleep_milliseconds(unsigned milliseconds) {
#if defined(_WIN32)
    Sleep(milliseconds);
#else
    struct timespec delay = {(time_t)(milliseconds / 1000),
                             (long)(milliseconds % 1000) * 1000000L};
    nanosleep(&delay, NULL);
#endif
}

static void print_event_text(const char *label, const nk_event *event) {
    fprintf(stderr, "%s: %.*s\n", label, (int)event->data_size,
            event->data ? (const char *)event->data : "");
}

static int has_prefix(const nk_event *event, const char *prefix) {
    const size_t length = strlen(prefix);
    return event->data && event->data_size >= length &&
           memcmp(event->data, prefix, length) == 0;
}

static int navigation_is_safe(const nk_event *event) {
    return has_prefix(event, "https://") || has_prefix(event, "http://") ||
           has_prefix(event, "file://") || has_prefix(event, "about:");
}

static void update_title(nk_handle window, const nk_event *event) {
    static const char suffix[] = " \xe2\x80\x94 NativeKit Browser";
    char *title = malloc((size_t)event->data_size + sizeof(suffix));
    if (!title)
        return;
    memcpy(title, event->data, (size_t)event->data_size);
    memcpy(title + event->data_size, suffix, sizeof(suffix));
    nk_window_set_title(window, title);
    free(title);
}

static int report_failure(const char *operation, nk_result result) {
    if (result == NK_OK)
        return 0;
    fprintf(stderr, "%s failed (%d): %s\n", operation, result, nk_last_error());
    return 1;
}

int main(int argc, char **argv) {
    if (argc > 2 || (argc == 2 && strcmp(argv[1], "--help") == 0)) {
        printf("Usage: %s [URL]\n", argv[0]);
        return argc > 2;
    }
    const char *url = argc == 2 ? argv[1] : "https://example.com";

    nk_init_options init = {0};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    if (report_failure("nk_init", nk_init(&init)))
        return 1;

    const nk_capabilities capabilities = nk_get_capabilities();
    if ((capabilities & (NK_CAP_WINDOW | NK_CAP_WEBVIEW)) !=
        (NK_CAP_WINDOW | NK_CAP_WEBVIEW)) {
        fprintf(stderr, "This NativeKit backend does not provide windows and WebViews.\n");
        nk_shutdown();
        return 1;
    }

    nk_window_options window_options = {0};
    window_options.struct_size = sizeof(window_options);
    window_options.flags = NK_WINDOW_RESIZABLE;
    window_options.width = 1100;
    window_options.height = 720;
    window_options.title = "NativeKit Browser";
    nk_handle window = NK_INVALID_HANDLE;
    if (report_failure("nk_window_create", nk_window_create(&window_options, &window))) {
        nk_shutdown();
        return 1;
    }

    nk_webview_options webview_options = {0};
    webview_options.struct_size = sizeof(webview_options);
    webview_options.flags = NK_WEBVIEW_DEVTOOLS | NK_WEBVIEW_NAVIGATION_POLICY;
    webview_options.width = window_options.width;
    webview_options.height = window_options.height;
    webview_options.initial_url = url;
    nk_handle webview = NK_INVALID_HANDLE;
    if (report_failure("nk_webview_create",
                       nk_webview_create(window, &webview_options, &webview))) {
        nk_window_destroy(window);
        nk_shutdown();
        return 1;
    }

    int running = 1;
    while (running) {
        nk_event event = {0};
        event.struct_size = sizeof(event);
        if (report_failure("nk_poll_event", nk_poll_event(&event)))
            break;

        switch (event.kind) {
        case NK_EVENT_NONE:
            sleep_milliseconds(8);
            break;
        case NK_EVENT_WINDOW_CLOSE:
            if (event.source == window)
                running = 0;
            break;
        case NK_EVENT_WINDOW_RESIZE:
            if (event.source == window && event.data_size >= sizeof(nk_window_resize_event)) {
                const nk_window_resize_event *size = event.data;
                if (report_failure("nk_webview_set_bounds",
                                   nk_webview_set_bounds(webview, 0, 0, size->width,
                                                         size->height)))
                    running = 0;
            }
            break;
        case NK_EVENT_WEBVIEW_READY:
            fprintf(stderr, "WebView ready. Developer tools are enabled.\n");
            break;
        case NK_EVENT_WEBVIEW_NAVIGATION_REQUEST: {
            const int allow = navigation_is_safe(&event);
            if (!allow)
                print_event_text("Blocked navigation", &event);
            if (report_failure("nk_webview_navigation_decide",
                               nk_webview_navigation_decide(event.request_id, allow)))
                running = 0;
            break;
        }
        case NK_EVENT_WEBVIEW_NAVIGATED:
            print_event_text("Navigated", &event);
            break;
        case NK_EVENT_WEBVIEW_TITLE_CHANGED:
            if (event.data)
                update_title(window, &event);
            break;
        case NK_EVENT_WEBVIEW_NAVIGATION_FAILED:
            print_event_text("Navigation failed", &event);
            break;
        case NK_EVENT_WEBVIEW_PROCESS_TERMINATED:
            fprintf(stderr, "WebView process terminated (reason %u).\n", event.flags);
            running = 0;
            break;
        default:
            break;
        }
        nk_event_release(&event);
    }

    nk_webview_destroy(webview);
    nk_window_destroy(window);
    nk_shutdown();
    return 0;
}
