#include "nativekit.h"
#include "nativekit_graphics.h"
#include "nativekit_window.h"

#include <assert.h>
#include <unistd.h>

static void pump_events(int iterations) {
    for (int iteration = 0; iteration < iterations; ++iteration) {
        nk_event event = {0};
        event.struct_size = sizeof(event);
        assert(nk_poll_event(&event) == NK_OK);
        nk_event_release(&event);
        usleep(2000);
    }
}

int main(void) {
    nk_init_options init = {0};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    assert(nk_init(&init) == NK_OK);

    nk_window_options window_options = {0};
    window_options.struct_size = sizeof(window_options);
    window_options.width = 256;
    window_options.height = 192;
    window_options.title = "NativeKit make-current probe";
    nk_window window = NK_INVALID_HANDLE;
    assert(nk_window_create(&window_options, &window) == NK_OK);

    nk_surface_options surface_options = {0};
    surface_options.struct_size = sizeof(surface_options);
    surface_options.api = NK_GRAPHICS_OPENGL;
    surface_options.width = 256;
    surface_options.height = 192;
    nk_surface surface = NK_INVALID_HANDLE;
    assert(nk_surface_create(window, &surface_options, &surface) == NK_OK);
    pump_events(40);

    const nk_result first = nk_surface_make_current(surface);
    const nk_result second = nk_surface_make_current(surface);
    const nk_result presented = nk_surface_present(surface);
    const nk_result after_present = nk_surface_make_current(surface);
    /* Preparing a frame is idempotent, and a present leaves the surface usable. */
    assert(first == NK_OK);
    assert(second == NK_OK);
    assert(presented == NK_OK);
    assert(after_present == NK_OK);

    assert(nk_surface_destroy(surface) == NK_OK);
    assert(nk_window_destroy(window) == NK_OK);
    nk_shutdown();
    return first == NK_OK ? 0 : 1;
}
