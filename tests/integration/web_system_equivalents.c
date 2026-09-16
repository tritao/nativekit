#include "nativekit.h"
#include "nativekit_input.h"
#include "nativekit_joystick.h"
#include "nativekit_resource.h"
#include "nativekit_system.h"
#include "nativekit_window.h"

#include <assert.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#include <stdlib.h>

#ifdef NDEBUG
#define NK_TEST_ASSERT(condition)                                                                  \
    do {                                                                                           \
        if (!(condition))                                                                          \
            abort();                                                                               \
    } while (0)
#else
#define NK_TEST_ASSERT(condition) assert(condition)
#endif

static void test_window_styling(void) {
    nk_window_options options = {0};
    options.struct_size = sizeof(options);
    options.flags = NK_WINDOW_HIDDEN | NK_WINDOW_RESIZABLE;
    options.width = 320;
    options.height = 240;
    nk_window window = NK_INVALID_HANDLE;
    NK_TEST_ASSERT(nk_window_create(&options, &window) == NK_OK);

    nk_window_size_limits limits = {0};
    limits.struct_size = sizeof(limits);
    limits.min_width = 320;
    limits.min_height = 240;
    limits.max_width = 1280;
    limits.max_height = 960;
    NK_TEST_ASSERT(nk_window_set_size_limits(window, &limits) == NK_OK);
    NK_TEST_ASSERT(nk_window_set_aspect_ratio(window, 4, 3) == NK_OK);
    NK_TEST_ASSERT(nk_window_set_resizable(window, 0) == NK_OK);
    NK_TEST_ASSERT(nk_window_set_opacity(window, 0.75f) == NK_OK);
    NK_TEST_ASSERT(nk_window_set_mouse_passthrough(window, 1) == NK_OK);

    const unsigned char cursor_pixels[16] = {255, 0, 0, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255};
    nk_cursor_image cursor_image = {0};
    cursor_image.struct_size = sizeof(cursor_image);
    cursor_image.width = 2;
    cursor_image.height = 2;
    cursor_image.stride = 8;
    cursor_image.hotspot_x = 1;
    cursor_image.hotspot_y = 1;
    cursor_image.rgba = cursor_pixels;
    nk_cursor cursor = NK_INVALID_HANDLE;
    NK_TEST_ASSERT(nk_cursor_create_custom(&cursor_image, &cursor) == NK_OK);
    NK_TEST_ASSERT(nk_window_set_cursor(window, cursor) == NK_OK);

    // clang-format off
    NK_TEST_ASSERT(EM_ASM_INT({
               const canvas = document.querySelector("#canvas");
               return canvas && canvas.style.minWidth == "320px" && canvas.style.minHeight ==
                      "240px" && canvas.style.maxWidth == "1280px" && canvas.style.maxHeight ==
                      "960px" && canvas.style.aspectRatio == "4 / 3" && canvas.style.resize ==
               "none" && canvas.style.opacity == "0.75" && canvas.style.pointerEvents ==
                      "none" && canvas.style.cursor.indexOf("data:image/svg+xml;base64,") >= 0 ? 1 : 0;
           }) == 1);
    // clang-format on

    nk_bool hovered = 1;
    NK_TEST_ASSERT(nk_window_get_hovered(window, &hovered) == NK_OK);
    NK_TEST_ASSERT(hovered == 0);

    // clang-format off
    EM_ASM({
        Module._nkNativeKitOriginalShowOpenFilePicker = window.showOpenFilePicker;
        window.showOpenFilePicker = () => Promise.resolve([]);
    });
    // clang-format on
    nk_file_dialog_options dialog_options = {0};
    dialog_options.struct_size = sizeof(dialog_options);
    dialog_options.title = "NativeKit Web resource cancellation";
    nk_request_id request = NK_INVALID_REQUEST_ID;
    NK_TEST_ASSERT(nk_dialog_open_resource(window, &dialog_options, &request) == NK_OK);
    NK_TEST_ASSERT(nk_dialog_cancel(request) == NK_OK);
    nk_event event = {0};
    event.struct_size = sizeof(event);
    int dialog_seen = 0;
    for (int attempt = 0; attempt < 8 && !dialog_seen; ++attempt) {
        NK_TEST_ASSERT(nk_poll_event(&event) == NK_OK);
        if (event.kind == NK_EVENT_DIALOG_RESOURCES_COMPLETE && event.request_id == request) {
            NK_TEST_ASSERT(event.result == NK_OK);
            NK_TEST_ASSERT(event.flags == NK_DIALOG_OPEN_RESOURCE);
            NK_TEST_ASSERT(event.data_size >= sizeof(nk_resource_list));
            const nk_resource_list *resources = (const nk_resource_list *)event.data;
            NK_TEST_ASSERT(resources->accepted == 0);
            NK_TEST_ASSERT(resources->item_count == 0);
            dialog_seen = 1;
        }
        nk_event_release(&event);
        event.struct_size = sizeof(event);
    }
    NK_TEST_ASSERT(dialog_seen);
    // clang-format off
    EM_ASM({
        window.showOpenFilePicker = Module._nkNativeKitOriginalShowOpenFilePicker;
        delete Module._nkNativeKitOriginalShowOpenFilePicker;
    });
    // clang-format on
    NK_TEST_ASSERT(nk_window_set_mouse_passthrough(window, 0) == NK_OK);
    NK_TEST_ASSERT(nk_cursor_destroy(cursor) == NK_OK);
    NK_TEST_ASSERT(nk_window_set_cursor(window, NK_INVALID_HANDLE) == NK_OK);
    NK_TEST_ASSERT(nk_window_set_resizable(window, 1) == NK_OK);
    NK_TEST_ASSERT(nk_window_set_aspect_ratio(window, 0, 0) == NK_OK);
    NK_TEST_ASSERT(nk_window_set_size_limits(
                       window, &(nk_window_size_limits){
                                   sizeof(nk_window_size_limits), 0, 0, 0, 0, {0, 0}}) == NK_OK);
    NK_TEST_ASSERT(nk_window_destroy(window) == NK_OK);
}

static void test_writable_resource_stream(void) {
    // clang-format off
    EM_ASM({
        const uri = "nativekit-file-handle://web-smoke";
        Module._nkNativeKitResourceHandles = {};
        Module._nkNativeKitResourceHandles[uri] = {
            createWritable: () => Promise.resolve({
                write: bytes => {
                    Module._nkWebSmokeResourceBytes = Array.from(bytes);
                    return Promise.resolve();
                },
                close: () => {
                    const bytes = Module._nkWebSmokeResourceBytes || [];
                    const valid = bytes.length == 3 && bytes[0] == 0x4e && bytes[1] ==
                                  0x4b && bytes[2] == 0x21;
                    document.documentElement.dataset.nativekitResourceWrite =
                        valid ? "verified" : "failed";
                    document.documentElement.dataset.nativekitSystemResult =
                        valid ? "passed" : "failed";
                    return Promise.resolve();
                }
            })
};
});
// clang-format on

nk_resource resource = {0};
resource.struct_size = sizeof(resource);
resource.flags = NK_RESOURCE_WRITABLE;
resource.uri = "nativekit-file-handle://web-smoke";
nk_resource_stream stream = NK_INVALID_HANDLE;
NK_TEST_ASSERT(nk_resource_open(&resource,
                                NK_RESOURCE_OPEN_WRITE | NK_RESOURCE_OPEN_CREATE |
                                    NK_RESOURCE_OPEN_TRUNCATE,
                                &stream) == NK_OK);
const unsigned char bytes[] = {'N', 'K', '!'};
uint64_t written = 0;
NK_TEST_ASSERT(nk_resource_write(stream, bytes, sizeof(bytes), &written) == NK_OK);
NK_TEST_ASSERT(written == sizeof(bytes));
nk_resource_stream_info info = {0};
info.struct_size = sizeof(info);
NK_TEST_ASSERT(nk_resource_stream_info_get(stream, &info) == NK_OK);
NK_TEST_ASSERT((info.flags & NK_RESOURCE_STREAM_WRITABLE) != 0);
NK_TEST_ASSERT((info.flags & NK_RESOURCE_STREAM_SEEKABLE) != 0);
NK_TEST_ASSERT(info.size == sizeof(bytes));
NK_TEST_ASSERT(nk_resource_close(stream) == NK_OK);

/* Web resource writes are flushed by the backend event pump. */
nk_event event = {0};
event.struct_size = sizeof(event);
NK_TEST_ASSERT(nk_poll_event(&event) == NK_OK);
nk_event_release(&event);

/* The File System Access write and close are promise-based. */
// clang-format off
EM_ASM({
    if (typeof setTimeout == "function") {
        let attempts = 0;
        const check = () => {
            if (document.documentElement.dataset.nativekitResourceWrite == "verified")
                return;
            if (++attempts >= 100) {
                document.documentElement.dataset.nativekitSystemResult = "failed";
                throw new Error("NativeKit Web resource write was not flushed");
            }
            setTimeout(check, 10);
        };
        setTimeout(check, 0);
    }
});
// clang-format on
}
#endif

int main(void) {
    nk_init_options init = {0};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    NK_TEST_ASSERT(nk_init(&init) == NK_OK);

    const nk_capabilities capabilities = nk_get_capabilities();
    NK_TEST_ASSERT((capabilities & NK_CAP_SHELL) != 0);
    NK_TEST_ASSERT((capabilities & NK_CAP_SYSTEM_APPEARANCE) != 0);
    NK_TEST_ASSERT((capabilities & NK_CAP_NOTIFICATION) != 0);
    NK_TEST_ASSERT((capabilities & NK_CAP_JOYSTICK) != 0);
    NK_TEST_ASSERT((capabilities & NK_CAP_RESOURCE_IO) != 0);
    NK_TEST_ASSERT((capabilities & NK_CAP_WINDOW_STYLING) != 0);

    nk_system_appearance appearance = {0};
    appearance.struct_size = sizeof(appearance);
    NK_TEST_ASSERT(nk_system_get_appearance(&appearance) == NK_OK);
    NK_TEST_ASSERT(appearance.color_scheme == NK_COLOR_SCHEME_LIGHT ||
                   appearance.color_scheme == NK_COLOR_SCHEME_DARK);

    uint32_t joystick_count = 0;
    const nk_result joystick_result = nk_joystick_list(NULL, &joystick_count);
    NK_TEST_ASSERT(joystick_result == NK_OK || joystick_result == NK_ERROR_BUFFER_TOO_SMALL);

    NK_TEST_ASSERT(nk_shell_open_url("not a URI") == NK_ERROR_INVALID_ARGUMENT);
#ifdef __EMSCRIPTEN__
    test_window_styling();
    test_writable_resource_stream();
#endif
    nk_shutdown();
    return 0;
}
