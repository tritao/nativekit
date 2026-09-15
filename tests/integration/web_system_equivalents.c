#include "nativekit.h"
#include "nativekit_joystick.h"
#include "nativekit_resource.h"
#include "nativekit_system.h"
#include "nativekit_window.h"

#include <assert.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>

static void test_writable_resource_stream(void) {
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
                    const valid = bytes.length === 3 && bytes[0] === 0x4e &&
                                  bytes[1] === 0x4b && bytes[2] === 0x21;
                    document.documentElement.dataset.nativekitResourceWrite =
                        valid ? "verified" : "failed";
                    return Promise.resolve();
                }
            })
        };
    });

    nk_resource resource = {0};
    resource.struct_size = sizeof(resource);
    resource.flags = NK_RESOURCE_WRITABLE;
    resource.uri = "nativekit-file-handle://web-smoke";
    nk_resource_stream stream = NK_INVALID_HANDLE;
    assert(nk_resource_open(&resource,
                            NK_RESOURCE_OPEN_WRITE | NK_RESOURCE_OPEN_CREATE |
                                NK_RESOURCE_OPEN_TRUNCATE,
                            &stream) == NK_OK);
    const unsigned char bytes[] = {'N', 'K', '!'};
    uint64_t written = 0;
    assert(nk_resource_write(stream, bytes, sizeof(bytes), &written) == NK_OK);
    assert(written == sizeof(bytes));
    nk_resource_stream_info info = {0};
    info.struct_size = sizeof(info);
    assert(nk_resource_stream_info_get(stream, &info) == NK_OK);
    assert((info.flags & NK_RESOURCE_STREAM_WRITABLE) != 0);
    assert((info.flags & NK_RESOURCE_STREAM_SEEKABLE) != 0);
    assert(info.size == sizeof(bytes));
    assert(nk_resource_close(stream) == NK_OK);

    /* The File System Access write and close are promise-based. */
    EM_ASM({
        if (typeof setTimeout === "function")
            setTimeout(() => {
                if (document.documentElement.dataset.nativekitResourceWrite !== "verified")
                    throw new Error("NativeKit Web resource write was not flushed");
            }, 0);
    });
}
#endif

int main(void) {
    nk_init_options init = {0};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    assert(nk_init(&init) == NK_OK);

    const nk_capabilities capabilities = nk_get_capabilities();
    assert((capabilities & NK_CAP_SHELL) != 0);
    assert((capabilities & NK_CAP_SYSTEM_APPEARANCE) != 0);
    assert((capabilities & NK_CAP_NOTIFICATION) != 0);
    assert((capabilities & NK_CAP_JOYSTICK) != 0);
    assert((capabilities & NK_CAP_RESOURCE_IO) != 0);

    nk_system_appearance appearance = {0};
    appearance.struct_size = sizeof(appearance);
    assert(nk_system_get_appearance(&appearance) == NK_OK);
    assert(appearance.color_scheme == NK_COLOR_SCHEME_LIGHT ||
           appearance.color_scheme == NK_COLOR_SCHEME_DARK);

    uint32_t joystick_count = 0;
    const nk_result joystick_result = nk_joystick_list(NULL, &joystick_count);
    assert(joystick_result == NK_OK || joystick_result == NK_ERROR_BUFFER_TOO_SMALL);

    assert(nk_shell_open_url("not a URI") == NK_ERROR_INVALID_ARGUMENT);
#ifdef __EMSCRIPTEN__
    test_writable_resource_stream();
#endif
    nk_shutdown();
    return 0;
}
