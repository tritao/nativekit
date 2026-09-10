#include "nativekit.h"
#include "nativekit_gamepad.h"
#include "nativekit_graphics.h"
#include "nativekit_input.h"
#include "nativekit_resource.h"
#include "nativekit_vulkan.h"
#include "nativekit_webview.h"

#include <jni.h>
#include <dlfcn.h>

#include <cstring>

namespace {
struct ApplicationInfo {
    uint32_t type = 0;
    const void *next = nullptr;
    const char *name = "NativeKit probe";
    uint32_t application_version = 1;
    const char *engine = "NativeKit";
    uint32_t engine_version = 1;
    uint32_t api_version = (1u << 22);
};

struct InstanceCreateInfo {
    uint32_t type = 1;
    const void *next = nullptr;
    uint32_t flags = 0;
    const ApplicationInfo *application = nullptr;
    uint32_t layer_count = 0;
    const char *const *layers = nullptr;
    uint32_t extension_count = 0;
    const char *const *extensions = nullptr;
};

using DestroyInstance = void (*)(void *, const void *);
void *vulkan_library = nullptr;
void *vulkan_instance = nullptr;
nk_vulkan_surface live_vulkan_surface = NK_INVALID_VULKAN_SURFACE;
DestroyInstance destroy_vulkan_instance = nullptr;

void release_vulkan_probe() {
    if (live_vulkan_surface && vulkan_instance)
        nk_vulkan_destroy_surface(vulkan_instance, live_vulkan_surface, nullptr);
    live_vulkan_surface = NK_INVALID_VULKAN_SURFACE;
    if (vulkan_instance && destroy_vulkan_instance)
        destroy_vulkan_instance(vulkan_instance, nullptr);
    vulkan_instance = nullptr;
    destroy_vulkan_instance = nullptr;
    if (vulkan_library)
        dlclose(vulkan_library);
    vulkan_library = nullptr;
}
} // namespace

extern "C" JNIEXPORT jlong JNICALL
Java_io_nativekit_consumer_MainActivity_nativeProbe(JNIEnv *, jclass, jlong host) {
    nk_webview_options options{};
    options.struct_size = sizeof(options);
    options.width = 320;
    options.height = 240;
    options.initial_url = "data:text/html,<h1>NativeKit source consumer</h1>";
    nk_handle webview = NK_INVALID_HANDLE;
    if (nk_webview_create(static_cast<nk_handle>(host), &options, &webview) != NK_OK)
        return 0;
    return static_cast<jlong>(nk_api_version()) << 32 | webview;
}

extern "C" JNIEXPORT jlong JNICALL
Java_io_nativekit_consumer_MainActivity_nativeCreateSurfaceProbe(JNIEnv *, jclass, jlong host) {
    nk_surface_options options{};
    options.struct_size = sizeof(options);
    options.flags = NK_SURFACE_DEPTH;
    options.api = NK_GRAPHICS_OPENGL_ES;
    options.major_version = 2;
    options.x = 8;
    options.y = 8;
    options.width = 64;
    options.height = 48;
    nk_handle surface = NK_INVALID_HANDLE;
    return nk_surface_create(static_cast<nk_handle>(host), &options, &surface) == NK_OK
               ? static_cast<jlong>(surface)
               : 0;
}

extern "C" JNIEXPORT jint JNICALL
Java_io_nativekit_consumer_MainActivity_nativeGraphicsSurfaceProbe(JNIEnv *, jclass,
                                                                    jlong surface_value) {
    const auto surface = static_cast<nk_handle>(surface_value);
    nk_event event{};
    bool ready = false;
    bool resized = false;
    event.struct_size = sizeof(event);
    for (int attempt = 0; attempt < 64; ++attempt) {
        if (nk_poll_event(&event) != NK_OK)
            return 1;
        if (event.kind == NK_EVENT_NONE)
            break;
        if (event.source == surface && event.kind == NK_EVENT_SURFACE_READY)
            ready = true;
        if (event.source == surface && event.kind == NK_EVENT_SURFACE_RESIZE)
            resized = true;
        nk_event_release(&event);
        event.struct_size = sizeof(event);
    }
    if (!ready || !resized)
        return 2;
    int32_t width = 0;
    int32_t height = 0;
    if (nk_surface_make_current(surface) != NK_OK ||
        nk_surface_get_framebuffer_size(surface, &width, &height) != NK_OK || width <= 0 ||
        height <= 0)
        return 3;
    nk_graphics_proc clear_color = nullptr;
    if (nk_surface_get_proc_address(surface, "glClearColor", &clear_color) != NK_OK ||
        !clear_color)
        return 4;
    if (nk_surface_present(surface) != NK_OK)
        return 5;
    if (nk_surface_set_bounds(surface, 12, 12, 80, 60) != NK_OK)
        return 6;
    return 0;
}

extern "C" JNIEXPORT jint JNICALL
Java_io_nativekit_consumer_MainActivity_nativeSetSurfaceVisible(JNIEnv *, jclass,
                                                                 jlong surface, jboolean visible) {
    return nk_surface_show(static_cast<nk_handle>(surface), visible ? 1u : 0u);
}

extern "C" JNIEXPORT jint JNICALL
Java_io_nativekit_consumer_MainActivity_nativeSurfaceLifecycleProbe(JNIEnv *, jclass,
                                                                    jlong surface_value,
                                                                    jint expected_kind) {
    const auto surface = static_cast<nk_handle>(surface_value);
    nk_event event{};
    event.struct_size = sizeof(event);
    bool found = false;
    for (int attempt = 0; attempt < 64; ++attempt) {
        if (nk_poll_event(&event) != NK_OK)
            return 1;
        if (event.kind == NK_EVENT_NONE)
            break;
        if (event.source == surface && event.kind == static_cast<nk_event_kind>(expected_kind))
            found = true;
        nk_event_release(&event);
        event.struct_size = sizeof(event);
    }
    if (!found)
        return 2;
    if (expected_kind == NK_EVENT_SURFACE_LOST)
        return nk_surface_make_current(surface) == NK_ERROR_INVALID_REQUEST ? 0 : 3;
    return nk_surface_make_current(surface) == NK_OK && nk_surface_present(surface) == NK_OK ? 0
                                                                                             : 4;
}

extern "C" JNIEXPORT jlong JNICALL
Java_io_nativekit_consumer_MainActivity_nativeCreateVulkanSurfaceProbe(JNIEnv *, jclass,
                                                                        jlong host) {
    nk_surface_options options{};
    options.struct_size = sizeof(options);
    options.api = NK_GRAPHICS_VULKAN;
    options.x = 96;
    options.y = 8;
    options.width = 64;
    options.height = 48;
    nk_handle surface = NK_INVALID_HANDLE;
    return nk_surface_create(static_cast<nk_handle>(host), &options, &surface) == NK_OK
               ? static_cast<jlong>(surface)
               : 0;
}

extern "C" JNIEXPORT jint JNICALL
Java_io_nativekit_consumer_MainActivity_nativeVulkanSurfaceProbe(JNIEnv *, jclass,
                                                                  jlong surface_value) {
    if (!nk_vulkan_supported())
        return 1;
    const auto surface_handle = static_cast<nk_handle>(surface_value);
    nk_event event{};
    event.struct_size = sizeof(event);
    bool ready = false;
    for (int attempt = 0; attempt < 64; ++attempt) {
        if (nk_poll_event(&event) != NK_OK)
            return 2;
        if (event.kind == NK_EVENT_NONE)
            break;
        if (event.kind == NK_EVENT_SURFACE_READY && event.source == surface_handle)
            ready = true;
        nk_event_release(&event);
        event.struct_size = sizeof(event);
    }
    if (!ready)
        return 2;
    uint32_t extension_count = 0;
    if (nk_vulkan_get_required_instance_extensions(surface_handle, nullptr, &extension_count) !=
            NK_ERROR_BUFFER_TOO_SMALL ||
        extension_count != 2)
        return 3;
    const char *extensions[2]{};
    if (nk_vulkan_get_required_instance_extensions(surface_handle, extensions,
                                                    &extension_count) != NK_OK)
        return 4;
    ApplicationInfo application;
    InstanceCreateInfo info;
    info.application = &application;
    info.extension_count = extension_count;
    info.extensions = extensions;
    release_vulkan_probe();
    vulkan_library = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    using CreateInstance = int32_t (*)(const InstanceCreateInfo *, const void *, void **);
    auto create_instance = vulkan_library
                               ? reinterpret_cast<CreateInstance>(
                                     dlsym(vulkan_library, "vkCreateInstance"))
                               : nullptr;
    destroy_vulkan_instance = vulkan_library
                                  ? reinterpret_cast<DestroyInstance>(
                                        dlsym(vulkan_library, "vkDestroyInstance"))
                                  : nullptr;
    if (!create_instance || !destroy_vulkan_instance ||
        create_instance(&info, nullptr, &vulkan_instance) != 0)
        return 5;
    const auto created = nk_vulkan_create_surface(surface_handle, vulkan_instance, nullptr,
                                                  &live_vulkan_surface);
    if (created != NK_OK) {
        release_vulkan_probe();
        return 6;
    }
    return 0;
}

extern "C" JNIEXPORT jint JNICALL
Java_io_nativekit_consumer_MainActivity_nativeVulkanSurfaceLostProbe(JNIEnv *, jclass,
                                                                      jlong surface_value) {
    nk_event event{};
    event.struct_size = sizeof(event);
    bool lost = false;
    for (int attempt = 0; attempt < 64; ++attempt) {
        if (nk_poll_event(&event) != NK_OK)
            return 1;
        if (event.kind == NK_EVENT_NONE)
            break;
        if (event.kind == NK_EVENT_SURFACE_LOST &&
            event.source == static_cast<nk_handle>(surface_value))
            lost = true;
        nk_event_release(&event);
        event.struct_size = sizeof(event);
    }
    if (!lost || !live_vulkan_surface || !vulkan_instance)
        return 2;
    if (nk_vulkan_destroy_surface(vulkan_instance, live_vulkan_surface, nullptr) != NK_OK)
        return 3;
    live_vulkan_surface = NK_INVALID_VULKAN_SURFACE;
    return 0;
}

extern "C" JNIEXPORT jint JNICALL
Java_io_nativekit_consumer_MainActivity_nativeVulkanSurfaceRecreatedProbe(JNIEnv *, jclass,
                                                                           jlong surface_value) {
    nk_event event{};
    event.struct_size = sizeof(event);
    bool ready = false;
    for (int attempt = 0; attempt < 64; ++attempt) {
        if (nk_poll_event(&event) != NK_OK)
            return 1;
        if (event.kind == NK_EVENT_NONE)
            break;
        if (event.kind == NK_EVENT_SURFACE_READY &&
            event.source == static_cast<nk_handle>(surface_value))
            ready = true;
        nk_event_release(&event);
        event.struct_size = sizeof(event);
    }
    if (!ready || !vulkan_instance || live_vulkan_surface)
        return 2;
    if (nk_vulkan_create_surface(static_cast<nk_handle>(surface_value), vulkan_instance, nullptr,
                                 &live_vulkan_surface) != NK_OK)
        return 3;
    release_vulkan_probe();
    return 0;
}

extern "C" JNIEXPORT jint JNICALL
Java_io_nativekit_consumer_MainActivity_nativeInputProbe(JNIEnv *, jclass, jlong surface_value) {
    const auto surface = static_cast<nk_handle>(surface_value);
    int touch_begin = 0;
    int touch_move = 0;
    int touch_end = 0;
    bool first_finger = false;
    bool second_finger = false;
    bool stylus = false;
    bool pointer_move = false;
    bool pointer_button = false;
    bool pointer_scroll = false;
    bool key_press = false;
    bool key_release = false;
    bool text = false;
    bool gamepad_axis = false;
    bool gamepad_button = false;
    nk_handle controller = NK_INVALID_HANDLE;
    nk_event event{};
    event.struct_size = sizeof(event);
    for (int attempt = 0; attempt < 128; ++attempt) {
        if (nk_poll_event(&event) != NK_OK)
            return 1;
        if (event.kind == NK_EVENT_NONE)
            break;
        if (event.kind == NK_EVENT_JOYSTICK_CONNECTED)
            controller = event.source;
        if (event.source == surface && event.kind == NK_EVENT_TOUCH &&
            event.data_size >= sizeof(nk_touch_event)) {
            const auto *value = static_cast<const nk_touch_event *>(event.data);
            touch_begin += value->action == NK_TOUCH_BEGIN;
            touch_move += value->action == NK_TOUCH_MOVE;
            touch_end += value->action == NK_TOUCH_END;
            first_finger |= value->pointer_id == 7 && value->tool == NK_TOUCH_TOOL_FINGER;
            second_finger |= value->pointer_id == 11 && value->tool == NK_TOUCH_TOOL_FINGER;
            stylus |= value->pointer_id == 19 && value->tool == NK_TOUCH_TOOL_STYLUS &&
                      value->pressure > 0.6f && value->tilt_y != 0.f;
        } else if (event.source == surface && event.kind == NK_EVENT_POINTER_MOVE) {
            pointer_move = true;
        } else if (event.source == surface && event.kind == NK_EVENT_POINTER_BUTTON &&
                   event.data_size >= sizeof(nk_pointer_button_event)) {
            const auto *value = static_cast<const nk_pointer_button_event *>(event.data);
            pointer_button = value->button == NK_POINTER_BUTTON_RIGHT &&
                             value->action == NK_INPUT_PRESS;
        } else if (event.source == surface && event.kind == NK_EVENT_POINTER_SCROLL &&
                   event.data_size >= sizeof(nk_pointer_scroll_event)) {
            const auto *value = static_cast<const nk_pointer_scroll_event *>(event.data);
            pointer_scroll = value->x == 1.5 && value->y == -2.0;
        } else if (event.source == surface && event.kind == NK_EVENT_KEY &&
                   event.data_size >= sizeof(nk_key_event)) {
            const auto *value = static_cast<const nk_key_event *>(event.data);
            key_press |= value->key == NK_KEY_A && value->action == NK_INPUT_PRESS &&
                         (value->modifiers & NK_MOD_SHIFT);
            key_release |= value->key == NK_KEY_A && value->action == NK_INPUT_RELEASE;
        } else if (event.source == surface && event.kind == NK_EVENT_TEXT_INPUT &&
                   event.data_size >= sizeof(nk_text_input_event)) {
            text = static_cast<const nk_text_input_event *>(event.data)->codepoint == 'A';
        } else if (event.kind == NK_EVENT_GAMEPAD_AXIS &&
                   event.data_size >= sizeof(nk_gamepad_axis_event)) {
            const auto *value = static_cast<const nk_gamepad_axis_event *>(event.data);
            controller = event.source;
            gamepad_axis |= value->axis == NK_GAMEPAD_AXIS_LEFT_X && value->value == 0.5f;
        } else if (event.kind == NK_EVENT_GAMEPAD_BUTTON &&
                   event.data_size >= sizeof(nk_gamepad_button_event)) {
            const auto *value = static_cast<const nk_gamepad_button_event *>(event.data);
            controller = event.source;
            gamepad_button = value->button == NK_GAMEPAD_BUTTON_A && value->pressed;
        }
        nk_event_release(&event);
        event.struct_size = sizeof(event);
    }
    if (touch_begin < 3 || touch_move < 2 || touch_end < 3 || !first_finger || !second_finger ||
        !stylus)
        return 2;
    if (!pointer_move || !pointer_button || !pointer_scroll)
        return 3;
    if (!key_press || !key_release || !text)
        return 4;
    if (!controller || !gamepad_axis || !gamepad_button)
        return 5;
    uint32_t mapped = 0;
    nk_gamepad_state gamepad{};
    gamepad.struct_size = sizeof(gamepad);
    if (nk_gamepad_is_mapped(controller, &mapped) != NK_OK || !mapped ||
        nk_gamepad_get_state(controller, &gamepad) != NK_OK ||
        gamepad.axes[NK_GAMEPAD_AXIS_LEFT_X] != 0.5f || !gamepad.buttons[NK_GAMEPAD_BUTTON_A])
        return 9;
    nk_input_action state = NK_INPUT_PRESS;
    if (nk_key_get_state(surface, NK_KEY_A, &state) != NK_OK || state != NK_INPUT_RELEASE)
        return 6;
    if (nk_pointer_button_get_state(surface, NK_POINTER_BUTTON_RIGHT, &state) != NK_OK ||
        state != NK_INPUT_PRESS)
        return 7;
    double x = 0;
    double y = 0;
    if (nk_pointer_get_position(surface, &x, &y) != NK_OK || x <= 0 || y <= 0)
        return 8;
    return 0;
}

extern "C" JNIEXPORT jint JNICALL
Java_io_nativekit_consumer_MainActivity_nativeResourceClipboardProbe(JNIEnv *, jclass) {
    nk_resource input[2]{};
    input[0].struct_size = sizeof(nk_resource);
    input[0].uri = "content://io.nativekit.consumer.resources/clipboard-one";
    input[0].mime_type = "application/octet-stream";
    input[0].display_name = "ignored-one";
    input[1].struct_size = sizeof(nk_resource);
    input[1].uri = "content://io.nativekit.consumer.resources/clipboard-two";
    input[1].mime_type = "application/octet-stream";
    input[1].display_name = "ignored-two";
    if (nk_clipboard_set_resources(input, 2) != NK_OK)
        return 1;
    nk_request_id request = NK_INVALID_REQUEST_ID;
    if (nk_clipboard_read_resources(&request) != NK_OK)
        return 2;
    nk_event event{};
    event.struct_size = sizeof(event);
    for (int attempt = 0; attempt < 64; ++attempt) {
        if (nk_poll_event(&event) != NK_OK)
            return 11;
        if (event.kind == NK_EVENT_CLIPBOARD_RESOURCES_COMPLETE) {
            if (event.request_id != request)
                return 12;
            break;
        }
        if (event.kind == NK_EVENT_NONE)
            return 3;
        nk_event_release(&event);
        event.struct_size = sizeof(event);
    }
    if (event.kind != NK_EVENT_CLIPBOARD_RESOURCES_COMPLETE || event.request_id != request)
        return 4;
    nk_resource_view first{};
    first.struct_size = sizeof(first);
    nk_resource_view second{};
    second.struct_size = sizeof(second);
    const auto first_decoded = nk_resource_event_item(&event, 0, &first);
    const auto second_decoded = nk_resource_event_item(&event, 1, &second);
    nk_resource_view extra{};
    extra.struct_size = sizeof(extra);
    const bool exactly_two = nk_resource_event_item(&event, 2, &extra) ==
                             NK_ERROR_INVALID_ARGUMENT;
    const bool uris_match = first_decoded == NK_OK && second_decoded == NK_OK &&
                            first.uri_length == std::strlen(input[0].uri) &&
                            std::memcmp(first.uri, input[0].uri, first.uri_length) == 0 &&
                            second.uri_length == std::strlen(input[1].uri) &&
                            std::memcmp(second.uri, input[1].uri, second.uri_length) == 0;
    const bool mimes_match = first.mime_type_length == 10 &&
                             std::memcmp(first.mime_type, "text/plain", 10) == 0 &&
                             second.mime_type_length == 9 &&
                             std::memcmp(second.mime_type, "image/png", 9) == 0;
    constexpr char first_name[] = "provided-clipboard-one";
    constexpr char second_name[] = "provided-clipboard-two";
    const bool names_match = first.display_name_length == sizeof(first_name) - 1 &&
                             std::memcmp(first.display_name, first_name, sizeof(first_name) - 1) ==
                                 0 &&
                             second.display_name_length == sizeof(second_name) - 1 &&
                             std::memcmp(second.display_name, second_name,
                                         sizeof(second_name) - 1) == 0;
    const bool flags_match = (first.flags & NK_RESOURCE_READABLE) != 0 &&
                             (second.flags & NK_RESOURCE_READABLE) != 0;
    nk_event_release(&event);
    return first_decoded != NK_OK || second_decoded != NK_OK ? 5
           : !exactly_two                                  ? 6
           : !uris_match                                   ? 7
           : !mimes_match                                  ? 8
           : !names_match                                  ? 9
           : !flags_match                                  ? 10
                                                           : 0;
}

extern "C" JNIEXPORT jint JNICALL
Java_io_nativekit_consumer_MainActivity_nativeResourceStreamProbe(JNIEnv *, jclass) {
    nk_resource resource{};
    resource.struct_size = sizeof(resource);
    resource.uri = "content://io.nativekit.consumer.resources/probe";
    nk_handle stream = NK_INVALID_HANDLE;
    const uint32_t mode = NK_RESOURCE_OPEN_READ | NK_RESOURCE_OPEN_WRITE |
                          NK_RESOURCE_OPEN_CREATE | NK_RESOURCE_OPEN_TRUNCATE;
    if (nk_resource_open(&resource, mode, &stream) != NK_OK)
        return 1;
    const char message[] = "NativeKit content stream";
    uint64_t written = 0;
    if (nk_resource_write(stream, message, sizeof(message) - 1, &written) != NK_OK ||
        written != sizeof(message) - 1) {
        nk_resource_close(stream);
        return 2;
    }
    uint64_t position = 0;
    if (nk_resource_seek(stream, 0, NK_SEEK_START, &position) != NK_OK || position != 0) {
        nk_resource_close(stream);
        return 3;
    }
    char result[sizeof(message)]{};
    uint64_t read = 0;
    if (nk_resource_read(stream, result, sizeof(message) - 1, &read) != NK_OK ||
        read != sizeof(message) - 1 || std::memcmp(result, message, read) != 0) {
        nk_resource_close(stream);
        return 4;
    }
    nk_resource_stream_info info{};
    info.struct_size = sizeof(info);
    if (nk_resource_stream_info_get(stream, &info) != NK_OK ||
        !(info.flags & NK_RESOURCE_STREAM_SEEKABLE) ||
        !(info.flags & NK_RESOURCE_STREAM_SIZE_KNOWN) || info.size != sizeof(message) - 1) {
        nk_resource_close(stream);
        return 5;
    }
    if (nk_resource_close(stream) != NK_OK || nk_resource_close(stream) != NK_ERROR_INVALID_HANDLE)
        return 6;
    return 0;
}

extern "C" JNIEXPORT jint JNICALL
Java_io_nativekit_consumer_MainActivity_nativeWebViewHistoryProbe(JNIEnv *, jclass,
                                                                  jlong webview) {
    uint32_t can_go_back = 1;
    uint32_t can_go_forward = 1;
    const auto handle = static_cast<nk_handle>(webview);
    if (nk_webview_can_go_back(handle, &can_go_back) != NK_OK || can_go_back != 0)
        return 1;
    if (nk_webview_can_go_forward(handle, &can_go_forward) != NK_OK || can_go_forward != 0)
        return 2;
    if (nk_webview_reload(handle) != NK_OK)
        return 3;
    if (nk_webview_stop(handle) != NK_OK)
        return 4;
    return 0;
}

extern "C" JNIEXPORT jint JNICALL
Java_io_nativekit_consumer_MainActivity_nativePersistedResourceProbe(JNIEnv *, jclass) {
    nk_resource resource{};
    resource.struct_size = sizeof(resource);
    resource.uri = "content://io.nativekit.consumer.resources/not-persisted";
    uint32_t flags = UINT32_MAX;
    if (nk_resource_get_persisted_access(&resource, &flags) != NK_OK || flags != 0)
        return 1;
    flags = UINT32_MAX;
    if (nk_resource_set_persisted_access(&resource, 0, &flags) != NK_OK || flags != 0)
        return 2;
    if (nk_resource_set_persisted_access(&resource, NK_RESOURCE_PERSISTED, &flags) !=
        NK_ERROR_INVALID_ARGUMENT)
        return 3;
    return 0;
}

namespace {
bool poll_kind(nk_event_kind kind, nk_event &event) {
    event.struct_size = sizeof(event);
    for (int attempt = 0; attempt < 64; ++attempt) {
        if (nk_poll_event(&event) != NK_OK)
            return false;
        if (event.kind == kind)
            return true;
        if (event.kind == NK_EVENT_NONE)
            return false;
        nk_event_release(&event);
        event.struct_size = sizeof(event);
    }
    return false;
}
} // namespace

extern "C" JNIEXPORT jint JNICALL
Java_io_nativekit_consumer_MainActivity_nativeIncomingShareProbe(JNIEnv *, jclass) {
    nk_event event{};
    if (!poll_kind(NK_EVENT_SHARE_RECEIVED, event))
        return 1;
    const char *text = nullptr;
    const char *subject = nullptr;
    uint32_t text_length = 0;
    uint32_t subject_length = 0;
    if (nk_share_event_text(&event, &text, &text_length) != NK_OK || text_length != 11 ||
        std::memcmp(text, "shared text", text_length) != 0 ||
        nk_share_event_subject(&event, &subject, &subject_length) != NK_OK ||
        subject_length != 14 || std::memcmp(subject, "shared subject", subject_length) != 0) {
        nk_event_release(&event);
        return 2;
    }
    for (uint32_t index = 0; index < 2; ++index) {
        nk_resource_view resource{};
        resource.struct_size = sizeof(resource);
        if (nk_resource_event_item(&event, index, &resource) != NK_OK ||
            !(resource.flags & NK_RESOURCE_READABLE) || resource.mime_type_length != 24 ||
            std::memcmp(resource.mime_type, "application/octet-stream", 24) != 0 ||
            resource.display_name_length != 3 ||
            std::memcmp(resource.display_name, index == 0 ? "one" : "two", 3) != 0) {
            nk_event_release(&event);
            return 3;
        }
    }
    nk_resource_view extra{};
    extra.struct_size = sizeof(extra);
    const bool exactly_two = nk_resource_event_item(&event, 2, &extra) == NK_ERROR_INVALID_ARGUMENT;
    nk_event_release(&event);
    return exactly_two ? 0 : 4;
}

extern "C" JNIEXPORT jint JNICALL
Java_io_nativekit_consumer_MainActivity_nativeIncomingViewProbe(JNIEnv *, jclass) {
    nk_event event{};
    if (!poll_kind(NK_EVENT_RESOURCE_OPENED, event))
        return 1;
    nk_resource_view resource{};
    resource.struct_size = sizeof(resource);
    const char expected[] = "content://io.nativekit.consumer.resources/viewed";
    const auto decoded = nk_resource_event_item(&event, 0, &resource);
    const bool matches = decoded == NK_OK && resource.uri_length == sizeof(expected) - 1 &&
                         std::memcmp(resource.uri, expected, sizeof(expected) - 1) == 0 &&
                         (resource.flags & NK_RESOURCE_READABLE) &&
                         resource.mime_type_length == 24 &&
                         std::memcmp(resource.mime_type, "application/octet-stream", 24) == 0 &&
                         resource.display_name_length == 6 &&
                         std::memcmp(resource.display_name, "viewed", 6) == 0;
    nk_event_release(&event);
    return matches ? 0 : 2;
}
