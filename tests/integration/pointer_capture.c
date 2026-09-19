#include "nativekit.h"
#include "nativekit_input.h"
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
    options.title = "NativeKit pointer-capture runtime test";
    nk_window window = NK_INVALID_HANDLE;
    assert(nk_window_create(&options, &window) == NK_OK);

    nk_cursor_mode mode = NK_CURSOR_MODE_DISABLED;
    assert(nk_window_get_cursor_mode(window, &mode) == NK_OK);
    assert(mode == NK_CURSOR_MODE_NORMAL);
    assert(nk_window_set_cursor_mode(window, (nk_cursor_mode)99) == NK_ERROR_INVALID_ARGUMENT);

    const nk_result shown = nk_window_show(window, 1);
    assert(shown == NK_OK || shown == NK_ERROR_UNSUPPORTED);
    if (shown == NK_ERROR_UNSUPPORTED) {
        assert(nk_window_destroy(window) == NK_OK);
        nk_shutdown();
        return 77;
    }

    const nk_result captured = nk_window_set_cursor_mode(window, NK_CURSOR_MODE_CAPTURED);
    if (captured == NK_ERROR_UNSUPPORTED) {
        assert(nk_window_destroy(window) == NK_OK);
        nk_shutdown();
        return 77;
    }
    assert(captured == NK_OK);
    assert(nk_window_get_cursor_mode(window, &mode) == NK_OK);
    assert(mode == NK_CURSOR_MODE_CAPTURED);

    assert(nk_window_set_cursor_mode(window, NK_CURSOR_MODE_NORMAL) == NK_OK);
    assert(nk_window_get_cursor_mode(window, &mode) == NK_OK);
    assert(mode == NK_CURSOR_MODE_NORMAL);
    assert(nk_window_destroy(window) == NK_OK);
    nk_shutdown();
    return 0;
}
