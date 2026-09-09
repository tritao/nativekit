#include "nativekit.h"
#include "nativekit_clipboard.h"
#include "nativekit_dialog.h"
#include "nativekit_graphics.h"
#include "nativekit_gamepad.h"
#include "nativekit_input.h"
#include "nativekit_joystick.h"
#include "nativekit_mobile.h"
#include "nativekit_monitor.h"
#include "nativekit_notification.h"
#include "nativekit_resource.h"
#include "nativekit_system.h"
#include "nativekit_vulkan.h"
#include "nativekit_webview.h"
#include "nativekit_window.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

int main(void) {
    nk_init_options options = {0};
    options.struct_size = sizeof(options);
    options.api_version = NK_API_VERSION;
    assert(nk_api_version() == NK_API_VERSION);
    assert(nk_init(&options) == NK_OK);
    (void)nk_get_capabilities();
    (void)nk_vulkan_supported();
    uint32_t vulkan_extension_count = 0;
    nk_result vulkan_extensions = nk_vulkan_get_required_instance_extensions(
        NK_INVALID_HANDLE, NULL, &vulkan_extension_count);
    assert(vulkan_extensions == NK_ERROR_INVALID_HANDLE ||
           vulkan_extensions == NK_ERROR_UNSUPPORTED);
    nk_result vulkan_surface =
        nk_vulkan_create_surface(NK_INVALID_HANDLE, NULL, NULL, NULL);
    assert(vulkan_surface == NK_ERROR_INVALID_ARGUMENT ||
           vulkan_surface == NK_ERROR_UNSUPPORTED);
    uint32_t monitor_count = 0;
    nk_result monitor_result = nk_monitor_list(NULL, &monitor_count);
    assert(monitor_result == NK_ERROR_BUFFER_TOO_SMALL ||
           monitor_result == NK_ERROR_UNSUPPORTED || monitor_result == NK_OK);
    uint32_t joystick_count = 0;
    nk_result joystick_result = nk_joystick_list(NULL, &joystick_count);
    assert(joystick_result == NK_ERROR_BUFFER_TOO_SMALL ||
           joystick_result == NK_ERROR_UNSUPPORTED || joystick_result == NK_OK);
    uint32_t joystick_diagnostic_size = 0;
    nk_result joystick_diagnostic_result =
        nk_joystick_get_diagnostics(NULL, &joystick_diagnostic_size);
    assert(joystick_diagnostic_result == NK_ERROR_BUFFER_TOO_SMALL ||
           joystick_diagnostic_result == NK_ERROR_UNSUPPORTED);
    uint32_t mapped = 0;
    assert(nk_gamepad_add_mapping(
               "03000000112200003344000055660000,ABI Gamepad,a:b0,leftx:a0,") == NK_OK);
    uint32_t mappings_added = 0;
    assert(nk_gamepad_add_mappings(
               "# mappings\n"
               "03000000112200003344000055660001,ABI Bulk,a:b0,platform:Linux,\n",
               &mappings_added) == NK_OK);
#if defined(__linux__) && !defined(__ANDROID__)
    assert(mappings_added == 1);
#endif
    uint32_t mapping_revision_size = 0;
    assert(nk_gamepad_get_builtin_database_revision(NULL, &mapping_revision_size) ==
           NK_ERROR_BUFFER_TOO_SMALL);
    assert(mapping_revision_size == 41);
    nk_gamepad_options gamepad_options = {0};
    gamepad_options.struct_size = sizeof(gamepad_options);
    gamepad_options.stick_dead_zone = 0.2f;
    gamepad_options.trigger_dead_zone = 0.1f;
    gamepad_options.flags = NK_GAMEPAD_TRIGGER_ZERO_TO_ONE;
    assert(nk_gamepad_set_options(&gamepad_options) == NK_OK);
    nk_gamepad_options returned_gamepad_options = {0};
    returned_gamepad_options.struct_size = sizeof(returned_gamepad_options);
    assert(nk_gamepad_get_options(&returned_gamepad_options) == NK_OK);
    assert(returned_gamepad_options.stick_dead_zone == 0.2f);
    nk_result mapped_result = nk_gamepad_is_mapped(NK_INVALID_HANDLE, &mapped);
    assert(mapped_result == NK_ERROR_INVALID_HANDLE || mapped_result == NK_ERROR_UNSUPPORTED);
    int32_t window_width = 0;
    int32_t window_height = 0;
    nk_result geometry_result =
        nk_window_get_size(NK_INVALID_HANDLE, &window_width, &window_height);
    assert(geometry_result == NK_ERROR_INVALID_HANDLE ||
           geometry_result == NK_ERROR_UNSUPPORTED);
    nk_input_action input_action = NK_INPUT_RELEASE;
    nk_result input_result = nk_key_get_state(NK_INVALID_HANDLE, NK_KEY_A, &input_action);
    assert(input_result == NK_ERROR_INVALID_HANDLE || input_result == NK_ERROR_UNSUPPORTED);
    nk_handle cursor = NK_INVALID_HANDLE;
    nk_result cursor_result = nk_cursor_create_standard(NK_CURSOR_ARROW, &cursor);
    if (cursor_result == NK_OK)
        assert(nk_cursor_destroy(cursor) == NK_OK);
    else
        assert(cursor_result == NK_ERROR_UNSUPPORTED);
    (void)nk_raw_pointer_motion_supported();
    nk_surface_options surface_options = {0};
    surface_options.struct_size = sizeof(surface_options);
    surface_options.api = NK_GRAPHICS_OPENGL;
    surface_options.width = 1;
    surface_options.height = 1;
    nk_handle surface = NK_INVALID_HANDLE;
    nk_result surface_result =
        nk_surface_create(NK_INVALID_HANDLE, &surface_options, &surface);
    assert(surface_result == NK_ERROR_INVALID_HANDLE || surface_result == NK_ERROR_UNSUPPORTED);
    nk_mobile_host_options mobile = {0};
    mobile.struct_size = sizeof(mobile);
    mobile.kind = NK_MOBILE_HOST_ANDROID_VIEW_GROUP;
    nk_handle mobile_host = NK_INVALID_HANDLE;
    nk_result mobile_result = nk_mobile_host_attach(&mobile, &mobile_host);
    assert(mobile_result == NK_ERROR_INVALID_ARGUMENT || mobile_result == NK_ERROR_UNSUPPORTED);
    assert(nk_mobile_host_dispatch_event(NK_INVALID_HANDLE, NULL) ==
           NK_ERROR_INVALID_ARGUMENT);
    nk_result notification_result = nk_notification_show(NULL, NULL);
    assert(notification_result == NK_ERROR_INVALID_ARGUMENT ||
           notification_result == NK_ERROR_UNSUPPORTED);
    assert(nk_dialog_event_path(NULL, 0, NULL, NULL) == NK_ERROR_INVALID_ARGUMENT);
    nk_resource_view invalid_resource = {0};
    invalid_resource.struct_size = sizeof(invalid_resource);
    assert(nk_resource_event_item(NULL, 0, &invalid_resource) == NK_ERROR_INVALID_ARGUMENT);
    typedef struct resource_test_payload {
        nk_resource_list header;
        nk_resource_item item;
        char uri[21];
        char mime[11];
        char name[8];
    } resource_test_payload;
    resource_test_payload resource_data = {
        {0, 1, offsetof(resource_test_payload, item), offsetof(resource_test_payload, uri)},
        {NK_RESOURCE_READABLE, offsetof(resource_test_payload, uri),
         offsetof(resource_test_payload, mime), offsetof(resource_test_payload, name)},
        "content://provider/x",
        "text/plain",
        "Example"};
    nk_event resource_event = {0};
    resource_event.struct_size = sizeof(resource_event);
    resource_event.kind = NK_EVENT_CLIPBOARD_RESOURCES_COMPLETE;
    resource_event.data = &resource_data;
    resource_event.data_size = sizeof(resource_data);
    nk_resource_view resource_view = {0};
    resource_view.struct_size = sizeof(resource_view);
    assert(nk_resource_event_item(&resource_event, 0, &resource_view) == NK_OK);
    assert(resource_view.flags == NK_RESOURCE_READABLE);
    assert(resource_view.uri_length == 20);
    assert(memcmp(resource_view.uri, "content://provider/x", 20) == 0);
    assert(resource_view.mime_type_length == 10);
    assert(resource_view.display_name_length == 7);
    typedef struct share_test_payload {
        nk_received_share share;
        nk_resource_list resources;
        nk_resource_item item;
        char uri[21];
        char text[12];
        char subject[8];
    } share_test_payload;
    share_test_payload share_data = {
        {offsetof(share_test_payload, resources), offsetof(share_test_payload, text),
         offsetof(share_test_payload, subject), 0},
        {0, 1, offsetof(share_test_payload, item), offsetof(share_test_payload, uri)},
        {NK_RESOURCE_READABLE, offsetof(share_test_payload, uri), 0, 0},
        "content://provider/y",
        "shared text",
        "Subject"};
    nk_event share_event = {0};
    share_event.struct_size = sizeof(share_event);
    share_event.kind = NK_EVENT_SHARE_RECEIVED;
    share_event.data = &share_data;
    share_event.data_size = sizeof(share_data);
    resource_view.struct_size = sizeof(resource_view);
    assert(nk_resource_event_item(&share_event, 0, &resource_view) == NK_OK);
    assert(resource_view.uri_length == 20);
    assert(memcmp(resource_view.uri, "content://provider/y", 20) == 0);
    const char *share_string = NULL;
    uint32_t share_string_length = 0;
    assert(nk_share_event_text(&share_event, &share_string, &share_string_length) == NK_OK);
    assert(share_string_length == 11);
    assert(memcmp(share_string, "shared text", 11) == 0);
    assert(nk_share_event_subject(&share_event, &share_string, &share_string_length) == NK_OK);
    assert(share_string_length == 7);
    assert(memcmp(share_string, "Subject", 7) == 0);
    nk_resource_stream_info stream_info = {0};
    stream_info.struct_size = sizeof(stream_info);
    assert(nk_resource_stream_info_get(NK_INVALID_HANDLE, &stream_info) ==
           NK_ERROR_INVALID_HANDLE);
    assert(nk_resource_read(NK_INVALID_HANDLE, NULL, 0, &stream_info.size) ==
           NK_ERROR_INVALID_HANDLE);
    assert(nk_resource_write(NK_INVALID_HANDLE, NULL, 0, &stream_info.size) ==
           NK_ERROR_INVALID_HANDLE);
    assert(nk_resource_seek(NK_INVALID_HANDLE, 0, NK_SEEK_START, &stream_info.size) ==
           NK_ERROR_INVALID_HANDLE);
    assert(nk_resource_close(NK_INVALID_HANDLE) == NK_ERROR_INVALID_HANDLE);
    struct {
        nk_dialog_paths header;
        uint32_t offset;
        char path[4];
    } packed = {{1, 1, sizeof(nk_dialog_paths), sizeof(nk_dialog_paths) + sizeof(uint32_t)},
                sizeof(nk_dialog_paths) + sizeof(uint32_t),
                "abc"};
    nk_event packed_event = {0};
    packed_event.struct_size = sizeof(packed_event);
    packed_event.kind = NK_EVENT_DIALOG_COMPLETE;
    packed_event.data = &packed;
    packed_event.data_size = sizeof(packed);
    const char *decoded_path = NULL;
    uint32_t decoded_length = 0;
    assert(nk_dialog_event_path(&packed_event, 0, &decoded_path, &decoded_length) == NK_OK);
    assert(decoded_length == 3);
    assert(memcmp(decoded_path, "abc", 3) == 0);
    uint32_t home_size = 0;
    nk_result home_result = nk_system_directory(NK_DIRECTORY_HOME, NULL, &home_size);
    assert(home_result == NK_ERROR_BUFFER_TOO_SMALL || home_result == NK_ERROR_UNSUPPORTED);
    if (home_result == NK_ERROR_BUFFER_TOO_SMALL) {
        assert(home_size > 1);
        char home[4096];
        uint32_t home_capacity = sizeof(home);
        assert(nk_system_directory(NK_DIRECTORY_HOME, home, &home_capacity) == NK_OK);
        assert(home[0] != '\0');
    }
    uint32_t locale_size = 0;
    nk_result locale_result = nk_system_locale(NULL, &locale_size);
    assert(locale_result == NK_ERROR_BUFFER_TOO_SMALL || locale_result == NK_ERROR_UNSUPPORTED);
    if (locale_result == NK_ERROR_BUFFER_TOO_SMALL)
        assert(locale_size > 1);
    struct {
        nk_clipboard_files header;
        char path[10];
    } clipboard_data = {{1, sizeof(nk_clipboard_files)}, "/tmp/file"};
    nk_event clipboard_event = {0};
    clipboard_event.struct_size = sizeof(clipboard_event);
    clipboard_event.kind = NK_EVENT_CLIPBOARD_FILES_COMPLETE;
    clipboard_event.data = &clipboard_data;
    clipboard_event.data_size = sizeof(clipboard_data);
    const char *clipboard_path = NULL;
    uint32_t clipboard_path_length = 0;
    assert(nk_clipboard_event_file(&clipboard_event, 0, &clipboard_path, &clipboard_path_length) ==
           NK_OK);
    assert(clipboard_path_length == 9);
    assert(nk_init(&options) == NK_ERROR_ALREADY_INITIALIZED);
    assert(strlen(nk_last_error()) > 0);

    nk_event event = {0};
    event.struct_size = sizeof(event);
    for (;;) {
        assert(nk_poll_event(&event) == NK_OK);
        assert(event.kind == NK_EVENT_NONE || event.kind == NK_EVENT_JOYSTICK_CONNECTED ||
               event.kind == NK_EVENT_JOYSTICK_DISCONNECTED);
        nk_event_kind kind = event.kind;
        nk_event_release(&event);
        event.struct_size = sizeof(event);
        if (kind == NK_EVENT_NONE)
            break;
    }

    nk_shutdown();
    assert(nk_poll_event(&event) == NK_ERROR_NOT_INITIALIZED);
    assert(nk_init(&options) == NK_OK);
    assert(nk_poll_event(&event) == NK_OK);
    assert(event.kind == NK_EVENT_NONE);
    nk_shutdown();
    return 0;
}
