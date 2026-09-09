#include "nativekit.h"
#include "nativekit_clipboard.h"
#include "nativekit_dialog.h"
#include "nativekit_notification.h"
#include "nativekit_system.h"
#include "nativekit_webview.h"
#include "nativekit_window.h"

#include <assert.h>
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdlib.h>
#include <string.h>

static nk_event wait_for_dialog(nk_request_id request) {
    for (int attempt = 0; attempt < 1000; ++attempt) {
        nk_event event = {0};
        event.struct_size = sizeof(event);
        assert(nk_poll_event(&event) == NK_OK);
        if (event.kind == NK_EVENT_DIALOG_COMPLETE && event.request_id == request)
            return event;
        nk_event_release(&event);
        Sleep(10);
    }
    assert(!"timed out waiting for dialog completion");
    nk_event unreachable = {0};
    return unreachable;
}

static nk_event wait_for_event(nk_event_kind kind, nk_request_id request) {
    for (int attempt = 0; attempt < 1000; ++attempt) {
        nk_event event = {0};
        event.struct_size = sizeof(event);
        assert(nk_poll_event(&event) == NK_OK);
        if (event.kind == kind && event.request_id == request)
            return event;
        nk_event_release(&event);
        Sleep(10);
    }
    assert(!"timed out waiting for event");
    nk_event unreachable = {0};
    return unreachable;
}

static nk_event wait_for_kind(nk_event_kind kind) {
    for (int attempt = 0; attempt < 1000; ++attempt) {
        nk_event event = {0};
        event.struct_size = sizeof(event);
        assert(nk_poll_event(&event) == NK_OK);
        if (event.kind == kind) return event;
        nk_event_release(&event);
        Sleep(10);
    }
    assert(!"timed out waiting for event kind");
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
    assert((nk_get_capabilities() & NK_CAP_CLIPBOARD) != 0);
    assert((nk_get_capabilities() & NK_CAP_DRAG_DROP) != 0);
    assert((nk_get_capabilities() & NK_CAP_SHELL) != 0);
    assert((nk_get_capabilities() & NK_CAP_SYSTEM_APPEARANCE) != 0);
    assert((nk_get_capabilities() & NK_CAP_NOTIFICATION) != 0);
    assert(nk_notification_show(NULL, NULL) == NK_ERROR_INVALID_ARGUMENT);
    assert(nk_notification_close(NK_INVALID_REQUEST_ID) == NK_ERROR_INVALID_REQUEST);

    verify_system_string(NK_DIRECTORY_HOME);
    verify_system_string(NK_DIRECTORY_DESKTOP);
    verify_system_string(NK_DIRECTORY_DOCUMENTS);
    verify_system_string(NK_DIRECTORY_DOWNLOADS);
    verify_system_string(NK_DIRECTORY_CACHE);
    verify_system_string(NK_DIRECTORY_CONFIG);
    verify_system_string(NK_DIRECTORY_DATA);
    verify_system_string(NK_DIRECTORY_TEMP);
    uint32_t invalid_size = 0;
    assert(nk_system_directory(9999, NULL, &invalid_size) == NK_ERROR_UNSUPPORTED);

    uint32_t locale_size = 0;
    assert(nk_system_locale(NULL, &locale_size) == NK_ERROR_BUFFER_TOO_SMALL);
    assert(locale_size > 1);
    char *locale = (char *)malloc(locale_size);
    assert(locale != NULL);
    uint32_t locale_capacity = locale_size;
    assert(nk_system_locale(locale, &locale_capacity) == NK_OK);
    assert(strlen(locale) + 1 == locale_size);
    free(locale);

    nk_system_appearance appearance = {0};
    appearance.struct_size = sizeof(appearance);
    assert(nk_system_get_appearance(&appearance) == NK_OK);
    assert(appearance.color_scheme == NK_COLOR_SCHEME_LIGHT ||
           appearance.color_scheme == NK_COLOR_SCHEME_DARK);
    assert(nk_shell_open_url(NULL) == NK_ERROR_INVALID_ARGUMENT);
    assert(nk_shell_open_url("not a URL") == NK_ERROR_INVALID_ARGUMENT);
    assert(nk_shell_open_file("") == NK_ERROR_INVALID_ARGUMENT);
    assert(nk_shell_reveal_file(NULL) == NK_ERROR_INVALID_ARGUMENT);

    nk_window_options options = {0};
    options.struct_size = sizeof(options);
    options.flags = NK_WINDOW_HIDDEN | NK_WINDOW_RESIZABLE;
    options.width = 640;
    options.height = 480;
    options.title = "NativeKit Wine smoke test";
    nk_handle window = NK_INVALID_HANDLE;
    assert(nk_window_create(&options, &window) == NK_OK);
    assert(window != NK_INVALID_HANDLE);
    assert(nk_window_set_title(window, "NativeKit UTF-8 \xE2\x9C\x93") == NK_OK);

    float scale = 0.0f;
    assert(nk_window_get_scale(window, &scale) == NK_OK);
    assert(scale > 0.0f);
    nk_native_window native = {0};
    native.struct_size = sizeof(native);
    assert(nk_window_get_native(window, &native) == NK_OK);
    assert(native.kind == NK_NATIVE_WINDOW_WIN32);
    assert(native.window != 0);

    assert(nk_window_show(window, 1) == NK_OK);
    assert(nk_window_set_bounds(window, 20, 20, 800, 600) == NK_OK);
    for (int index = 0; index < 10; ++index) {
        nk_event event = {0};
        event.struct_size = sizeof(event);
        assert(nk_poll_event(&event) == NK_OK);
        nk_event_release(&event);
    }

    const char clipboard_text[] = "NativeKit clipboard UTF-8 \xE2\x9C\x93";
    assert(nk_clipboard_set_text(clipboard_text) == NK_OK);
    nk_request_id text_request = NK_INVALID_REQUEST_ID;
    assert(nk_clipboard_read_text(&text_request) == NK_OK);
    nk_event text_event = wait_for_event(NK_EVENT_CLIPBOARD_TEXT_COMPLETE, text_request);
    assert(text_event.result == NK_OK);
    assert(text_event.data_size == strlen(clipboard_text));
    assert(memcmp(text_event.data, clipboard_text, text_event.data_size) == 0);
    nk_event_release(&text_event);

    const char *clipboard_paths[] = {
        "C:\\NativeKit clipboard one.txt",
        "C:\\NativeKit clipboard \xE2\x9C\x93.txt"
    };
    assert(nk_clipboard_set_files(clipboard_paths, 2) == NK_OK);
    nk_request_id files_request = NK_INVALID_REQUEST_ID;
    assert(nk_clipboard_read_files(&files_request) == NK_OK);
    nk_event files_event = wait_for_event(NK_EVENT_CLIPBOARD_FILES_COMPLETE, files_request);
    assert(files_event.result == NK_OK);
    assert(files_event.data_count == 2);
    for (uint32_t index = 0; index < 2; ++index) {
        const char *path = NULL;
        uint32_t length = 0;
        assert(nk_clipboard_event_file(&files_event, index, &path, &length) == NK_OK);
        assert(length == strlen(clipboard_paths[index]));
        assert(memcmp(path, clipboard_paths[index], length) == 0);
    }
    nk_event_release(&files_event);

    assert(nk_window_set_drop_enabled(window, 1) == NK_OK);
    const wchar_t dropped_path[] = L"C:\\NativeKit dropped.txt";
    const size_t drop_size = sizeof(DROPFILES) + sizeof(dropped_path) + sizeof(wchar_t);
    HGLOBAL drop_memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, drop_size);
    assert(drop_memory != NULL);
    DROPFILES *drop = (DROPFILES *)GlobalLock(drop_memory);
    assert(drop != NULL);
    drop->pFiles = sizeof(DROPFILES);
    drop->pt.x = 17;
    drop->pt.y = 23;
    drop->fWide = TRUE;
    memcpy((unsigned char *)drop + drop->pFiles, dropped_path, sizeof(dropped_path));
    GlobalUnlock(drop_memory);
    SendMessageW((HWND)native.window, WM_DROPFILES, (WPARAM)drop_memory, 0);
    nk_event drop_event = wait_for_event(NK_EVENT_DROP_FILES, NK_INVALID_REQUEST_ID);
    assert(drop_event.source == window);
    assert(drop_event.data_count == 1);
    const nk_drop_data *drop_header = (const nk_drop_data *)drop_event.data;
    assert(drop_header->x == 17 && drop_header->y == 23);
    const char *drop_path = NULL;
    uint32_t drop_path_length = 0;
    assert(nk_drop_event_item(&drop_event, 0, &drop_path, &drop_path_length) == NK_OK);
    assert(drop_path_length == strlen("C:\\NativeKit dropped.txt"));
    assert(memcmp(drop_path, "C:\\NativeKit dropped.txt", drop_path_length) == 0);
    nk_event_release(&drop_event);
    assert(nk_window_set_drop_enabled(window, 0) == NK_OK);

    nk_webview_options webview_options = {0};
    webview_options.struct_size = sizeof(webview_options);
    webview_options.flags = NK_WEBVIEW_HIDDEN;
    webview_options.width = 320;
    webview_options.height = 240;
    nk_handle webview = NK_INVALID_HANDLE;
    if (nk_get_capabilities() & NK_CAP_WEBVIEW) {
        assert(nk_webview_create(window, &webview_options, &webview) == NK_OK);
        const char webview_html[] =
            "<title>NativeKit title event</title>"
            "<script>window.webkit.messageHandlers.nativekit.postMessage({answer:[42,true,'hello',null]})</script>";
        assert(nk_webview_set_html(webview, webview_html, "https://nativekit.invalid/") == NK_OK);
        nk_request_id eval_request = NK_INVALID_REQUEST_ID;
        assert(nk_webview_eval(webview, "6 * 7", &eval_request) == NK_OK);

        int saw_ready = 0;
        int saw_navigation = 0;
        int saw_title = 0;
        int saw_message = 0;
        int saw_evaluation = 0;
        for (int attempt = 0; attempt < 1000 &&
             !(saw_ready && saw_navigation && saw_title && saw_message && saw_evaluation);
             ++attempt) {
            nk_event event = {0};
            event.struct_size = sizeof(event);
            assert(nk_poll_event(&event) == NK_OK);
            if (event.source == webview && event.kind == NK_EVENT_WEBVIEW_READY)
                saw_ready = 1;
            else if (event.source == webview && event.kind == NK_EVENT_WEBVIEW_NAVIGATED)
                saw_navigation = 1;
            else if (event.source == webview && event.kind == NK_EVENT_WEBVIEW_TITLE_CHANGED &&
                     event.data_size == strlen("NativeKit title event") &&
                     memcmp(event.data, "NativeKit title event", event.data_size) == 0)
                saw_title = 1;
            else if (event.source == webview && event.kind == NK_EVENT_WEBVIEW_MESSAGE &&
                     event.result == NK_OK &&
                     event.data_size == strlen("{\"answer\":[42,true,\"hello\",null]}") &&
                     memcmp(event.data, "{\"answer\":[42,true,\"hello\",null]}",
                            event.data_size) == 0)
                saw_message = 1;
            else if (event.source == webview && event.kind == NK_EVENT_WEBVIEW_EVAL_COMPLETE &&
                     event.request_id == eval_request) {
                assert(event.result == NK_OK);
                assert(event.data_size == 2);
                assert(memcmp(event.data, "42", 2) == 0);
                saw_evaluation = 1;
            }
            nk_event_release(&event);
            Sleep(10);
        }
        assert(saw_ready && saw_navigation && saw_title && saw_message && saw_evaluation);
        nk_request_id invalid_json_request = NK_INVALID_REQUEST_ID;
        assert(nk_webview_eval(webview, "undefined", &invalid_json_request) == NK_OK);
        nk_event invalid_json = wait_for_event(
            NK_EVENT_WEBVIEW_EVAL_COMPLETE, invalid_json_request);
        assert(invalid_json.source == webview && invalid_json.result != NK_OK);
        nk_event_release(&invalid_json);
        assert(nk_webview_set_bounds(webview, 4, 5, 300, 200) == NK_OK);
        assert(nk_webview_show(webview, 1) == NK_OK);
        assert(nk_webview_destroy(webview) == NK_OK);
        assert(nk_webview_destroy(webview) == NK_ERROR_INVALID_HANDLE);

        webview_options.flags = NK_WEBVIEW_HIDDEN | NK_WEBVIEW_NAVIGATION_POLICY;
        nk_handle policy_webview = NK_INVALID_HANDLE;
        assert(nk_webview_create(window, &webview_options, &policy_webview) == NK_OK);
        nk_event policy_ready = wait_for_event(
            NK_EVENT_WEBVIEW_READY, NK_INVALID_REQUEST_ID);
        assert(policy_ready.source == policy_webview);
        nk_event_release(&policy_ready);
        const char policy_url[] = "data:text/html,NativeKit-policy";
        assert(nk_webview_navigate(policy_webview, policy_url) == NK_OK);
        nk_event policy_request = wait_for_kind(NK_EVENT_WEBVIEW_NAVIGATION_REQUEST);
        assert(policy_request.source == policy_webview);
        assert(policy_request.data_size == strlen(policy_url));
        assert(memcmp(policy_request.data, policy_url, policy_request.data_size) == 0);
        assert(nk_webview_navigation_decide(policy_request.request_id, 1) == NK_OK);
        assert(nk_webview_navigation_decide(policy_request.request_id, 1) ==
               NK_ERROR_INVALID_REQUEST);
        nk_event_release(&policy_request);
        nk_event policy_navigated = wait_for_event(
            NK_EVENT_WEBVIEW_NAVIGATED, NK_INVALID_REQUEST_ID);
        assert(policy_navigated.source == policy_webview);
        nk_event_release(&policy_navigated);
        assert(nk_webview_navigate(policy_webview, "data:text/html,cancel") == NK_OK);
        nk_event cancelled_request = wait_for_kind(NK_EVENT_WEBVIEW_NAVIGATION_REQUEST);
        const nk_request_id cancelled_id = cancelled_request.request_id;
        nk_event_release(&cancelled_request);
        assert(nk_webview_destroy(policy_webview) == NK_OK);
        assert(nk_webview_navigation_decide(cancelled_id, 1) == NK_ERROR_INVALID_REQUEST);
    } else {
        assert(nk_webview_create(window, &webview_options, &webview) ==
               NK_ERROR_UNSUPPORTED);
    }

    nk_file_dialog_options file_options = {0};
    file_options.struct_size = sizeof(file_options);
    file_options.title = "NativeKit cancellation test";
    nk_request_id file_request = NK_INVALID_REQUEST_ID;
    assert(nk_dialog_open_file(window, &file_options, &file_request) == NK_OK);
    assert(nk_dialog_cancel(file_request) == NK_OK);
    nk_event file_event = wait_for_dialog(file_request);
    assert(file_event.flags == NK_DIALOG_OPEN_FILE);
    assert(file_event.result == NK_OK);
    assert(file_event.data_size >= sizeof(nk_dialog_paths));
    const nk_dialog_paths *paths = (const nk_dialog_paths *)file_event.data;
    assert(paths->accepted == 0);
    assert(paths->path_count == 0);
    nk_event_release(&file_event);
    assert(nk_dialog_cancel(file_request) == NK_ERROR_INVALID_REQUEST);

    nk_message_dialog_options message_options = {0};
    message_options.struct_size = sizeof(message_options);
    message_options.kind = NK_MESSAGE_QUESTION;
    message_options.buttons = NK_MESSAGE_BUTTON_YES | NK_MESSAGE_BUTTON_NO |
                              NK_MESSAGE_BUTTON_CANCEL;
    message_options.title = "NativeKit cancellation test";
    message_options.message = "This dialog should be canceled automatically.";
    nk_request_id message_request = NK_INVALID_REQUEST_ID;
    assert(nk_dialog_message(window, &message_options, &message_request) == NK_OK);
    assert(nk_dialog_cancel(message_request) == NK_OK);
    nk_event message_event = wait_for_dialog(message_request);
    assert(message_event.flags == NK_DIALOG_MESSAGE);
    assert(message_event.result == NK_OK);
    assert(message_event.data_size == sizeof(nk_dialog_message_result));
    const nk_dialog_message_result *message_result =
        (const nk_dialog_message_result *)message_event.data;
    assert(message_result->button == NK_MESSAGE_RESULT_CANCEL);
    nk_event_release(&message_event);

    if (nk_get_capabilities() & NK_CAP_WEBVIEW) {
        nk_handle pending_webview = NK_INVALID_HANDLE;
        assert(nk_webview_create(window, &webview_options, &pending_webview) == NK_OK);
        nk_request_id destroyed_eval_request = NK_INVALID_REQUEST_ID;
        assert(nk_webview_eval(pending_webview, "42", &destroyed_eval_request) == NK_OK);
        assert(nk_window_destroy(window) == NK_OK);
        nk_event destroyed_eval = wait_for_event(
            NK_EVENT_WEBVIEW_EVAL_COMPLETE, destroyed_eval_request);
        assert(destroyed_eval.source == pending_webview);
        assert(destroyed_eval.result == NK_ERROR_INVALID_REQUEST);
        nk_event_release(&destroyed_eval);
        assert(nk_webview_destroy(pending_webview) == NK_ERROR_INVALID_HANDLE);
        for (int index = 0; index < 100; ++index) {
            nk_event event = {0};
            event.struct_size = sizeof(event);
            assert(nk_poll_event(&event) == NK_OK);
            nk_event_release(&event);
            Sleep(1);
        }
    } else {
        assert(nk_window_destroy(window) == NK_OK);
    }
    assert(nk_window_destroy(window) == NK_ERROR_INVALID_HANDLE);
    nk_shutdown();
    return 0;
}
