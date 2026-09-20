#include "nativekit.h"
#include "nativekit_graphics.h"
#include "nativekit_window.h"

#include <assert.h>

int main(void) {
    nk_init_options init = {0};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    assert(nk_init(&init) == NK_OK);

    nk_window_options window_options = {0};
    window_options.struct_size = sizeof(window_options);
    window_options.width = 320;
    window_options.height = 240;
    nk_window window = NK_INVALID_HANDLE;
    assert(nk_window_create(&window_options, &window) == NK_OK);

    nk_surface_options surface_options = {0};
    surface_options.struct_size = sizeof(surface_options);
    surface_options.api = NK_GRAPHICS_OPENGL;
    surface_options.width = 320;
    surface_options.height = 240;
    nk_surface surface = NK_INVALID_HANDLE;
    assert(nk_surface_create(window, &surface_options, &surface) == NK_OK);

    nk_surface_frame frame = NK_INVALID_HANDLE;
    nk_surface_frame_target target = {0};
    target.struct_size = sizeof(target);
    assert(nk_surface_acquire_frame(surface, &frame, &target) == NK_OK);
    assert(frame != NK_INVALID_HANDLE);
    assert(target.native_target != 0);
    assert(target.native_context != 0);
    assert(target.native_present_target != 0);
    assert(nk_surface_cancel_frame(frame) == NK_OK);

    nk_surface second_surface = NK_INVALID_HANDLE;
    assert(nk_surface_create(window, &surface_options, &second_surface) == NK_OK);
    nk_surface second_frame = NK_INVALID_HANDLE;
    nk_surface_frame_target second_target = {0};
    second_target.struct_size = sizeof(second_target);
    assert(nk_surface_acquire_frame(second_surface, &second_frame, &second_target) == NK_OK);
    assert(second_target.native_context == target.native_context);
    assert(second_target.native_target != 0);
    assert(nk_surface_cancel_frame(second_frame) == NK_OK);

    assert(nk_surface_destroy(second_surface) == NK_OK);
    assert(nk_surface_destroy(surface) == NK_OK);
    assert(nk_window_destroy(window) == NK_OK);
    nk_shutdown();
    return 0;
}
