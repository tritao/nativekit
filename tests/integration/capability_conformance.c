#include "nativekit.h"
#include "nativekit_accessibility.h"
#include "nativekit_clipboard.h"
#include "nativekit_dialog.h"
#include "nativekit_gamepad.h"
#include "nativekit_graphics.h"
#include "nativekit_input.h"
#include "nativekit_joystick.h"
#include "nativekit_monitor.h"
#include "nativekit_notification.h"
#include "nativekit_resource.h"
#include "nativekit_system.h"
#include "nativekit_time.h"
#include "nativekit_window.h"
#include "nativekit_webview.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define NK_TEST_CLIPBOARD_PATH "C:\\NativeKit capability conformance.txt"
#else
#define NK_TEST_CLIPBOARD_PATH "/tmp/nativekit-capability-conformance.txt"
#endif

static int require_ok(const char *operation, nk_result result) {
    if (result == NK_OK)
        return 1;
    fprintf(stderr, "%s returned %d: %s\n", operation, result, nk_last_error());
    return 0;
}

static int require_not_unsupported(const char *operation, nk_result result) {
    if (result == NK_ERROR_UNSUPPORTED) {
        fprintf(stderr, "%s is unsupported despite its advertised capability\n", operation);
        return 0;
    }
    return 1;
}

static int require_size_query(const char *operation, nk_result result) {
    if (result == NK_OK || result == NK_ERROR_BUFFER_TOO_SMALL)
        return 1;
    fprintf(stderr, "%s returned %d: %s\n", operation, result, nk_last_error());
    return 0;
}

static int require_result(const char *operation, nk_result result, nk_result expected) {
    if (result == expected)
        return 1;
    fprintf(stderr, "%s returned %d, expected %d: %s\n", operation, result, expected,
            nk_last_error());
    return 0;
}

static int wait_for_request(nk_request_id request, nk_event *out_event) {
    for (int attempt = 0; attempt < 500; ++attempt) {
        nk_event event = {0};
        event.struct_size = sizeof(event);
        if (!require_ok("nk_poll_event", nk_poll_event(&event)))
            return 0;
        if (event.kind != NK_EVENT_NONE && event.request_id == request) {
            *out_event = event;
            return 1;
        }
        nk_event_release(&event);
        if (!require_ok("nk_wait_events_timeout", nk_wait_events_timeout(0.01)))
            return 0;
    }
    fprintf(stderr, "timed out waiting for request %llu\n", (unsigned long long)request);
    return 0;
}

static int probe_window(nk_capabilities capabilities, nk_window *out_window,
                        nk_native_window *out_native) {
    if (!(capabilities & NK_CAP_WINDOW))
        return 1;
    nk_window_options options = {0};
    options.struct_size = sizeof(options);
    options.flags = NK_WINDOW_HIDDEN | NK_WINDOW_RESIZABLE;
    options.width = 640;
    options.height = 480;
    options.title = "NativeKit capability conformance";
    if (!require_ok("nk_window_create", nk_window_create(&options, out_window)))
        return 0;
    if (!require_ok("nk_window_set_title", nk_window_set_title(*out_window, "NativeKit parity")) ||
        !require_ok("nk_window_show(false)", nk_window_show(*out_window, 0)) ||
        !require_ok("nk_window_show(true)", nk_window_show(*out_window, 1)) ||
        !require_ok("nk_window_set_bounds", nk_window_set_bounds(*out_window, 20, 20, 800, 600)))
        return 0;

    if (capabilities & NK_CAP_WINDOW_GEOMETRY) {
        float scale = 0.0f;
        nk_window_content_scale content_scale = {0};
        content_scale.struct_size = sizeof(content_scale);
        nk_window_frame_extents extents = {0};
        extents.struct_size = sizeof(extents);
        int32_t x = 0, y = 0, width = 0, height = 0, framebuffer_width = 0, framebuffer_height = 0;
        if (!require_ok("nk_window_get_scale", nk_window_get_scale(*out_window, &scale)) ||
            !require_ok("nk_window_get_content_scale",
                        nk_window_get_content_scale(*out_window, &content_scale)) ||
            !require_ok("nk_window_get_position", nk_window_get_position(*out_window, &x, &y)) ||
            !require_ok("nk_window_get_size", nk_window_get_size(*out_window, &width, &height)) ||
            !require_ok("nk_window_get_framebuffer_size",
                        nk_window_get_framebuffer_size(*out_window, &framebuffer_width,
                                                       &framebuffer_height)) ||
            !require_ok("nk_window_get_frame_extents",
                        nk_window_get_frame_extents(*out_window, &extents)))
            return 0;
        if (!(scale > 0.0f && content_scale.x > 0.0f && content_scale.y > 0.0f && width > 0 &&
              height > 0 && framebuffer_width > 0 && framebuffer_height > 0)) {
            fprintf(stderr, "window geometry returned non-positive dimensions\n");
            return 0;
        }
    }

    if (capabilities & NK_CAP_WINDOW_STYLING) {
        nk_window_size_limits limits = {0};
        limits.struct_size = sizeof(limits);
        limits.min_width = 320;
        limits.min_height = 240;
        if (!require_ok("nk_window_set_size_limits",
                        nk_window_set_size_limits(*out_window, &limits)) ||
            !require_ok("nk_window_set_aspect_ratio",
                        nk_window_set_aspect_ratio(*out_window, 16, 9)) ||
            !require_ok("nk_window_clear_aspect_ratio",
                        nk_window_set_aspect_ratio(*out_window, 0, 0)) ||
            !require_ok("nk_window_set_resizable", nk_window_set_resizable(*out_window, 0)) ||
            !require_ok("nk_window_restore_resizable", nk_window_set_resizable(*out_window, 1)) ||
            !require_ok("nk_window_set_decorated", nk_window_set_decorated(*out_window, 0)) ||
            !require_ok("nk_window_restore_decorated", nk_window_set_decorated(*out_window, 1)) ||
            !require_ok("nk_window_set_floating", nk_window_set_floating(*out_window, 1)) ||
            !require_ok("nk_window_clear_floating", nk_window_set_floating(*out_window, 0)) ||
            !require_ok("nk_window_set_opacity", nk_window_set_opacity(*out_window, 0.75f)) ||
            !require_ok("nk_window_restore_opacity", nk_window_set_opacity(*out_window, 1.0f)) ||
            !require_ok("nk_window_set_mouse_passthrough",
                        nk_window_set_mouse_passthrough(*out_window, 1)) ||
            !require_ok("nk_window_clear_mouse_passthrough",
                        nk_window_set_mouse_passthrough(*out_window, 0)))
            return 0;
    }

    nk_window_state state = {0};
    state.struct_size = sizeof(state);
    nk_bool focused = 0, visible = 0, hovered = 0;
    if (!require_ok("nk_window_get_state", nk_window_get_state(*out_window, &state)) ||
        !require_ok("nk_window_is_focused", nk_window_is_focused(*out_window, &focused)) ||
        !require_ok("nk_window_is_visible", nk_window_is_visible(*out_window, &visible)) ||
        !require_ok("nk_window_get_hovered", nk_window_get_hovered(*out_window, &hovered)) ||
        !require_ok("nk_window_minimize", nk_window_minimize(*out_window)) ||
        !require_ok("nk_window_maximize", nk_window_maximize(*out_window)) ||
        !require_ok("nk_window_restore", nk_window_restore(*out_window)) ||
        !require_ok("nk_window_activate", nk_window_activate(*out_window)) ||
        !require_ok("nk_window_set_fullscreen", nk_window_set_fullscreen(*out_window, 1)) ||
        !require_ok("nk_window_clear_fullscreen", nk_window_set_fullscreen(*out_window, 0)) ||
        !require_ok("nk_window_request_attention", nk_window_request_attention(*out_window)))
        return 0;

    if (capabilities & NK_CAP_EXPORT_NATIVE_WINDOW) {
        memset(out_native, 0, sizeof(*out_native));
        out_native->struct_size = sizeof(*out_native);
        if (!require_ok("nk_window_get_native", nk_window_get_native(*out_window, out_native)) ||
            out_native->kind == NK_NATIVE_WINDOW_UNKNOWN || !out_native->window) {
            fprintf(stderr, "native window export returned an empty descriptor\n");
            return 0;
        }
    }
    if (capabilities & NK_CAP_WRAP_NATIVE_WINDOW) {
        nk_window wrapped = NK_INVALID_HANDLE;
        if (!require_ok("nk_window_wrap_native", nk_window_wrap_native(out_native, &wrapped)) ||
            wrapped == NK_INVALID_HANDLE ||
            !require_ok("nk_window_destroy(wrapped)", nk_window_destroy(wrapped)))
            return 0;
        memset(out_native, 0, sizeof(*out_native));
        out_native->struct_size = sizeof(*out_native);
        if (!require_ok("nk_window_get_native(after wrap)",
                        nk_window_get_native(*out_window, out_native)) ||
            !out_native->window) {
            fprintf(stderr, "native window did not survive wrapper destruction\n");
            return 0;
        }
    }
    return 1;
}

static int probe_input_and_cursor(nk_capabilities capabilities, nk_window window) {
    if (capabilities & NK_CAP_INPUT) {
        nk_input_action action = NK_INPUT_RELEASE;
        double x = 0.0, y = 0.0;
        nk_text_input_state text = {0};
        text.struct_size = sizeof(text);
        text.text = "hello";
        text.document_length = 5;
        text.selection_start = 5;
        text.selection_end = 5;
        text.composition_start = NK_TEXT_POSITION_NONE;
        text.composition_end = NK_TEXT_POSITION_NONE;
        text.cursor_width = 1.0f;
        text.cursor_height = 16.0f;
        nk_text_input_state empty_text = {0};
        empty_text.struct_size = sizeof(empty_text);
        empty_text.text = "";
        empty_text.composition_start = NK_TEXT_POSITION_NONE;
        empty_text.composition_end = NK_TEXT_POSITION_NONE;
        if (!require_ok("nk_key_get_state", nk_key_get_state(window, NK_KEY_A, &action)) ||
            !require_ok("nk_pointer_button_get_state",
                        nk_pointer_button_get_state(window, NK_POINTER_BUTTON_LEFT, &action)) ||
            !require_ok("nk_pointer_get_position", nk_pointer_get_position(window, &x, &y)) ||
            !require_ok("nk_surface_set_text_input_state",
                        nk_surface_set_text_input_state(window, &text)) ||
            !require_ok("nk_surface_set_text_input_active(true)",
                        nk_surface_set_text_input_active(window, 1)) ||
            !require_ok("nk_surface_set_text_input_active(false)",
                        nk_surface_set_text_input_active(window, 0)) ||
            !require_ok("nk_surface_set_text_input_state(empty)",
                        nk_surface_set_text_input_state(window, &empty_text)))
            return 0;
    }
    if (capabilities & NK_CAP_CURSOR) {
        nk_cursor cursor = NK_INVALID_HANDLE;
        unsigned char pixel[4] = {255, 255, 255, 255};
        nk_cursor_image image = {0};
        image.struct_size = sizeof(image);
        image.width = 1;
        image.height = 1;
        image.stride = 4;
        image.rgba = pixel;
        if (!require_ok("nk_cursor_create_standard",
                        nk_cursor_create_standard(NK_CURSOR_ARROW, &cursor)) ||
            !require_ok("nk_window_set_cursor", nk_window_set_cursor(window, cursor)) ||
            !require_ok("nk_cursor_destroy(standard)", nk_cursor_destroy(cursor)))
            return 0;
        cursor = NK_INVALID_HANDLE;
        if (!require_ok("nk_cursor_create_custom", nk_cursor_create_custom(&image, &cursor)) ||
            !require_ok("nk_window_set_custom_cursor", nk_window_set_cursor(window, cursor)))
            return 0;
        if (!require_ok("nk_cursor_destroy", nk_cursor_destroy(cursor)) ||
            !require_ok("nk_window_clear_cursor", nk_window_set_cursor(window, NK_INVALID_HANDLE)))
            return 0;
        nk_cursor_mode mode = NK_CURSOR_MODE_NORMAL;
        if (!require_ok("nk_window_get_cursor_mode", nk_window_get_cursor_mode(window, &mode)) ||
            !require_ok("nk_window_set_cursor_mode(normal)",
                        nk_window_set_cursor_mode(window, NK_CURSOR_MODE_NORMAL)))
            return 0;
        if (capabilities & NK_CAP_POINTER_CAPTURE) {
            if (!require_ok("nk_window_set_cursor_mode(captured)",
                            nk_window_set_cursor_mode(window, NK_CURSOR_MODE_CAPTURED)) ||
                !require_ok("nk_window_set_cursor_mode(released)",
                            nk_window_set_cursor_mode(window, NK_CURSOR_MODE_NORMAL)))
                return 0;
        }
    }
    return 1;
}

static int probe_clipboard_and_resources(nk_capabilities capabilities, const char *resource_uri) {
    if (capabilities & NK_CAP_CLIPBOARD) {
        const char text[] = "NativeKit capability clipboard";
        const char *paths[] = {NK_TEST_CLIPBOARD_PATH};
        nk_request_id request = NK_INVALID_REQUEST_ID;
        nk_event event = {0};
        if (!require_ok("nk_clipboard_set_text", nk_clipboard_set_text(text)) ||
            !require_ok("nk_clipboard_read_text", nk_clipboard_read_text(&request)) ||
            !wait_for_request(request, &event) || event.result != NK_OK ||
            event.data_size != sizeof(text) - 1 ||
            memcmp(event.data, text, sizeof(text) - 1) != 0) {
            fprintf(stderr, "clipboard text round trip failed\n");
            nk_event_release(&event);
            return 0;
        }
        nk_event_release(&event);
        if (!require_ok("nk_clipboard_set_files", nk_clipboard_set_files(paths, 1)) ||
            !require_ok("nk_clipboard_read_files", nk_clipboard_read_files(&request)) ||
            !wait_for_request(request, &event) || event.result != NK_OK || event.data_count != 1) {
            fprintf(stderr, "clipboard file round trip failed\n");
            nk_event_release(&event);
            return 0;
        }
        nk_event_release(&event);
    }
    if (capabilities & NK_CAP_RESOURCE_SHARING) {
        nk_resource resource = {0};
        resource.struct_size = sizeof(resource);
        resource.flags = NK_RESOURCE_READABLE;
        resource.uri = resource_uri;
        nk_request_id request = NK_INVALID_REQUEST_ID;
        nk_event event = {0};
        if (!require_ok("nk_clipboard_set_resources", nk_clipboard_set_resources(&resource, 1)) ||
            !require_ok("nk_clipboard_read_resources", nk_clipboard_read_resources(&request)) ||
            !wait_for_request(request, &event) || event.result != NK_OK || event.data_count != 1) {
            fprintf(stderr, "resource clipboard round trip failed\n");
            nk_event_release(&event);
            return 0;
        }
        nk_resource_view view = {0};
        view.struct_size = sizeof(view);
        if (!require_ok("nk_resource_event_item", nk_resource_event_item(&event, 0, &view)) ||
            !view.uri || strcmp(view.uri, resource_uri) != 0) {
            fprintf(stderr, "resource clipboard item was not preserved\n");
            nk_event_release(&event);
            return 0;
        }
        nk_event_release(&event);
    }
    return 1;
}

static int probe_resource_io(const char *path, const char *uri) {
    nk_resource resource = {0};
    resource.struct_size = sizeof(resource);
    resource.flags = NK_RESOURCE_READABLE | NK_RESOURCE_WRITABLE;
    resource.uri = uri;
    nk_resource_stream stream = NK_INVALID_HANDLE;
    const char bytes[] = "NativeKit capability resource";
    char result[sizeof(bytes)] = {0};
    uint64_t transferred = 0;
    uint64_t position = UINT64_MAX;
    nk_resource_stream_info info = {0};
    info.struct_size = sizeof(info);
    if (!require_ok("nk_resource_open",
                    nk_resource_open(&resource,
                                     NK_RESOURCE_OPEN_READ | NK_RESOURCE_OPEN_WRITE |
                                         NK_RESOURCE_OPEN_CREATE | NK_RESOURCE_OPEN_TRUNCATE,
                                     &stream)) ||
        !require_ok("nk_resource_write",
                    nk_resource_write(stream, bytes, sizeof(bytes) - 1, &transferred)) ||
        transferred != sizeof(bytes) - 1 ||
        !require_ok("nk_resource_seek", nk_resource_seek(stream, 0, NK_SEEK_START, &position)) ||
        position != 0 ||
        !require_ok("nk_resource_read",
                    nk_resource_read(stream, result, sizeof(bytes) - 1, &transferred)) ||
        transferred != sizeof(bytes) - 1 || memcmp(result, bytes, sizeof(bytes) - 1) != 0 ||
        !require_ok("nk_resource_stream_info_get", nk_resource_stream_info_get(stream, &info)) ||
        !(info.flags & NK_RESOURCE_STREAM_SEEKABLE) ||
        !require_ok("nk_resource_close", nk_resource_close(stream))) {
        remove(path);
        return 0;
    }
    if (!require_result("nk_resource_close(stale)", nk_resource_close(stream),
                        NK_ERROR_INVALID_HANDLE))
        return 0;
    remove(path);
    return 1;
}

static int probe_monitors(nk_capabilities capabilities, nk_window window) {
    if (!(capabilities & NK_CAP_MONITOR))
        return 1;
    uint32_t count = 0;
    nk_result result = nk_monitor_list(NULL, &count);
    if (result != NK_ERROR_BUFFER_TOO_SMALL && result != NK_OK) {
        fprintf(stderr, "nk_monitor_list(count) returned %d: %s\n", result, nk_last_error());
        return 0;
    }
    if (!count)
        return 0;
    nk_monitor *monitors = calloc(count, sizeof(*monitors));
    if (!monitors || !require_ok("nk_monitor_list", nk_monitor_list(monitors, &count))) {
        free(monitors);
        return 0;
    }
    nk_monitor primary = NK_INVALID_HANDLE;
    nk_monitor_geometry geometry = {0};
    nk_video_mode mode = {0};
    geometry.struct_size = sizeof(geometry);
    mode.struct_size = sizeof(mode);
    if (!require_ok("nk_monitor_get_primary", nk_monitor_get_primary(&primary)) ||
        !require_ok("nk_monitor_get_geometry", nk_monitor_get_geometry(primary, &geometry)) ||
        !require_ok("nk_monitor_get_current_mode", nk_monitor_get_current_mode(primary, &mode))) {
        free(monitors);
        return 0;
    }
    uint32_t name_size = 0;
    if (!require_size_query("nk_monitor_get_name(size)",
                            nk_monitor_get_name(primary, NULL, &name_size)) ||
        !name_size) {
        free(monitors);
        return 0;
    }
    char *name = malloc(name_size);
    if (!name ||
        !require_ok("nk_monitor_get_name", nk_monitor_get_name(primary, name, &name_size))) {
        free(name);
        free(monitors);
        return 0;
    }
    free(name);
    uint32_t mode_count = 0;
    if (!require_size_query("nk_monitor_get_modes(size)",
                            nk_monitor_get_modes(primary, NULL, &mode_count)) ||
        !mode_count) {
        free(monitors);
        return 0;
    }
    nk_video_mode *modes = calloc(mode_count, sizeof(*modes));
    if (!modes) {
        free(modes);
        free(monitors);
        return 0;
    }
    for (uint32_t index = 0; index < mode_count; ++index)
        modes[index].struct_size = sizeof(*modes);
    if (!require_ok("nk_monitor_get_modes", nk_monitor_get_modes(primary, modes, &mode_count))) {
        free(modes);
        free(monitors);
        return 0;
    }
    free(modes);
    if ((capabilities & NK_CAP_MONITOR_FULLSCREEN) &&
        (!require_ok("nk_window_set_fullscreen_monitor",
                     nk_window_set_fullscreen_monitor(window, primary)) ||
         !require_ok("nk_window_clear_fullscreen_monitor",
                     nk_window_set_fullscreen_monitor(window, NK_INVALID_HANDLE)))) {
        free(monitors);
        return 0;
    }
    free(monitors);
    return geometry.width > 0 && geometry.height > 0 && mode.width > 0 && mode.height > 0;
}

static int probe_system_and_validation(nk_capabilities capabilities) {
    if (capabilities & NK_CAP_SYSTEM_APPEARANCE) {
        nk_system_appearance appearance = {0};
        appearance.struct_size = sizeof(appearance);
        if (!require_ok("nk_system_get_appearance", nk_system_get_appearance(&appearance)))
            return 0;
    }
    uint32_t size = 0;
    if (!require_size_query("nk_system_locale(size)", nk_system_locale(NULL, &size)) || !size)
        return 0;
    char *locale = malloc(size);
    if (!locale || !require_ok("nk_system_locale", nk_system_locale(locale, &size))) {
        free(locale);
        return 0;
    }
    free(locale);
    if (capabilities & NK_CAP_SHELL) {
        const nk_result result = nk_shell_open_url("not a URI");
        if (!require_not_unsupported("nk_shell_open_url(validation)", result) ||
            !require_result("nk_shell_open_url(validation)", result, NK_ERROR_INVALID_ARGUMENT))
            return 0;
    }
    if (capabilities & NK_CAP_NOTIFICATION) {
        const nk_result result = nk_notification_show(NULL, NULL);
        if (!require_not_unsupported("nk_notification_show(validation)", result) ||
            !require_result("nk_notification_show(validation)", result, NK_ERROR_INVALID_ARGUMENT))
            return 0;
    }
    if (capabilities & NK_CAP_FILE_DIALOG) {
        nk_file_dialog_options file_options = {0};
        file_options.struct_size = sizeof(file_options);
        const nk_result open_result = nk_dialog_open_file(NK_INVALID_HANDLE, &file_options, NULL);
        const nk_result save_result = nk_dialog_save_file(NK_INVALID_HANDLE, &file_options, NULL);
        const nk_result directory_result =
            nk_dialog_select_directory(NK_INVALID_HANDLE, &file_options, NULL);
        if (!require_not_unsupported("nk_dialog_open_file(validation)", open_result) ||
            !require_result("nk_dialog_open_file(validation)", open_result,
                            NK_ERROR_INVALID_ARGUMENT) ||
            !require_not_unsupported("nk_dialog_save_file(validation)", save_result) ||
            !require_result("nk_dialog_save_file(validation)", save_result,
                            NK_ERROR_INVALID_ARGUMENT) ||
            !require_not_unsupported("nk_dialog_select_directory(validation)", directory_result) ||
            !require_result("nk_dialog_select_directory(validation)", directory_result,
                            NK_ERROR_INVALID_ARGUMENT))
            return 0;
    }
    return 1;
}

static int probe_joysticks(nk_capabilities capabilities) {
    if (!(capabilities & NK_CAP_JOYSTICK))
        return 1;
    uint32_t count = 0;
    nk_result result = nk_joystick_list(NULL, &count);
    if (result != NK_OK && result != NK_ERROR_BUFFER_TOO_SMALL) {
        fprintf(stderr, "nk_joystick_list(count) returned %d: %s\n", result, nk_last_error());
        return 0;
    }
    nk_joystick *joysticks = count ? calloc(count, sizeof(*joysticks)) : NULL;
    if (count &&
        (!joysticks || !require_ok("nk_joystick_list", nk_joystick_list(joysticks, &count)))) {
        free(joysticks);
        return 0;
    }
    for (uint32_t index = 0; index < count; ++index) {
        uint32_t size = 0;
        if (!require_size_query("nk_joystick_get_name(size)",
                                nk_joystick_get_name(joysticks[index], NULL, &size)))
            return 0;
        char *name = malloc(size);
        if (!name || !require_ok("nk_joystick_get_name",
                                 nk_joystick_get_name(joysticks[index], name, &size))) {
            free(name);
            free(joysticks);
            return 0;
        }
        free(name);
        size = 0;
        if (!require_size_query("nk_joystick_get_guid(size)",
                                nk_joystick_get_guid(joysticks[index], NULL, &size)))
            return 0;
        char *guid = malloc(size);
        if (!guid || !require_ok("nk_joystick_get_guid",
                                 nk_joystick_get_guid(joysticks[index], guid, &size))) {
            free(guid);
            free(joysticks);
            return 0;
        }
        free(guid);
        uint32_t element_count = 0;
        result = nk_joystick_get_axes(joysticks[index], NULL, &element_count);
        if (!require_size_query("nk_joystick_get_axes(size)", result))
            return 0;
        float *axes = element_count ? calloc(element_count, sizeof(*axes)) : NULL;
        if (element_count &&
            (!axes || !require_ok("nk_joystick_get_axes",
                                  nk_joystick_get_axes(joysticks[index], axes, &element_count)))) {
            free(axes);
            free(joysticks);
            return 0;
        }
        free(axes);
        element_count = 0;
        result = nk_joystick_get_buttons(joysticks[index], NULL, &element_count);
        if (!require_size_query("nk_joystick_get_buttons(size)", result))
            return 0;
        uint8_t *buttons = element_count ? calloc(element_count, sizeof(*buttons)) : NULL;
        if (element_count &&
            (!buttons ||
             !require_ok("nk_joystick_get_buttons",
                         nk_joystick_get_buttons(joysticks[index], buttons, &element_count)))) {
            free(buttons);
            free(joysticks);
            return 0;
        }
        free(buttons);
        element_count = 0;
        result = nk_joystick_get_hats(joysticks[index], NULL, &element_count);
        if (!require_size_query("nk_joystick_get_hats(size)", result))
            return 0;
        uint8_t *hats = element_count ? calloc(element_count, sizeof(*hats)) : NULL;
        if (element_count &&
            (!hats || !require_ok("nk_joystick_get_hats",
                                  nk_joystick_get_hats(joysticks[index], hats, &element_count)))) {
            free(hats);
            free(joysticks);
            return 0;
        }
        free(hats);
    }
    free(joysticks);
    uint32_t diagnostics_size = 0;
    if (!require_size_query("nk_joystick_get_diagnostics(size)",
                            nk_joystick_get_diagnostics(NULL, &diagnostics_size)))
        return 0;
    char *diagnostics = diagnostics_size ? malloc(diagnostics_size) : NULL;
    if (diagnostics_size && (!diagnostics || !require_ok("nk_joystick_get_diagnostics",
                                                         nk_joystick_get_diagnostics(
                                                             diagnostics, &diagnostics_size)))) {
        free(diagnostics);
        return 0;
    }
    free(diagnostics);
    return 1;
}

static nk_graphics_api selected_graphics_api(nk_capabilities capabilities) {
    if (capabilities & NK_CAP_D3D11_SURFACE)
        return NK_GRAPHICS_D3D11;
    if (capabilities & NK_CAP_METAL_SURFACE)
        return NK_GRAPHICS_METAL;
    if (capabilities & NK_CAP_OPENGL_SURFACE)
        return NK_GRAPHICS_OPENGL;
    if (capabilities & NK_CAP_OPENGL_ES_SURFACE)
        return NK_GRAPHICS_OPENGL_ES;
    return 0;
}

static int acquire_surface(nk_surface surface) {
    for (int attempt = 0; attempt < 100; ++attempt) {
        nk_result result = nk_surface_make_current(surface);
        if (result == NK_OK)
            return 1;
        if (result != NK_ERROR_INVALID_REQUEST ||
            !require_ok("nk_wait_events_timeout", nk_wait_events_timeout(0.01)))
            return require_not_unsupported("nk_surface_make_current", result);
    }
    fprintf(stderr, "timed out preparing graphics surface\n");
    return 0;
}

static int probe_graphics_and_accessibility(nk_capabilities capabilities, nk_window window,
                                            nk_surface *out_surface) {
    const nk_graphics_api api = selected_graphics_api(capabilities);
    if (!api)
        return 1;
    nk_surface_options options = {0};
    options.struct_size = sizeof(options);
    options.flags = NK_SURFACE_DEPTH | NK_SURFACE_STENCIL;
    options.api = api;
    options.width = 320;
    options.height = 240;
    if (!require_ok("nk_surface_create", nk_surface_create(window, &options, out_surface)))
        return 0;
    if (!acquire_surface(*out_surface))
        return 0;
    nk_surface_frame_target target = {0};
    target.struct_size = sizeof(target);
    int32_t width = 0, height = 0;
    if (!require_ok("nk_surface_get_framebuffer_size",
                    nk_surface_get_framebuffer_size(*out_surface, &width, &height)) ||
        !require_ok("nk_surface_get_frame_target",
                    nk_surface_get_frame_target(*out_surface, &target)) ||
        target.api != api ||
        !require_ok("nk_surface_set_bounds", nk_surface_set_bounds(*out_surface, 0, 0, 320, 240)) ||
        !require_ok("nk_surface_present", nk_surface_present(*out_surface)) ||
        !require_ok("nk_surface_set_frame_callback",
                    nk_surface_set_frame_callback(*out_surface, NULL, NULL)))
        return 0;
    if (api == NK_GRAPHICS_OPENGL || api == NK_GRAPHICS_OPENGL_ES) {
        nk_graphics_proc proc = NULL;
        if (!require_ok("nk_surface_get_proc_address",
                        nk_surface_get_proc_address(*out_surface, "glGetString", &proc)) ||
            !proc)
            return 0;
    }
    if (capabilities & NK_CAP_ACCESSIBILITY) {
        nk_accessibility_node node = {0};
        node.struct_size = sizeof(node);
        node.id = 1;
        node.parent_id = NK_ACCESSIBILITY_ROOT;
        node.role = NK_ACCESSIBILITY_BUTTON;
        node.states = NK_ACCESSIBILITY_FOCUSABLE;
        node.actions = NK_ACCESSIBILITY_CAN_ACTIVATE | NK_ACCESSIBILITY_CAN_FOCUS;
        node.width = 160;
        node.height = 32;
        node.label = "Conformance button";
        node.value = "Hello";
        node.document_length = 5;
        nk_accessibility_text_range range = {0, 5, 0, 0, 160, 32};
        nk_accessibility_update update = {0};
        update.struct_size = sizeof(update);
        update.nodes = &node;
        update.node_count = 1;
        uint8_t removed_id[4] = {1, 0, 0, 0};
        nk_accessibility_update remove_update = {0};
        remove_update.struct_size = sizeof(remove_update);
        if (!require_ok("nk_surface_accessibility_set_node",
                        nk_surface_accessibility_set_node(*out_surface, &node)) ||
            !require_ok("nk_surface_accessibility_set_focus",
                        nk_surface_accessibility_set_focus(*out_surface, node.id)) ||
            !require_ok(
                "nk_surface_accessibility_set_text_ranges",
                nk_surface_accessibility_set_text_ranges(*out_surface, node.id, &range, 1)) ||
            !require_ok("nk_surface_accessibility_update",
                        nk_surface_accessibility_update(*out_surface, &update)) ||
            !require_ok("nk_surface_accessibility_update_with_removed_ids",
                        nk_surface_accessibility_update_with_removed_ids(
                            *out_surface, &remove_update, removed_id, sizeof(removed_id))) ||
            !require_ok("nk_surface_accessibility_set_node(reinsert)",
                        nk_surface_accessibility_set_node(*out_surface, &node)) ||
            !require_ok("nk_surface_accessibility_remove_node",
                        nk_surface_accessibility_remove_node(*out_surface, node.id)) ||
            !require_ok("nk_surface_accessibility_clear",
                        nk_surface_accessibility_clear(*out_surface)))
            return 0;
    }
    return 1;
}

static int probe_webview(nk_capabilities capabilities, nk_window window) {
    if (!(capabilities & NK_CAP_WEBVIEW))
        return 1;
    nk_webview_options options = {0};
    options.struct_size = sizeof(options);
    options.flags = NK_WEBVIEW_HIDDEN;
    options.width = 320;
    options.height = 240;
    nk_webview webview = NK_INVALID_HANDLE;
    nk_request_id eval_request = NK_INVALID_REQUEST_ID;
    nk_bool can_go_back = 0;
    nk_bool can_go_forward = 0;
    if (!require_ok("nk_webview_create", nk_webview_create(window, &options, &webview)) ||
        !require_ok("nk_webview_show", nk_webview_show(webview, 1)) ||
        !require_ok("nk_webview_set_bounds", nk_webview_set_bounds(webview, 0, 0, 320, 240)) ||
        !require_ok("nk_webview_set_html",
                    nk_webview_set_html(webview, "<title>conformance</title>", NULL)) ||
        !require_ok("nk_webview_navigate", nk_webview_navigate(webview, "about:blank")) ||
        !require_ok("nk_webview_can_go_back", nk_webview_can_go_back(webview, &can_go_back)) ||
        !require_ok("nk_webview_can_go_forward",
                    nk_webview_can_go_forward(webview, &can_go_forward)) ||
        !require_ok("nk_webview_go_back", nk_webview_go_back(webview)) ||
        !require_ok("nk_webview_go_forward", nk_webview_go_forward(webview)) ||
        !require_ok("nk_webview_reload", nk_webview_reload(webview)) ||
        !require_ok("nk_webview_stop", nk_webview_stop(webview)) ||
        !require_ok("nk_webview_eval", nk_webview_eval(webview, "1 + 1", &eval_request)) ||
        !require_ok("nk_webview_destroy", nk_webview_destroy(webview)))
        return 0;
    return 1;
}

static int make_resource_uri(char *path, size_t path_size, char *uri, size_t uri_size) {
    uint32_t size = 0;
    if (!require_size_query("nk_system_directory(temp size)",
                            nk_system_directory(NK_DIRECTORY_TEMP, NULL, &size)) ||
        size >= path_size)
        return 0;
    if (!require_ok("nk_system_directory(temp)",
                    nk_system_directory(NK_DIRECTORY_TEMP, path, &size)))
        return 0;
    const size_t length = strlen(path);
    const char *separator =
        length && (path[length - 1] == '/' || path[length - 1] == '\\') ? "" : "/";
    if (snprintf(path + length, path_size - length, "%snativekit-capability-conformance.bin",
                 separator) >= (int)(path_size - length))
        return 0;
#if defined(_WIN32)
    for (char *value = path; *value; ++value)
        if (*value == '\\')
            *value = '/';
    if (snprintf(uri, uri_size, "file:///%s", path) >= (int)uri_size)
        return 0;
#else
    if (snprintf(uri, uri_size, "file://%s", path) >= (int)uri_size)
        return 0;
#endif
    return 1;
}

int main(void) {
    nk_init_options init = {0};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    if (!require_ok("nk_init", nk_init(&init)))
        return 1;
    const nk_capabilities capabilities = nk_get_capabilities();
    nk_window window = NK_INVALID_HANDLE;
    nk_surface surface = NK_INVALID_HANDLE;
    nk_native_window native = {0};
    char path[4096] = {0};
    char uri[8192] = {0};
    int success =
        make_resource_uri(path, sizeof(path), uri, sizeof(uri)) &&
        probe_window(capabilities, &window, &native) &&
        probe_input_and_cursor(capabilities, window) && probe_monitors(capabilities, window) &&
        probe_system_and_validation(capabilities) &&
        (!(capabilities & NK_CAP_DRAG_DROP) ||
         require_ok("nk_window_set_drop_enabled(true)", nk_window_set_drop_enabled(window, 1))) &&
        (!(capabilities & NK_CAP_DRAG_DROP) ||
         require_ok("nk_window_set_drop_enabled(false)", nk_window_set_drop_enabled(window, 0))) &&
        (!(capabilities & NK_CAP_RESOURCE_IO) || probe_resource_io(path, uri)) &&
        probe_clipboard_and_resources(capabilities, uri) && probe_joysticks(capabilities) &&
        probe_graphics_and_accessibility(capabilities, window, &surface) &&
        probe_webview(capabilities, window);
    if (surface != NK_INVALID_HANDLE)
        success = require_ok("nk_surface_destroy", nk_surface_destroy(surface)) && success;
    if (window != NK_INVALID_HANDLE)
        success = require_ok("nk_window_destroy", nk_window_destroy(window)) && success;
    remove(path);
    nk_shutdown();
    return success ? 0 : 1;
}
