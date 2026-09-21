#include "nativekit.h"
#include "nativekit_accessibility.h"
#include "nativekit_clipboard.h"
#include "nativekit_file_watch.h"
#include "nativekit_dialog.h"
#include "nativekit_graphics.h"
#include "nativekit_gamepad.h"
#include "nativekit_haptics.h"
#include "nativekit_input.h"
#include "nativekit_library.h"
#include "nativekit_joystick.h"
#include "nativekit_mobile.h"
#include "nativekit_monitor.h"
#include "nativekit_menu.h"
#include "nativekit_notification.h"
#include "nativekit_plugin.h"
#include "nativekit_resource.h"
#include "nativekit_sensor.h"
#include "nativekit_system.h"
#include "nativekit_time.h"
#include "nativekit_task.h"
#include "nativekit_transport.h"
#include "nativekit_vulkan.h"
#include "nativekit_webview.h"
#include "nativekit_window.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

_Static_assert(NK_GRAPHICS_D3D11 == 4, "D3D11 graphics API value is stable");
_Static_assert(NK_GRAPHICS_METAL == 5, "Metal graphics API value is stable");
_Static_assert(sizeof(nk_surface_frame_target) == 80,
               "surface frame target keeps its versioned 40-byte prefix and token tail");
_Static_assert(offsetof(nk_surface_frame_target, native_device) == 40,
               "native frame-target tokens are appended after the original ABI prefix");
_Static_assert(offsetof(nk_surface_frame_target, native_present_target) == 64,
               "present target token has a stable ABI offset");
_Static_assert(offsetof(nk_surface_frame_target, frame) == 72,
               "the frame token is appended after the native presentation token");
_Static_assert(offsetof(nk_init_options, application_id) == 16,
               "application identity is appended after the original init prefix");
_Static_assert(sizeof(nk_init_options) == 16 + 2 * sizeof(const char *),
               "init options ABI layout is stable");
_Static_assert(sizeof(nk_system_info) == 40, "system info ABI layout is stable");
_Static_assert(sizeof(nk_system_orientation) == 32, "system orientation ABI layout is stable");
_Static_assert(sizeof(nk_orientation_event) == 32, "orientation event ABI layout is stable");
_Static_assert(sizeof(nk_sensor_sample) == 56, "sensor sample ABI layout is stable");
_Static_assert(sizeof(nk_sensor_permission_event) == 20,
               "sensor permission event ABI layout is stable");
_Static_assert(sizeof(nk_gamepad_rumble_options) == 20, "gamepad rumble ABI layout is stable");
_Static_assert(sizeof(nk_window_decoration_region) == 24,
               "window decoration region ABI layout is stable");
_Static_assert(offsetof(nk_window_decoration_region, kind) == 16,
               "window decoration region kind offset is stable");
_Static_assert(sizeof(nk_file_watch_options) == 24, "file-watch options ABI layout is stable");
_Static_assert(sizeof(nk_file_changed_event) == 28, "file-change event ABI layout is stable");
_Static_assert(sizeof(nk_clipboard_watch_options) == 24,
               "clipboard-watch options ABI layout is stable");
_Static_assert(sizeof(nk_clipboard_changed_event) == 16,
               "clipboard-change event ABI layout is stable");
_Static_assert(sizeof(nk_menu) == sizeof(uint32_t), "menu handles remain four-byte tokens");
_Static_assert(sizeof(nk_library) == sizeof(uint32_t), "library handles remain four-byte tokens");
_Static_assert(sizeof(nk_menu_item) == sizeof(uint32_t),
               "menu item handles remain four-byte tokens");
_Static_assert(sizeof(nk_menu_item_activated_event) == 24,
               "menu activation payload layout is stable");
_Static_assert(sizeof(nk_task) == sizeof(uint32_t), "task handles remain four-byte tokens");
_Static_assert(sizeof(nk_task_options) == 20, "task options ABI layout is stable");
_Static_assert(offsetof(nk_task_options, execution_mode) == 4 &&
                   offsetof(nk_task_options, step_budget_us) == 8,
               "task option fields retain their stable offsets");
_Static_assert(offsetof(nk_task_step_output, progress_size) == (sizeof(void *) == 8 ? 16 : 8),
               "task progress payload fields retain their stable offsets");
_Static_assert(NK_TASK_EXECUTION_AUTO == 0 && NK_TASK_EXECUTION_BACKGROUND == 1 &&
                   NK_TASK_EXECUTION_COOPERATIVE == 2,
               "task execution modes are stable");
_Static_assert(NK_TASK_STATE_RUNNING == 0 && NK_TASK_STATE_YIELDED == 1 &&
                   NK_TASK_STATE_COMPLETED == 2 && NK_TASK_STATE_FAILED == 3 &&
                   NK_TASK_STATE_CANCELLED == 4,
               "task states are stable");
_Static_assert(NK_EVENT_TASK_PROGRESS == 1100 && NK_EVENT_TASK_COMPLETE == 1101 &&
                   NK_EVENT_TASK_FAILED == 1102 && NK_EVENT_TASK_CANCELLED == 1103,
               "task event kinds are stable");

_Static_assert(NK_EXECUTOR_PLATFORM == 0 && NK_EXECUTOR_APP == 1 && NK_EXECUTOR_RENDER == 2 &&
                   NK_EXECUTOR_WORKER == 3,
               "logical executor values are stable");
_Static_assert(NK_EVENT_PLUGIN_COMPLETE == 1000 && NK_EVENT_PLUGIN_EVENT == 1001,
               "plugin event kinds are stable");
_Static_assert(NK_EVENT_TRANSPORT_CONNECTED == 950 && NK_EVENT_TRANSPORT_FAILED == 955,
               "transport event kinds are stable");
_Static_assert(sizeof(nk_transport) == sizeof(uint32_t) && sizeof(nk_listener) == sizeof(uint32_t),
               "transport handles remain four-byte tokens");
_Static_assert(NK_EVENT_MENU_ITEM_ACTIVATED == 510 && NK_EVENT_APPLICATION_QUIT_REQUESTED == 511,
               "menu event kinds are stable");
_Static_assert(NK_ERROR_NOT_FOUND == -12 && NK_ERROR_PAYLOAD_TOO_LARGE == -13 &&
                   NK_ERROR_CANCELLED == -14,
               "plugin result codes are stable");
_Static_assert(NK_PLUGIN_ABI_VERSION == 1, "plugin ABI version is stable");
_Static_assert(NK_SURFACE_FRAME_CONTINUOUS == 0 && NK_SURFACE_FRAME_ON_DEMAND == 1,
               "surface frame scheduling modes are stable");
_Static_assert(NK_PLUGIN_PAYLOAD_MAX == 65536, "plugin control-plane payload limit is stable");
_Static_assert(NK_PLUGIN_PENDING == NK_PENDING, "plugin pending result is stable");
_Static_assert(NK_PLUGIN_BINARY_SPAN_V1_SIZE == 24, "plugin binary span v1 prefix is frozen");
_Static_assert(NK_PLUGIN_HOST_V1_SIZE == (sizeof(void *) == 8 ? 64 : 36),
               "plugin host v1 prefix is frozen for the target pointer width");
_Static_assert(NK_PLUGIN_DESCRIPTOR_V1_SIZE == (sizeof(void *) == 8 ? 32 : 20),
               "plugin descriptor v1 prefix is frozen");
_Static_assert(NK_PLUGIN_REPLY_V1_SIZE == 32, "plugin reply v1 prefix is frozen");
_Static_assert(NK_PLUGIN_SERVICE_V1_SIZE == (sizeof(void *) == 8 ? 40 : 28),
               "plugin service v1 prefix is frozen for the target pointer width");
_Static_assert(NK_PLUGIN_EVENT_DATA_V1_SIZE == 32, "plugin event data v1 prefix is frozen");
_Static_assert(NK_PLUGIN_EVENT_VIEW_V1_SIZE == (sizeof(void *) == 8 ? 64 : 56),
               "plugin event view v1 prefix is frozen for the target pointer width");
_Static_assert(offsetof(nk_plugin_host, register_service) == 8,
               "plugin host function table starts after its versioned prefix");
_Static_assert(offsetof(nk_plugin_descriptor, id) == 8,
               "plugin descriptor identity starts after its versioned prefix");
_Static_assert(sizeof(nk_plugin_descriptor) == 8 + 3 * sizeof(void *),
               "plugin descriptor ABI layout is stable");
_Static_assert(offsetof(nk_plugin_service, invoke) ==
                   offsetof(nk_plugin_service, name) + sizeof(const char *),
               "plugin service function pointer follows its diagnostics name");
_Static_assert(offsetof(nk_binary_span, data) == 8,
               "binary span bytes follow the versioned prefix");
_Static_assert(offsetof(nk_plugin_event_data, handle) == 16 &&
                   offsetof(nk_plugin_event_data, payload_size) == 24 &&
                   sizeof(nk_plugin_event_data) == 32,
               "plugin event header ABI layout is stable");

static void verify_system_string(nk_system_string_kind kind, int required) {
    uint32_t size = 0;
    const nk_result query = nk_system_get_string(kind, NULL, &size);
    if (query == NK_ERROR_UNSUPPORTED) {
        assert(!required);
        return;
    }
    assert(query == NK_ERROR_BUFFER_TOO_SMALL);
    if (size == 1) {
        assert(!required);
        return;
    }
    assert(size <= 4096);
    char value[4096] = {0};
    uint32_t capacity = sizeof(value);
    assert(nk_system_get_string(kind, value, &capacity) == NK_OK);
    assert(value[0] != '\0');
}

int main(void) {
    assert(nk_time_now_ns() > 0);
    assert(nk_time_seconds() > 0.0);
    nk_init_options options = {0};
    options.struct_size = sizeof(options);
    options.api_version = NK_API_VERSION;
    options.application_id = "com.example/nativekit";
    options.application_name = "NativeKit ABI test";
    assert(nk_api_version() == NK_API_VERSION);
    assert(nk_init(&options) == NK_OK);
    nk_task_options task_options = {0};
    task_options.struct_size = sizeof(task_options);
    nk_task abi_task = NK_INVALID_HANDLE;
    assert(nk_task_start(&task_options, NULL, NULL, &abi_task) == NK_ERROR_INVALID_ARGUMENT);
    assert(nk_task_start(NULL, NULL, NULL, &abi_task) == NK_ERROR_INVALID_ARGUMENT);
    nk_task_state abi_task_state = NK_TASK_STATE_RUNNING;
    assert(nk_task_get_state(NK_INVALID_HANDLE, &abi_task_state) == NK_ERROR_INVALID_HANDLE);
    nk_system_info system_info = {0};
    system_info.struct_size = sizeof(system_info);
    assert(nk_system_get_info(&system_info) == NK_OK);
    assert(system_info.endianness == NK_SYSTEM_ENDIAN_LITTLE ||
           system_info.endianness == NK_SYSTEM_ENDIAN_BIG);
    verify_system_string(NK_SYSTEM_STRING_PLATFORM_NAME, 0);
    verify_system_string(NK_SYSTEM_STRING_PLATFORM_VERSION, 0);
    verify_system_string(NK_SYSTEM_STRING_PLATFORM_LABEL, 0);
    verify_system_string(NK_SYSTEM_STRING_DEVICE_VENDOR, 0);
    verify_system_string(NK_SYSTEM_STRING_DEVICE_MODEL, 0);
    verify_system_string(NK_SYSTEM_STRING_APPLICATION_NAME, 1);
    uint32_t application_id_size = 0;
    assert(nk_system_get_string(NK_SYSTEM_STRING_APPLICATION_ID, NULL, &application_id_size) ==
           NK_ERROR_BUFFER_TOO_SMALL);
    assert(application_id_size == sizeof("com.example_nativekit"));
    char application_id[64] = {0};
    assert(nk_system_get_string(NK_SYSTEM_STRING_APPLICATION_ID, application_id,
                                &application_id_size) == NK_OK);
    assert(strcmp(application_id, "com.example_nativekit") == 0);
    const nk_capabilities system_capabilities = nk_get_capabilities();
    if (system_capabilities & NK_CAP_APPLICATION_STORAGE) {
        uint32_t storage_size = 0;
        assert(nk_system_directory(NK_DIRECTORY_APPLICATION_STORAGE, NULL, &storage_size) ==
               NK_ERROR_BUFFER_TOO_SMALL);
        assert(storage_size > 1);
        char storage[4096] = {0};
        uint32_t storage_capacity = sizeof(storage);
        assert(nk_system_directory(NK_DIRECTORY_APPLICATION_STORAGE, storage, &storage_capacity) ==
               NK_OK);
        assert(strstr(storage, "com.example_nativekit") != NULL);
    }
    if (system_capabilities & NK_CAP_SYSTEM_FONTS) {
        uint32_t font_size = 0;
        assert(nk_system_directory(NK_DIRECTORY_FONTS, NULL, &font_size) ==
               NK_ERROR_BUFFER_TOO_SMALL);
        assert(font_size > 1);
        char fonts[4096] = {0};
        uint32_t fonts_capacity = sizeof(fonts);
        assert(nk_system_directory(NK_DIRECTORY_FONTS, fonts, &fonts_capacity) == NK_OK);
        assert(fonts[0] != '\0');
    }
    if (system_capabilities & NK_CAP_APPLICATION_PATH) {
        uint32_t application_size = 0;
        assert(nk_system_directory(NK_DIRECTORY_APPLICATION, NULL, &application_size) ==
               NK_ERROR_BUFFER_TOO_SMALL);
        assert(application_size > 1);
    }
    nk_system_orientation system_orientation = {0};
    system_orientation.struct_size = sizeof(system_orientation);
    const nk_result orientation_result = nk_system_get_orientation(&system_orientation);
    assert(orientation_result == NK_OK || orientation_result == NK_ERROR_UNSUPPORTED);
    if (orientation_result == NK_OK) {
        assert(system_orientation.device <= NK_ORIENTATION_FACE_DOWN);
        assert(system_orientation.display <= NK_ORIENTATION_FACE_DOWN);
    }
    assert(nk_system_request_device_orientation(NULL) == NK_ERROR_INVALID_ARGUMENT);
    nk_keep_awake_options keep_awake_options = {0};
    keep_awake_options.struct_size = sizeof(keep_awake_options);
    keep_awake_options.flags = NK_KEEP_AWAKE_DISPLAY;
    nk_keep_awake first_lease = 0;
    nk_keep_awake second_lease = 0;
    nk_keep_awake shutdown_lease = 0;
    const nk_result keep_awake_result =
        nk_system_keep_awake_acquire(&keep_awake_options, &first_lease);
    if ((system_capabilities & NK_CAP_KEEP_AWAKE) && keep_awake_result == NK_OK) {
        assert(keep_awake_result == NK_OK);
        assert(first_lease != 0);
        assert(nk_system_keep_awake_acquire(&keep_awake_options, &second_lease) == NK_OK);
        assert(second_lease != 0 && second_lease != first_lease);
        assert(nk_system_keep_awake_release(first_lease) == NK_OK);
        assert(nk_system_keep_awake_release(second_lease) == NK_OK);
        assert(nk_system_keep_awake_release(second_lease) == NK_ERROR_INVALID_HANDLE);
        assert(nk_system_keep_awake_acquire(&keep_awake_options, &shutdown_lease) == NK_OK);
    } else if (system_capabilities & NK_CAP_KEEP_AWAKE) {
        assert(keep_awake_result == NK_ERROR_UNSUPPORTED);
    } else {
        assert(keep_awake_result == NK_ERROR_UNSUPPORTED);
    }
    const nk_capabilities capabilities = nk_get_capabilities();
    const nk_result accessibility_handle_result =
        (capabilities & NK_CAP_ACCESSIBILITY) ? NK_ERROR_INVALID_HANDLE : NK_ERROR_UNSUPPORTED;
    assert(nk_surface_accessibility_clear(NK_INVALID_HANDLE) == accessibility_handle_result);
    nk_accessibility_update accessibility_update = {0};
    accessibility_update.struct_size = sizeof(accessibility_update);
    assert(nk_surface_accessibility_update(NK_INVALID_HANDLE, &accessibility_update) ==
           accessibility_handle_result);
    const uint8_t malformed_removed_ids[] = {1, 2, 3};
    assert(nk_surface_accessibility_update_with_removed_ids(
               NK_INVALID_HANDLE, &accessibility_update, malformed_removed_ids,
               sizeof(malformed_removed_ids)) == NK_ERROR_INVALID_ARGUMENT);
    assert(nk_surface_accessibility_set_text_ranges(NK_INVALID_HANDLE, 1, NULL, 0) ==
           accessibility_handle_result);
    (void)nk_vulkan_supported();
    uint32_t vulkan_extension_count = 0;
    nk_result vulkan_extensions = nk_vulkan_get_required_instance_extensions(
        NK_INVALID_HANDLE, NULL, &vulkan_extension_count);
    assert(vulkan_extensions == NK_ERROR_INVALID_HANDLE ||
           vulkan_extensions == NK_ERROR_UNSUPPORTED);
    nk_result vulkan_surface = nk_vulkan_create_surface(NK_INVALID_HANDLE, NULL, NULL, NULL);
    assert(vulkan_surface == NK_ERROR_INVALID_ARGUMENT || vulkan_surface == NK_ERROR_UNSUPPORTED);
    uint32_t monitor_count = 0;
    nk_result monitor_result = nk_monitor_list(NULL, &monitor_count);
    assert(monitor_result == NK_ERROR_BUFFER_TOO_SMALL || monitor_result == NK_ERROR_UNSUPPORTED ||
           monitor_result == NK_OK);
    uint32_t joystick_count = 0;
    nk_result joystick_result = nk_joystick_list(NULL, &joystick_count);
    assert(joystick_result == NK_ERROR_BUFFER_TOO_SMALL ||
           joystick_result == NK_ERROR_UNSUPPORTED || joystick_result == NK_OK);
    uint32_t sensor_count = 0;
    nk_result sensor_result = nk_sensor_list(NULL, &sensor_count);
    assert(sensor_result == NK_ERROR_BUFFER_TOO_SMALL || sensor_result == NK_ERROR_UNSUPPORTED ||
           sensor_result == NK_OK);
    assert(nk_sensor_request_permission(NULL) == NK_ERROR_INVALID_ARGUMENT);
    assert(nk_haptic_vibrate(NULL) == NK_ERROR_INVALID_ARGUMENT);
    assert(nk_gamepad_rumble(NK_INVALID_HANDLE, NULL) == NK_ERROR_INVALID_ARGUMENT);
    uint32_t joystick_diagnostic_size = 0;
    nk_result joystick_diagnostic_result =
        nk_joystick_get_diagnostics(NULL, &joystick_diagnostic_size);
    assert(joystick_diagnostic_result == NK_ERROR_BUFFER_TOO_SMALL ||
           joystick_diagnostic_result == NK_ERROR_UNSUPPORTED);
    uint32_t mapped = 0;
    assert(nk_gamepad_add_mapping("03000000112200003344000055660000,ABI Gamepad,a:b0,leftx:a0,") ==
           NK_OK);
    uint32_t mappings_added = 0;
    assert(
        nk_gamepad_add_mappings("# mappings\n"
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
    assert(geometry_result == NK_ERROR_INVALID_HANDLE || geometry_result == NK_ERROR_UNSUPPORTED);
    nk_input_action input_action = NK_INPUT_RELEASE;
    nk_result input_result = nk_key_get_state(NK_INVALID_HANDLE, NK_KEY_A, &input_action);
    assert(input_result == NK_ERROR_INVALID_HANDLE || input_result == NK_ERROR_UNSUPPORTED);
    nk_cursor cursor = NK_INVALID_HANDLE;
    nk_result cursor_result = nk_cursor_create_standard(NK_CURSOR_ARROW, &cursor);
    if (cursor_result == NK_OK)
        assert(nk_cursor_destroy(cursor) == NK_OK);
    else
        assert(cursor_result == NK_ERROR_UNSUPPORTED);
    (void)nk_raw_pointer_motion_supported();
    nk_surface_options surface_options = {0};
    surface_options.struct_size = sizeof(surface_options);
#if defined(__APPLE__)
    surface_options.api = NK_GRAPHICS_METAL;
#elif defined(_WIN32)
    surface_options.api = NK_GRAPHICS_D3D11;
#elif defined(__ANDROID__) || defined(__EMSCRIPTEN__)
    surface_options.api = NK_GRAPHICS_OPENGL_ES;
#else
    surface_options.api = NK_GRAPHICS_OPENGL;
#endif
    surface_options.width = 1;
    surface_options.height = 1;
    nk_surface surface = NK_INVALID_HANDLE;
    nk_result surface_result = nk_surface_create(NK_INVALID_HANDLE, &surface_options, &surface);
    assert(surface_result == NK_ERROR_INVALID_HANDLE || surface_result == NK_ERROR_UNSUPPORTED);
    nk_mobile_host_options mobile = {0};
    mobile.struct_size = sizeof(mobile);
    mobile.kind = NK_MOBILE_HOST_ANDROID_VIEW_GROUP;
    nk_mobile_host mobile_host = NK_INVALID_HANDLE;
    nk_result mobile_result = nk_mobile_host_attach(&mobile, &mobile_host);
    assert(mobile_result == NK_ERROR_INVALID_ARGUMENT || mobile_result == NK_ERROR_UNSUPPORTED);
    assert(nk_mobile_host_dispatch_event(NK_INVALID_HANDLE, NULL) == NK_ERROR_INVALID_ARGUMENT);
    assert(nk_mobile_host_set_drop_enabled(NK_INVALID_HANDLE, 2) == NK_ERROR_INVALID_ARGUMENT);
    uint32_t can_navigate = 0;
    nk_result history_result = nk_webview_can_go_back(NK_INVALID_HANDLE, &can_navigate);
    assert(history_result == NK_ERROR_INVALID_HANDLE || history_result == NK_ERROR_UNSUPPORTED);
    history_result = nk_webview_can_go_back(NK_INVALID_HANDLE, NULL);
    assert(history_result == NK_ERROR_INVALID_HANDLE || history_result == NK_ERROR_UNSUPPORTED);
    nk_result notification_result = nk_notification_show(NULL, NULL);
    assert(notification_result == NK_ERROR_INVALID_ARGUMENT ||
           notification_result == NK_ERROR_UNSUPPORTED);
    nk_resource_view invalid_resource = {0};
    invalid_resource.struct_size = sizeof(invalid_resource);
    assert(nk_resource_event_item(NULL, 0, &invalid_resource) == NK_ERROR_INVALID_ARGUMENT);
    uint32_t persisted_flags = 0;
    assert(nk_resource_get_persisted_access(NULL, &persisted_flags) == NK_ERROR_INVALID_ARGUMENT);
    assert(nk_resource_set_persisted_access(NULL, 0, &persisted_flags) ==
           NK_ERROR_INVALID_ARGUMENT);
    nk_resource local_resource = {0};
    local_resource.struct_size = sizeof(local_resource);
    local_resource.uri = "file:///tmp/nativekit-persisted-access";
    assert(nk_resource_get_persisted_access(&local_resource, &persisted_flags) ==
           NK_ERROR_UNSUPPORTED);
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
    typedef struct drop_test_payload {
        nk_resource_drop drop;
        nk_resource_list resources;
        nk_resource_item item;
        char uri[21];
        char text[13];
    } drop_test_payload;
    drop_test_payload drop_data = {
        {offsetof(drop_test_payload, resources),
         offsetof(drop_test_payload, text),
         12.0f,
         24.0f,
         {0, 0}},
        {0, 1, offsetof(drop_test_payload, item), offsetof(drop_test_payload, uri)},
        {NK_RESOURCE_READABLE, offsetof(drop_test_payload, uri), 0, 0},
        "content://provider/z",
        "dropped text"};
    nk_event drop_event = {0};
    drop_event.struct_size = sizeof(drop_event);
    drop_event.kind = NK_EVENT_RESOURCE_DROP;
    drop_event.data = &drop_data;
    drop_event.data_size = sizeof(drop_data);
    resource_view.struct_size = sizeof(resource_view);
    assert(nk_resource_event_item(&drop_event, 0, &resource_view) == NK_OK);
    assert(resource_view.uri_length == 20);
    assert(nk_resource_drop_event_text(&drop_event, &share_string, &share_string_length) == NK_OK);
    assert(share_string_length == 12);
    assert(memcmp(share_string, "dropped text", 12) == 0);
    nk_resource_stream_info stream_info = {0};
    stream_info.struct_size = sizeof(stream_info);
    assert(nk_resource_stream_info_get(NK_INVALID_HANDLE, &stream_info) == NK_ERROR_INVALID_HANDLE);
    assert(nk_resource_read(NK_INVALID_HANDLE, NULL, 0, &stream_info.size) ==
           NK_ERROR_INVALID_HANDLE);
    assert(nk_resource_write(NK_INVALID_HANDLE, NULL, 0, &stream_info.size) ==
           NK_ERROR_INVALID_HANDLE);
    assert(nk_resource_seek(NK_INVALID_HANDLE, 0, NK_SEEK_START, &stream_info.size) ==
           NK_ERROR_INVALID_HANDLE);
    assert(nk_resource_close(NK_INVALID_HANDLE) == NK_ERROR_INVALID_HANDLE);
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
    if (locale_result == NK_ERROR_BUFFER_TOO_SMALL) {
        assert(locale_size > 1);
        char locale[128] = {0};
        uint32_t locale_capacity = sizeof(locale);
        assert(nk_system_locale(locale, &locale_capacity) == NK_OK);
        assert(locale[0] != '\0');
    }
    nk_system_appearance appearance = {0};
    appearance.struct_size = sizeof(appearance);
    const nk_result appearance_result = nk_system_get_appearance(&appearance);
    assert(appearance_result == NK_OK || appearance_result == NK_ERROR_UNSUPPORTED);
    if (appearance_result == NK_OK) {
        assert(appearance.color_scheme == NK_COLOR_SCHEME_LIGHT ||
               appearance.color_scheme == NK_COLOR_SCHEME_DARK);
        assert(appearance.high_contrast <= 1);
    }
    nk_menu_options menu_options = {0};
    menu_options.struct_size = sizeof(menu_options);
    menu_options.title = "NativeKit";
    nk_menu menu = NK_INVALID_HANDLE;
    assert(nk_menu_create(NULL, &menu) == NK_ERROR_INVALID_ARGUMENT);
    assert(nk_menu_create(&menu_options, &menu) == NK_OK);
    nk_menu_item_options file_options = {0};
    file_options.struct_size = sizeof(file_options);
    file_options.kind = NK_MENU_ITEM_SUBMENU;
    file_options.label = "File";
    nk_menu_item file_item = NK_INVALID_HANDLE;
    assert(nk_menu_add_item(menu, NK_INVALID_HANDLE, &file_options, &file_item) == NK_OK);
    nk_menu_item_options command_options = {0};
    command_options.struct_size = sizeof(command_options);
    command_options.command_id = 42;
    command_options.label = "Close";
    command_options.shortcut.key = NK_KEY_W;
    command_options.shortcut.modifiers = NK_MENU_MOD_PRIMARY;
    nk_menu_item command_item = NK_INVALID_HANDLE;
    assert(nk_menu_add_item(menu, file_item, &command_options, &command_item) == NK_OK);
    command_options.shortcut.key = NK_KEY_LEFT_SHIFT;
    nk_menu_item invalid_shortcut_item = NK_INVALID_HANDLE;
    assert(nk_menu_add_item(menu, file_item, &command_options, &invalid_shortcut_item) ==
           NK_ERROR_INVALID_ARGUMENT);
    assert(nk_menu_item_set_enabled(command_item, 0) == NK_OK);
    assert(nk_menu_item_set_enabled(command_item, 1) == NK_OK);
    assert(nk_menu_item_set_label(command_item, "Close Window") == NK_OK);
    const nk_result menu_install_result = nk_application_set_menu(menu);
    if (nk_get_capabilities() & NK_CAP_APPLICATION_MENU)
        assert(menu_install_result == NK_OK);
    else
        assert(menu_install_result == NK_ERROR_UNSUPPORTED);
    assert(nk_application_set_menu(NK_INVALID_HANDLE) == NK_OK);
    assert(nk_menu_destroy(menu) == NK_OK);
    assert(nk_menu_item_set_enabled(command_item, 1) == NK_ERROR_INVALID_HANDLE);
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
               event.kind == NK_EVENT_JOYSTICK_DISCONNECTED ||
               event.kind == NK_EVENT_DEVICE_ORIENTATION_CHANGED ||
               event.kind == NK_EVENT_DISPLAY_ORIENTATION_CHANGED ||
               event.kind == NK_EVENT_DEVICE_ORIENTATION_PERMISSION_COMPLETE);
        nk_event_kind kind = event.kind;
        nk_event_release(&event);
        event.struct_size = sizeof(event);
        if (kind == NK_EVENT_NONE)
            break;
    }

    nk_shutdown();
    assert(nk_poll_event(&event) == NK_ERROR_NOT_INITIALIZED);
    assert(nk_init(&options) == NK_OK);
    if (shutdown_lease)
        assert(nk_system_keep_awake_release(shutdown_lease) == NK_ERROR_INVALID_HANDLE);
    assert(nk_poll_event(&event) == NK_OK);
    assert(event.kind == NK_EVENT_NONE);
    nk_shutdown();
    return 0;
}
