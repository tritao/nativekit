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

    nk_surface resize_frame = NK_INVALID_HANDLE;
    nk_surface_frame_target resize_target = {0};
    resize_target.struct_size = sizeof(resize_target);
    assert(nk_surface_acquire_frame(surface, &resize_frame, &resize_target) == NK_OK);
    const int32_t original_width = resize_target.width;
    const int32_t original_height = resize_target.height;
    assert(nk_surface_set_bounds(surface, 0, 0, 400, 300) == NK_OK);
    assert(resize_target.width == original_width);
    assert(resize_target.height == original_height);
    assert(nk_surface_cancel_frame(resize_frame) == NK_OK);

    nk_surface resized_frame = NK_INVALID_HANDLE;
    nk_surface_frame_target resized_target = {0};
    resized_target.struct_size = sizeof(resized_target);
    assert(nk_surface_acquire_frame(surface, &resized_frame, &resized_target) == NK_OK);
    assert(resized_target.width != original_width || resized_target.height != original_height);
    assert(resized_target.native_context == target.native_context);
    assert(nk_surface_cancel_frame(resized_frame) == NK_OK);

    assert(nk_surface_destroy(second_surface) == NK_OK);

    nk_window second_window = NK_INVALID_HANDLE;
    assert(nk_window_create(&window_options, &second_window) == NK_OK);
    nk_surface independent_surface = NK_INVALID_HANDLE;
    assert(nk_surface_create(second_window, &surface_options, &independent_surface) == NK_OK);
    nk_surface independent_frame = NK_INVALID_HANDLE;
    nk_surface_frame_target independent_target = {0};
    independent_target.struct_size = sizeof(independent_target);
    assert(nk_surface_acquire_frame(independent_surface, &independent_frame, &independent_target) ==
           NK_OK);
    assert(independent_target.native_context != target.native_context);
    assert(nk_surface_cancel_frame(independent_frame) == NK_OK);
    assert(nk_surface_destroy(independent_surface) == NK_OK);
    assert(nk_window_destroy(second_window) == NK_OK);

    nk_surface root_frame = NK_INVALID_HANDLE;
    nk_surface_frame_target root_target = {0};
    root_target.struct_size = sizeof(root_target);
    assert(nk_surface_acquire_frame(surface, &root_frame, &root_target) == NK_OK);
    assert(root_target.native_context == target.native_context);
    assert(nk_surface_cancel_frame(root_frame) == NK_OK);

    assert(nk_surface_destroy(surface) == NK_OK);
    assert(nk_window_destroy(window) == NK_OK);
    nk_shutdown();
    return 0;
}
