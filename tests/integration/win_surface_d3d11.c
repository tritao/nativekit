#include "nativekit_graphics.h"
#include "nativekit_time.h"
#include "nativekit_window.h"

#include <assert.h>
#include <stdint.h>

static void acquire_frame(nk_window window, nk_surface surface) {
    assert(nk_window_activate(window) == NK_OK);
    nk_result result = NK_ERROR_INVALID_REQUEST;
    for (uint32_t attempt = 0; attempt < 500 && result != NK_OK; ++attempt) {
        nk_event event = {0};
        event.struct_size = sizeof(event);
        assert(nk_poll_event(&event) == NK_OK);
        nk_event_release(&event);
        result = nk_surface_make_current(surface);
        if (result == NK_ERROR_INVALID_REQUEST)
            assert(nk_wait_events_timeout(0.01) == NK_OK);
        else
            assert(result == NK_OK);
    }
    assert(result == NK_OK);
}

int main(void) {
    nk_init_options init = {0};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    assert(nk_init(&init) == NK_OK);

    nk_window_options window_options = {0};
    window_options.struct_size = sizeof(window_options);
    window_options.width = 320;
    window_options.height = 240;
    window_options.flags = NK_WINDOW_RESIZABLE;
    nk_window window = NK_INVALID_HANDLE;
    assert(nk_window_create(&window_options, &window) == NK_OK);

    nk_surface_options surface_options = {0};
    surface_options.struct_size = sizeof(surface_options);
    surface_options.flags = NK_SURFACE_DEPTH | NK_SURFACE_STENCIL;
    surface_options.api = NK_GRAPHICS_D3D11;
    surface_options.width = 160;
    surface_options.height = 120;
    nk_surface surface = NK_INVALID_HANDLE;
    assert(nk_surface_create(window, &surface_options, &surface) == NK_OK);

    acquire_frame(window, surface);
    nk_surface_frame_target target = {0};
    target.struct_size = sizeof(target);
    assert(nk_surface_get_frame_target(surface, &target) == NK_OK);
    assert(target.api == NK_GRAPHICS_D3D11);
    assert(target.width > 0 && target.height > 0);
    assert(target.device.id != 0);
    assert(target.native_target != 0);
    assert(target.native_device != 0);
    assert(target.native_context != 0);
    assert(target.native_depth_stencil_target != 0);
    assert(target.native_present_target != 0);
    assert(nk_surface_present(surface) == NK_OK);

    assert(nk_surface_set_bounds(surface, 12, 16, 200, 100) == NK_OK);
    acquire_frame(window, surface);
    target = (nk_surface_frame_target){0};
    target.struct_size = sizeof(target);
    assert(nk_surface_get_frame_target(surface, &target) == NK_OK);
    assert(target.width > 0 && target.height > 0);
    assert(nk_surface_present(surface) == NK_OK);

    assert(nk_surface_destroy(surface) == NK_OK);
    assert(nk_window_destroy(window) == NK_OK);
    nk_shutdown();
    return 0;
}
