#include "nativekit.h"
#include "nativekit_window.h"

#include <assert.h>

int main(void) {
    nk_init_options init = {0};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    assert(nk_init(&init) == NK_OK);

    nk_window_options options = {0};
    options.struct_size = sizeof(options);
    options.flags = NK_WINDOW_HIDDEN;
    options.width = 320;
    options.height = 240;
    options.title = "NativeKit native-window runtime test";
    nk_window window = NK_INVALID_HANDLE;
    assert(nk_window_create(&options, &window) == NK_OK);

    nk_native_window native = {0};
    native.struct_size = sizeof(native);
    assert(nk_window_get_native(window, &native) == NK_OK);
    assert((native.kind == NK_NATIVE_WINDOW_X11 || native.kind == NK_NATIVE_WINDOW_WAYLAND) &&
           native.display != 0 && native.window != 0 && native.flags == 0);

    nk_window wrapped = NK_INVALID_HANDLE;
    assert(nk_window_wrap_native(&native, &wrapped) == NK_OK);
    assert(wrapped != NK_INVALID_HANDLE);

    nk_native_window wrapped_native = {0};
    wrapped_native.struct_size = sizeof(wrapped_native);
    assert(nk_window_get_native(wrapped, &wrapped_native) == NK_OK);
    assert(wrapped_native.kind == native.kind);
    assert(wrapped_native.display == native.display);
    assert(wrapped_native.window == native.window);
    if (native.kind == NK_NATIVE_WINDOW_WAYLAND)
        assert(nk_window_show(wrapped, 1) == NK_ERROR_UNSUPPORTED);

    assert(nk_window_destroy(wrapped) == NK_OK);
    assert(nk_window_get_native(window, &native) == NK_OK);
    assert(native.window != 0);
    assert(nk_window_destroy(window) == NK_OK);
    nk_shutdown();
    return 0;
}
