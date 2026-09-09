#include "nativekit.h"
#include "nativekit_dialog.h"
#include "nativekit_clipboard.h"
#include "nativekit_system.h"
#include "nativekit_webview.h"
#include "nativekit_window.h"

#include <assert.h>
#include <string.h>
#include <unistd.h>

static nk_event wait_for_event(nk_event_kind kind, nk_handle source) {
    for (int attempt = 0; attempt < 500; ++attempt) {
        nk_event event = {0};
        event.struct_size = sizeof(event);
        assert(nk_poll_event(&event) == NK_OK);
        if (event.kind == kind && event.source == source) return event;
        nk_event_release(&event);
        usleep(10000);
    }
    assert(!"timed out waiting for event");
    nk_event unreachable = {0};
    return unreachable;
}

int main(void) {
    nk_init_options init = {0};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    assert(nk_init(&init) == NK_OK);
    const nk_capabilities expected = NK_CAP_WINDOW | NK_CAP_WEBVIEW |
        NK_CAP_FILE_DIALOG | NK_CAP_CLIPBOARD | NK_CAP_DRAG_DROP |
        NK_CAP_SHELL | NK_CAP_SYSTEM_APPEARANCE;
    assert((nk_get_capabilities() & expected) == expected);
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
    nk_native_window native = {0};
    native.struct_size = sizeof(native);
    assert(nk_window_get_native(window, &native) == NK_OK);
    assert(native.kind == NK_NATIVE_WINDOW_X11);
    assert(native.display != 0);
    assert(native.window != 0);
    assert(native.flags == 0);
    nk_handle wrapped = NK_INVALID_HANDLE;
    assert(nk_window_wrap_native(&native, &wrapped) == NK_ERROR_UNSUPPORTED);
    assert(wrapped == NK_INVALID_HANDLE);
    float scale = 0.0f;
    assert(nk_window_get_scale(window, &scale) == NK_OK);
    assert(scale >= 1.0f);
    nk_system_appearance appearance = {0};
    appearance.struct_size = sizeof(appearance);
    assert(nk_system_get_appearance(&appearance) == NK_OK);
    assert(appearance.color_scheme == NK_COLOR_SCHEME_LIGHT ||
           appearance.color_scheme == NK_COLOR_SCHEME_DARK);
    assert(nk_window_set_drop_enabled(window, 1) == NK_OK);
    assert(nk_window_set_drop_enabled(window, 1) == NK_OK);
    assert(nk_window_set_drop_enabled(window, 0) == NK_OK);

    assert(nk_clipboard_set_text("clipboard text") == NK_OK);
    nk_request_id clipboard_text_request = NK_INVALID_REQUEST_ID;
    assert(nk_clipboard_read_text(&clipboard_text_request) == NK_OK);
    int received_clipboard_text = 0;
    for (int attempt = 0; attempt < 100 && !received_clipboard_text; ++attempt) {
        nk_event event = {0};
        event.struct_size = sizeof(event);
        assert(nk_poll_event(&event) == NK_OK);
        if (event.kind == NK_EVENT_CLIPBOARD_TEXT_COMPLETE &&
            event.request_id == clipboard_text_request) {
            assert(event.data_size == 14);
            assert(memcmp(event.data, "clipboard text", 14) == 0);
            received_clipboard_text = 1;
        }
        nk_event_release(&event);
        if (!received_clipboard_text) usleep(10000);
    }
    assert(received_clipboard_text);

    const char *clipboard_files[] = {"/tmp/nativekit-a", "/tmp/nativekit-b"};
    assert(nk_clipboard_set_files(clipboard_files, 2) == NK_OK);
    nk_request_id clipboard_files_request = NK_INVALID_REQUEST_ID;
    assert(nk_clipboard_read_files(&clipboard_files_request) == NK_OK);
    int received_clipboard_files = 0;
    for (int attempt = 0; attempt < 100 && !received_clipboard_files; ++attempt) {
        nk_event event = {0};
        event.struct_size = sizeof(event);
        assert(nk_poll_event(&event) == NK_OK);
        if (event.kind == NK_EVENT_CLIPBOARD_FILES_COMPLETE &&
            event.request_id == clipboard_files_request) {
            assert(event.data_count == 2);
            const char *path = NULL;
            uint32_t length = 0;
            assert(nk_clipboard_event_file(&event, 1, &path, &length) == NK_OK);
            assert(length == 16);
            assert(memcmp(path, "/tmp/nativekit-b", 16) == 0);
            received_clipboard_files = 1;
        }
        nk_event_release(&event);
        if (!received_clipboard_files) usleep(10000);
    }
    assert(received_clipboard_files);

    nk_webview_options webview_options = {0};
    webview_options.struct_size = sizeof(webview_options);
    webview_options.flags = NK_WEBVIEW_HIDDEN;
    webview_options.width = 640;
    webview_options.height = 480;
    nk_handle webview = NK_INVALID_HANDLE;
    assert(nk_webview_create(window, &webview_options, &webview) == NK_OK);
    int received_ready = 0;
    for (int attempt = 0; attempt < 100 && !received_ready; ++attempt) {
        nk_event event = {0};
        event.struct_size = sizeof(event);
        assert(nk_poll_event(&event) == NK_OK);
        if (event.kind == NK_EVENT_WEBVIEW_READY && event.source == webview)
            received_ready = 1;
        nk_event_release(&event);
    }
    assert(received_ready);
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

    nk_webview_options policy_options = webview_options;
    policy_options.flags |= NK_WEBVIEW_NAVIGATION_POLICY;
    nk_handle policy_webview = NK_INVALID_HANDLE;
    assert(nk_webview_create(window, &policy_options, &policy_webview) == NK_OK);
    nk_event policy_ready = wait_for_event(NK_EVENT_WEBVIEW_READY, policy_webview);
    nk_event_release(&policy_ready);
    const char policy_url[] = "data:text/html,NativeKit-policy";
    assert(nk_webview_navigate(policy_webview, policy_url) == NK_OK);
    nk_event policy_request = wait_for_event(
        NK_EVENT_WEBVIEW_NAVIGATION_REQUEST, policy_webview);
    assert(policy_request.request_id != NK_INVALID_REQUEST_ID);
    assert(policy_request.data_size == strlen(policy_url));
    assert(memcmp(policy_request.data, policy_url, policy_request.data_size) == 0);
    assert(nk_webview_navigation_decide(policy_request.request_id, 1) == NK_OK);
    assert(nk_webview_navigation_decide(policy_request.request_id, 1) ==
           NK_ERROR_INVALID_REQUEST);
    nk_event_release(&policy_request);
    nk_event policy_navigated = wait_for_event(NK_EVENT_WEBVIEW_NAVIGATED, policy_webview);
    nk_event_release(&policy_navigated);
    assert(nk_webview_navigate(policy_webview, "data:text/html,cancel") == NK_OK);
    nk_event cancelled_request = wait_for_event(
        NK_EVENT_WEBVIEW_NAVIGATION_REQUEST, policy_webview);
    const nk_request_id cancelled_id = cancelled_request.request_id;
    nk_event_release(&cancelled_request);
    assert(nk_webview_destroy(policy_webview) == NK_OK);
    assert(nk_webview_navigation_decide(cancelled_id, 1) == NK_ERROR_INVALID_REQUEST);

    nk_file_dialog_options file_options = {0};
    file_options.struct_size = sizeof(file_options);
    file_options.title = "NativeKit cancellation test";
    nk_request_id dialog_request = NK_INVALID_REQUEST_ID;
    assert(nk_dialog_open_file(window, &file_options, &dialog_request) == NK_OK);
    assert(dialog_request != NK_INVALID_REQUEST_ID);
    assert(nk_dialog_cancel(dialog_request) == NK_OK);
    assert(nk_dialog_cancel(dialog_request) == NK_ERROR_INVALID_REQUEST);
    int received_dialog = 0;
    for (int attempt = 0; attempt < 100 && !received_dialog; ++attempt) {
        nk_event event = {0};
        event.struct_size = sizeof(event);
        assert(nk_poll_event(&event) == NK_OK);
        if (event.kind == NK_EVENT_DIALOG_COMPLETE &&
            event.request_id == dialog_request) {
            assert(event.flags == NK_DIALOG_OPEN_FILE);
            assert(event.data_size >= sizeof(nk_dialog_paths));
            const nk_dialog_paths *paths = (const nk_dialog_paths *)event.data;
            assert(paths->accepted == 0);
            assert(paths->path_count == 0);
            const char *path = NULL;
            uint32_t path_length = 0;
            assert(nk_dialog_event_path(&event, 0, &path, &path_length) ==
                   NK_ERROR_INVALID_ARGUMENT);
            received_dialog = 1;
        }
        nk_event_release(&event);
    }
    assert(received_dialog);

    nk_message_dialog_options message_options = {0};
    message_options.struct_size = sizeof(message_options);
    message_options.kind = NK_MESSAGE_QUESTION;
    message_options.buttons = NK_MESSAGE_BUTTON_YES | NK_MESSAGE_BUTTON_NO;
    message_options.title = "NativeKit message test";
    message_options.message = "Cancel this dialog";
    nk_request_id message_request = NK_INVALID_REQUEST_ID;
    assert(nk_dialog_message(window, &message_options, &message_request) == NK_OK);
    assert(nk_dialog_cancel(message_request) == NK_OK);
    int received_message_dialog = 0;
    for (int attempt = 0; attempt < 100 && !received_message_dialog; ++attempt) {
        nk_event event = {0};
        event.struct_size = sizeof(event);
        assert(nk_poll_event(&event) == NK_OK);
        if (event.kind == NK_EVENT_DIALOG_COMPLETE &&
            event.request_id == message_request) {
            assert(event.flags == NK_DIALOG_MESSAGE);
            assert(event.data_size == sizeof(nk_dialog_message_result));
            const nk_dialog_message_result *result =
                (const nk_dialog_message_result *)event.data;
            assert(result->button == NK_MESSAGE_RESULT_CANCEL);
            received_message_dialog = 1;
        }
        nk_event_release(&event);
    }
    assert(received_message_dialog);

    /* Destroying a parent invalidates all of its borrowed child handles. */
    assert(nk_window_destroy(window) == NK_OK);
    assert(nk_webview_destroy(webview) == NK_ERROR_INVALID_HANDLE);
    assert(nk_window_destroy(window) == NK_ERROR_INVALID_HANDLE);

    nk_shutdown();
    return 0;
}
