#include "nativekit.h"
#include "nativekit_clipboard.h"
#include "nativekit_dialog.h"
#include "nativekit_graphics.h"
#include "nativekit_input.h"
#include "nativekit_notification.h"
#include "nativekit_system.h"
#include "nativekit_webview.h"
#include "nativekit_window.h"

#include <assert.h>
#include <string.h>
#include <unistd.h>

typedef void (*clear_color_proc)(float, float, float, float);
typedef void (*clear_proc)(unsigned int);
typedef void (*read_pixels_proc)(int, int, int, int, unsigned int, unsigned int, void *);

static nk_event wait_for_event(nk_event_kind kind, nk_handle source) {
    for (int attempt = 0; attempt < 500; ++attempt) {
        nk_event event = {0};
        event.struct_size = sizeof(event);
        assert(nk_poll_event(&event) == NK_OK);
        if (event.kind == kind && event.source == source)
            return event;
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
    const nk_capabilities expected = NK_CAP_WINDOW | NK_CAP_WEBVIEW | NK_CAP_FILE_DIALOG |
                                     NK_CAP_CLIPBOARD | NK_CAP_DRAG_DROP | NK_CAP_SHELL |
                                     NK_CAP_SYSTEM_APPEARANCE | NK_CAP_NOTIFICATION |
                                     NK_CAP_INPUT | NK_CAP_OPENGL_SURFACE | NK_CAP_CURSOR |
                                     NK_CAP_POINTER_CAPTURE;
    assert((nk_get_capabilities() & expected) == expected);
    assert(nk_window_create(NULL, NULL) == NK_ERROR_INVALID_ARGUMENT);
    assert(nk_webview_navigate(NK_INVALID_HANDLE, NULL) == NK_ERROR_INVALID_ARGUMENT);
    assert(nk_notification_show(NULL, NULL) == NK_ERROR_INVALID_ARGUMENT);
    assert(nk_notification_close(NK_INVALID_REQUEST_ID) == NK_ERROR_INVALID_REQUEST);

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
    nk_window_state state = {0};
    state.struct_size = sizeof(state);
    assert(nk_window_get_state(window, &state) == NK_OK);
    nk_input_action input_action = NK_INPUT_PRESS;
    assert(nk_key_get_state(window, NK_KEY_A, &input_action) == NK_OK);
    assert(input_action == NK_INPUT_RELEASE);
    assert(nk_pointer_button_get_state(window, NK_POINTER_BUTTON_LEFT, &input_action) == NK_OK);
    assert(input_action == NK_INPUT_RELEASE);
    double pointer_x = -1.0;
    double pointer_y = -1.0;
    assert(nk_pointer_get_position(window, &pointer_x, &pointer_y) == NK_OK);
    assert(pointer_x == 0.0 && pointer_y == 0.0);
    nk_handle cursor = NK_INVALID_HANDLE;
    assert(nk_cursor_create_standard(NK_CURSOR_HAND, &cursor) == NK_OK);
    assert(nk_window_set_cursor(window, cursor) == NK_OK);
    assert(nk_cursor_destroy(cursor) == NK_OK);
    nk_cursor_mode cursor_mode = NK_CURSOR_MODE_DISABLED;
    assert(nk_window_get_cursor_mode(window, &cursor_mode) == NK_OK);
    assert(cursor_mode == NK_CURSOR_MODE_NORMAL);
    assert(nk_window_set_cursor_mode(window, NK_CURSOR_MODE_HIDDEN) == NK_OK);
    assert(nk_window_get_cursor_mode(window, &cursor_mode) == NK_OK);
    assert(cursor_mode == NK_CURSOR_MODE_HIDDEN);
    assert(nk_window_set_cursor_mode(window, NK_CURSOR_MODE_DISABLED) ==
           NK_ERROR_UNSUPPORTED);
    assert(nk_window_set_cursor_mode(window, NK_CURSOR_MODE_NORMAL) == NK_OK);
    assert(nk_window_show(window, 1) == NK_OK);
    const nk_result capture_result =
        nk_window_set_cursor_mode(window, NK_CURSOR_MODE_CAPTURED);
    if (capture_result == NK_OK) {
        assert(nk_window_get_cursor_mode(window, &cursor_mode) == NK_OK);
        assert(cursor_mode == NK_CURSOR_MODE_CAPTURED);
        assert(nk_window_set_cursor_mode(window, NK_CURSOR_MODE_NORMAL) == NK_OK);
    } else {
        assert(capture_result == NK_ERROR_UNSUPPORTED);
    }
    assert(nk_window_show(window, 0) == NK_OK);
    const unsigned char cursor_pixels[16] = {
        255, 255, 255, 255, 0, 0, 0, 255,
        0,   0,   0,   255, 255, 255, 255, 255};
    nk_cursor_image cursor_image = {0};
    cursor_image.struct_size = sizeof(cursor_image);
    cursor_image.width = 2;
    cursor_image.height = 2;
    cursor_image.stride = 8;
    cursor_image.rgba = cursor_pixels;
    assert(nk_cursor_create_custom(&cursor_image, &cursor) == NK_OK);
    assert(nk_window_set_cursor(window, cursor) == NK_OK);
    assert(nk_cursor_destroy(cursor) == NK_OK);
    assert(nk_raw_pointer_motion_supported() == 0);
    nk_surface_options surface_options = {0};
    surface_options.struct_size = sizeof(surface_options);
    surface_options.flags = NK_SURFACE_HIDDEN | NK_SURFACE_DEPTH;
    surface_options.api = NK_GRAPHICS_OPENGL;
    surface_options.width = 320;
    surface_options.height = 240;
    nk_handle surface = NK_INVALID_HANDLE;
    const nk_result surface_result = nk_surface_create(window, &surface_options, &surface);
    if (surface_result == NK_OK) {
        assert(surface != NK_INVALID_HANDLE);
        assert(nk_surface_make_current(surface) == NK_OK);
        int32_t framebuffer_width = 0;
        int32_t framebuffer_height = 0;
        assert(nk_surface_get_framebuffer_size(surface, &framebuffer_width,
                                               &framebuffer_height) == NK_OK);
        assert(framebuffer_width > 0 && framebuffer_height > 0);
        nk_graphics_proc generic_proc = NULL;
        clear_color_proc clear_color = NULL;
        clear_proc clear = NULL;
        read_pixels_proc read_pixels = NULL;
        assert(nk_surface_get_proc_address(surface, "glClearColor", &generic_proc) == NK_OK);
        memcpy(&clear_color, &generic_proc, sizeof(clear_color));
        assert(nk_surface_get_proc_address(surface, "glClear", &generic_proc) == NK_OK);
        memcpy(&clear, &generic_proc, sizeof(clear));
        assert(nk_surface_get_proc_address(surface, "glReadPixels", &generic_proc) == NK_OK);
        memcpy(&read_pixels, &generic_proc, sizeof(read_pixels));
        clear_color(1.0f, 0.0f, 0.0f, 1.0f);
        clear(0x00004000u); /* GL_COLOR_BUFFER_BIT */
        unsigned char pixel[4] = {0, 0, 0, 0};
        read_pixels(0, 0, 1, 1, 0x1908u, 0x1401u, pixel); /* GL_RGBA, GL_UNSIGNED_BYTE */
        assert(pixel[0] > 200 && pixel[1] < 20 && pixel[2] < 20);

        nk_surface_options shared_options = surface_options;
        shared_options.share_surface = surface;
        nk_handle shared_surface = NK_INVALID_HANDLE;
        assert(nk_surface_create(window, &shared_options, &shared_surface) == NK_OK);
        assert(nk_surface_destroy(surface) == NK_ERROR_INVALID_REQUEST);
        assert(nk_surface_destroy(shared_surface) == NK_OK);
        assert(nk_surface_set_bounds(surface, 0, 0, 300, 200) == NK_OK);
        assert(nk_surface_present(surface) == NK_OK);
        assert(nk_surface_destroy(surface) == NK_OK);
        assert(nk_surface_destroy(surface) == NK_ERROR_INVALID_HANDLE);
    } else {
        assert(surface_result == NK_ERROR_UNSUPPORTED);
    }
    nk_window_size_limits limits = {0};
    limits.struct_size = sizeof(limits);
    limits.min_width = 320;
    limits.min_height = 240;
    assert(nk_window_set_size_limits(window, &limits) == NK_OK);
    assert(nk_window_request_attention(window) == NK_OK);
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
        if (!received_clipboard_text)
            usleep(10000);
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
        if (!received_clipboard_files)
            usleep(10000);
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
    assert(
        nk_webview_set_html(
            webview,
            "<title>NativeKit</title><script>"
            "window.webkit.messageHandlers.nativekit.postMessage({answer:[42,true,'hello',null]});"
            "</script>",
            NULL) == NK_OK);

    int received_message = 0;
    for (int attempt = 0; attempt < 500 && !received_message; ++attempt) {
        nk_event event = {0};
        event.struct_size = sizeof(event);
        assert(nk_poll_event(&event) == NK_OK);
        if (event.kind == NK_EVENT_WEBVIEW_MESSAGE) {
            assert(event.source == webview);
            const char expected[] = "{\"answer\":[42,true,\"hello\",null]}";
            assert(event.result == NK_OK);
            assert(event.data_size == strlen(expected));
            assert(memcmp(event.data, expected, event.data_size) == 0);
            received_message = 1;
        }
        nk_event_release(&event);
        if (!received_message)
            usleep(10000);
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
        if (!received_result)
            usleep(10000);
    }
    assert(received_result);
    nk_request_id invalid_json_request = NK_INVALID_REQUEST_ID;
    assert(nk_webview_eval(webview, "undefined", &invalid_json_request) == NK_OK);
    nk_event invalid_json = wait_for_event(NK_EVENT_WEBVIEW_EVAL_COMPLETE, webview);
    assert(invalid_json.request_id == invalid_json_request);
    assert(invalid_json.result != NK_OK);
    nk_event_release(&invalid_json);

    nk_webview_options policy_options = webview_options;
    policy_options.flags |= NK_WEBVIEW_NAVIGATION_POLICY;
    nk_handle policy_webview = NK_INVALID_HANDLE;
    assert(nk_webview_create(window, &policy_options, &policy_webview) == NK_OK);
    nk_event policy_ready = wait_for_event(NK_EVENT_WEBVIEW_READY, policy_webview);
    nk_event_release(&policy_ready);
    const char policy_url[] = "data:text/html,NativeKit-policy";
    assert(nk_webview_navigate(policy_webview, policy_url) == NK_OK);
    nk_event policy_request = wait_for_event(NK_EVENT_WEBVIEW_NAVIGATION_REQUEST, policy_webview);
    assert(policy_request.request_id != NK_INVALID_REQUEST_ID);
    assert(policy_request.data_size == strlen(policy_url));
    assert(memcmp(policy_request.data, policy_url, policy_request.data_size) == 0);
    assert(nk_webview_navigation_decide(policy_request.request_id, 1) == NK_OK);
    assert(nk_webview_navigation_decide(policy_request.request_id, 1) == NK_ERROR_INVALID_REQUEST);
    nk_event_release(&policy_request);
    nk_event policy_navigated = wait_for_event(NK_EVENT_WEBVIEW_NAVIGATED, policy_webview);
    nk_event_release(&policy_navigated);
    assert(nk_webview_navigate(policy_webview, "data:text/html,cancel") == NK_OK);
    nk_event cancelled_request =
        wait_for_event(NK_EVENT_WEBVIEW_NAVIGATION_REQUEST, policy_webview);
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
        if (event.kind == NK_EVENT_DIALOG_COMPLETE && event.request_id == dialog_request) {
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
        if (event.kind == NK_EVENT_DIALOG_COMPLETE && event.request_id == message_request) {
            assert(event.flags == NK_DIALOG_MESSAGE);
            assert(event.data_size == sizeof(nk_dialog_message_result));
            const nk_dialog_message_result *result = (const nk_dialog_message_result *)event.data;
            assert(result->button == NK_MESSAGE_RESULT_CANCEL);
            received_message_dialog = 1;
        }
        nk_event_release(&event);
    }
    assert(received_message_dialog);

    /* Destroying a parent invalidates all of its borrowed child handles. */
    nk_window_options owned_options = window_options;
    owned_options.flags = NK_WINDOW_HIDDEN | NK_WINDOW_BORDERLESS | NK_WINDOW_MODAL;
    owned_options.owner = window;
    owned_options.kind = NK_WINDOW_UTILITY;
    nk_handle owned_window = NK_INVALID_HANDLE;
    assert(nk_window_create(&owned_options, &owned_window) == NK_OK);
    nk_request_id destroyed_eval_request = NK_INVALID_REQUEST_ID;
    assert(nk_webview_eval(webview, "42", &destroyed_eval_request) == NK_OK);
    assert(nk_window_destroy(window) == NK_OK);
    nk_event destroyed_eval = wait_for_event(NK_EVENT_WEBVIEW_EVAL_COMPLETE, webview);
    assert(destroyed_eval.request_id == destroyed_eval_request);
    assert(destroyed_eval.result == NK_ERROR_INVALID_REQUEST);
    nk_event_release(&destroyed_eval);
    assert(nk_webview_destroy(webview) == NK_ERROR_INVALID_HANDLE);
    assert(nk_window_destroy(owned_window) == NK_ERROR_INVALID_HANDLE);
    assert(nk_window_destroy(window) == NK_ERROR_INVALID_HANDLE);

    nk_request_id stale_clipboard_request = NK_INVALID_REQUEST_ID;
    assert(nk_clipboard_read_text(&stale_clipboard_request) == NK_OK);
    nk_shutdown();
    assert(nk_init(&init) == NK_OK);
    for (int attempt = 0; attempt < 20; ++attempt) {
        nk_event stale = {0};
        stale.struct_size = sizeof(stale);
        assert(nk_poll_event(&stale) == NK_OK);
        assert(stale.kind == NK_EVENT_NONE);
        nk_event_release(&stale);
        usleep(10000);
    }
    nk_shutdown();
    return 0;
}
