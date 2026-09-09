#include "nativekit.h"
#include "nativekit_clipboard.h"
#include "nativekit_system.h"
#include "nativekit_webview.h"
#include "nativekit_window.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static nk_event wait_for_event(nk_event_kind kind, nk_request_id request) {
    for (int attempt = 0; attempt < 500; ++attempt) {
        nk_event event = {0};
        event.struct_size = sizeof(event);
        assert(nk_poll_event(&event) == NK_OK);
        if (event.kind == kind && event.request_id == request) return event;
        nk_event_release(&event);
        usleep(10000);
    }
    assert(!"timed out waiting for event");
    nk_event unreachable = {0};
    return unreachable;
}

static void verify_system_string(nk_system_directory_kind kind) {
    uint32_t size = 0;
    assert(nk_system_directory(kind, NULL, &size) == NK_ERROR_BUFFER_TOO_SMALL);
    assert(size > 1);
    char *value = (char *)malloc(size);
    assert(value != NULL);
    uint32_t capacity = size;
    assert(nk_system_directory(kind, value, &capacity) == NK_OK);
    assert(capacity == size);
    assert(strlen(value) + 1 == size);
    free(value);
}

int main(void) {
    nk_init_options init = {0};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    assert(nk_init(&init) == NK_OK);
    assert((nk_get_capabilities() & NK_CAP_WINDOW) != 0);
    assert((nk_get_capabilities() & NK_CAP_FILE_DIALOG) != 0);
    assert((nk_get_capabilities() & NK_CAP_CLIPBOARD) != 0);
    assert((nk_get_capabilities() & NK_CAP_DRAG_DROP) != 0);
    assert((nk_get_capabilities() & NK_CAP_SHELL) != 0);
    assert((nk_get_capabilities() & NK_CAP_WEBVIEW) != 0);

    verify_system_string(NK_DIRECTORY_HOME);
    verify_system_string(NK_DIRECTORY_DESKTOP);
    verify_system_string(NK_DIRECTORY_DOCUMENTS);
    verify_system_string(NK_DIRECTORY_DOWNLOADS);
    verify_system_string(NK_DIRECTORY_CACHE);
    verify_system_string(NK_DIRECTORY_CONFIG);
    verify_system_string(NK_DIRECTORY_DATA);
    verify_system_string(NK_DIRECTORY_TEMP);

    uint32_t locale_size = 0;
    assert(nk_system_locale(NULL, &locale_size) == NK_ERROR_BUFFER_TOO_SMALL);
    assert(locale_size > 1);

    nk_window_options options = {0};
    options.struct_size = sizeof(options);
    options.flags = NK_WINDOW_HIDDEN | NK_WINDOW_RESIZABLE;
    options.width = 640;
    options.height = 480;
    options.title = "NativeKit macOS smoke test";
    nk_handle window = NK_INVALID_HANDLE;
    assert(nk_window_create(&options, &window) == NK_OK);
    assert(nk_window_set_title(window, "NativeKit UTF-8 \xE2\x9C\x93") == NK_OK);
    assert(nk_window_set_bounds(window, 20, 20, 800, 600) == NK_OK);
    float scale = 0.0f;
    assert(nk_window_get_scale(window, &scale) == NK_OK);
    assert(scale >= 1.0f);
    nk_native_window native = {0};
    native.struct_size = sizeof(native);
    assert(nk_window_get_native(window, &native) == NK_OK);
    assert(native.kind == NK_NATIVE_WINDOW_COCOA);
    assert(native.window != 0 && native.view != 0);

    const char clipboard_text[] = "NativeKit pasteboard UTF-8 \xE2\x9C\x93";
    assert(nk_clipboard_set_text(clipboard_text) == NK_OK);
    nk_request_id text_request = NK_INVALID_REQUEST_ID;
    assert(nk_clipboard_read_text(&text_request) == NK_OK);
    nk_event text_event = wait_for_event(NK_EVENT_CLIPBOARD_TEXT_COMPLETE, text_request);
    assert(text_event.data_size == strlen(clipboard_text));
    assert(memcmp(text_event.data, clipboard_text, text_event.data_size) == 0);
    nk_event_release(&text_event);

    const char *clipboard_paths[] = {
        "/tmp/nativekit-pasteboard-a",
        "/tmp/nativekit-pasteboard-\xE2\x9C\x93"
    };
    assert(nk_clipboard_set_files(clipboard_paths, 2) == NK_OK);
    nk_request_id files_request = NK_INVALID_REQUEST_ID;
    assert(nk_clipboard_read_files(&files_request) == NK_OK);
    nk_event files_event = wait_for_event(NK_EVENT_CLIPBOARD_FILES_COMPLETE, files_request);
    assert(files_event.data_count == 2);
    for (uint32_t index = 0; index < 2; ++index) {
        const char *path = NULL;
        uint32_t length = 0;
        assert(nk_clipboard_event_file(&files_event, index, &path, &length) == NK_OK);
        assert(length >= strlen(clipboard_paths[index]));
        assert(memcmp(path + length - strlen(clipboard_paths[index]),
                      clipboard_paths[index], strlen(clipboard_paths[index])) == 0);
    }
    nk_event_release(&files_event);
    assert(nk_window_set_drop_enabled(window, 1) == NK_OK);
    assert(nk_window_set_drop_enabled(window, 0) == NK_OK);

    nk_webview_options web_options = {0};
    web_options.struct_size = sizeof(web_options);
    web_options.flags = NK_WEBVIEW_HIDDEN;
    web_options.x = 10;
    web_options.y = 20;
    web_options.width = 400;
    web_options.height = 300;
    nk_handle webview = NK_INVALID_HANDLE;
    assert(nk_webview_create(window, &web_options, &webview) == NK_OK);
    nk_event ready_event = wait_for_event(NK_EVENT_WEBVIEW_READY, NK_INVALID_REQUEST_ID);
    assert(ready_event.source == webview);
    nk_event_release(&ready_event);
    assert(nk_webview_set_bounds(webview, 15, 25, 420, 320) == NK_OK);
    assert(nk_webview_show(webview, 1) == NK_OK);
    assert(nk_webview_set_html(webview,
        "<html><head><title>NativeKit WebView</title></head><body>ready</body></html>",
        "https://nativekit.invalid/") == NK_OK);
    nk_event navigated_event = wait_for_event(
        NK_EVENT_WEBVIEW_NAVIGATED, NK_INVALID_REQUEST_ID);
    assert(navigated_event.source == webview);
    nk_event_release(&navigated_event);
    nk_request_id eval_request = NK_INVALID_REQUEST_ID;
    assert(nk_webview_eval(webview, "6 * 7", &eval_request) == NK_OK);
    nk_event eval_event = wait_for_event(NK_EVENT_WEBVIEW_EVAL_COMPLETE, eval_request);
    assert(eval_event.source == webview && eval_event.result == NK_OK);
    assert(eval_event.data_size == 2 && memcmp(eval_event.data, "42", 2) == 0);
    nk_event_release(&eval_event);
    nk_request_id message_request = NK_INVALID_REQUEST_ID;
    assert(nk_webview_eval(webview,
        "window.webkit.messageHandlers.nativekit.postMessage('hello'); true",
        &message_request) == NK_OK);
    nk_event message_event = wait_for_event(NK_EVENT_WEBVIEW_MESSAGE, NK_INVALID_REQUEST_ID);
    assert(message_event.source == webview);
    assert(message_event.data_size == 5 && memcmp(message_event.data, "hello", 5) == 0);
    nk_event_release(&message_event);
    assert(nk_webview_destroy(webview) == NK_OK);
    assert(nk_webview_destroy(webview) == NK_ERROR_INVALID_HANDLE);

    nk_system_appearance appearance = {0};
    appearance.struct_size = sizeof(appearance);
    assert(nk_system_get_appearance(&appearance) == NK_OK);
    assert(appearance.color_scheme == NK_COLOR_SCHEME_LIGHT ||
           appearance.color_scheme == NK_COLOR_SCHEME_DARK);
    assert(nk_window_destroy(window) == NK_OK);
    assert(nk_window_destroy(window) == NK_ERROR_INVALID_HANDLE);
    nk_shutdown();
    return 0;
}
