#include "nativekit.h"
#include "nativekit_dialog.h"
#include "nativekit_system.h"
#include "nativekit_window.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

static nk_event wait_for_dialog(nk_request_id request) {
    for (int attempt = 0; attempt < 500; ++attempt) {
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
    assert((nk_get_capabilities() & NK_CAP_SHELL) != 0);
    assert((nk_get_capabilities() & NK_CAP_SYSTEM_APPEARANCE) != 0);

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

    assert(nk_window_destroy(window) == NK_OK);
    assert(nk_window_destroy(window) == NK_ERROR_INVALID_HANDLE);
    nk_shutdown();
    return 0;
}
