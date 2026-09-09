#include "nativekit.h"
#include "nativekit_webview.h"
#include "nativekit_window.h"

#include <assert.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    nk_init_options init = {0};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    assert(nk_init(&init) == NK_OK);
    assert((nk_get_capabilities() & (NK_CAP_WINDOW | NK_CAP_WEBVIEW)) ==
           (NK_CAP_WINDOW | NK_CAP_WEBVIEW));
    assert(nk_window_create(NULL, NULL) == NK_ERROR_INVALID_ARGUMENT);
    assert(nk_webview_navigate(NK_INVALID_HANDLE, NULL) == NK_ERROR_INVALID_ARGUMENT);

    nk_window_options window_options = {0};
    window_options.struct_size = sizeof(window_options);
    window_options.flags = NK_WINDOW_HIDDEN | NK_WINDOW_RESIZABLE;
    window_options.width = 640;
    window_options.height = 480;
    window_options.title = "NativeKit integration test";
    nk_handle window = NK_INVALID_HANDLE;
    assert(nk_window_create(&window_options, &window) == NK_OK);
    float scale = 0.0f;
    assert(nk_window_get_scale(window, &scale) == NK_OK);
    assert(scale >= 1.0f);

    nk_webview_options webview_options = {0};
    webview_options.struct_size = sizeof(webview_options);
    webview_options.flags = NK_WEBVIEW_HIDDEN;
    webview_options.width = 640;
    webview_options.height = 480;
    nk_handle webview = NK_INVALID_HANDLE;
    assert(nk_webview_create(window, &webview_options, &webview) == NK_OK);
    assert(nk_webview_set_html(
               webview,
               "<title>NativeKit</title><script>"
               "window.webkit.messageHandlers.nativekit.postMessage('hello');"
               "</script>",
               NULL) == NK_OK);

    int received_message = 0;
    for (int attempt = 0; attempt < 500 && !received_message; ++attempt) {
        nk_event event = {0};
        event.struct_size = sizeof(event);
        assert(nk_poll_event(&event) == NK_OK);
        if (event.kind == NK_EVENT_WEBVIEW_MESSAGE) {
            assert(event.source == webview);
            assert(event.data_size == 5);
            assert(memcmp(event.data, "hello", 5) == 0);
            received_message = 1;
        }
        nk_event_release(&event);
        if (!received_message) usleep(10000);
    }
    assert(received_message);

    nk_request_id request = NK_INVALID_REQUEST_ID;
    assert(nk_webview_eval(webview, "6 * 7", &request) == NK_OK);
    assert(request != NK_INVALID_REQUEST_ID);
    int received_result = 0;
    for (int attempt = 0; attempt < 500 && !received_result; ++attempt) {
        nk_event event = {0};
        event.struct_size = sizeof(event);
        assert(nk_poll_event(&event) == NK_OK);
        if (event.kind == NK_EVENT_WEBVIEW_EVAL_COMPLETE) {
            assert(event.source == webview);
            assert(event.request_id == request);
            assert(event.result == NK_OK);
            assert(event.data_size == 2);
            assert(memcmp(event.data, "42", 2) == 0);
            received_result = 1;
        }
        nk_event_release(&event);
        if (!received_result) usleep(10000);
    }
    assert(received_result);

    /* Destroying a parent invalidates all of its borrowed child handles. */
    assert(nk_window_destroy(window) == NK_OK);
    assert(nk_webview_destroy(webview) == NK_ERROR_INVALID_HANDLE);
    assert(nk_window_destroy(window) == NK_ERROR_INVALID_HANDLE);

    nk_shutdown();
    return 0;
}
