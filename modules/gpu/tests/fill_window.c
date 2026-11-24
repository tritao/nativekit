#include "nativekit_gpu.h"
#include "nativekit_time.h"
#include "nativekit_window.h"

#include <stdio.h>

static int matches_window(nk_window window, nk_surface surface, int width, int height) {
    int32_t w = 0, h = 0, window_w = 0, window_h = 0, surface_w = 0, surface_h = 0;
    for (int attempt = 0; attempt < 100; ++attempt) {
        nk_event event = {0};
        event.struct_size = sizeof(event);
        if (nk_poll_event(&event) != NK_OK)
            return 0;
        nk_event_release(&event);
        if (nk_window_get_size(window, &w, &h) == NK_OK && w == width && h == height &&
            nk_window_get_framebuffer_size(window, &window_w, &window_h) == NK_OK &&
            nk_surface_get_framebuffer_size(surface, &surface_w, &surface_h) == NK_OK &&
            window_w > 0 && window_h > 0 && window_w == surface_w && window_h == surface_h)
            return 1;
        if (nk_wait_events_timeout(0.02) != NK_OK)
            return 0;
    }
    fprintf(stderr, "Expected %dx%d; window %dx%d, framebuffer %dx%d, surface %dx%d\n",
            width, height, w, h, window_w, window_h, surface_w, surface_h);
    return 0;
}

int main(void) {
    nk_init_options init = {0};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    if (nk_init(&init) != NK_OK)
        return 1;

    nk_window window = NK_INVALID_HANDLE;
    nk_surface surface = NK_INVALID_HANDLE;
    nk_window_options window_options = {0};
    window_options.struct_size = sizeof(window_options);
    window_options.title = "NativeKit window-filling surface";
    window_options.flags = NK_WINDOW_RESIZABLE;
    window_options.width = 240;
    window_options.height = 180;

    nk_surface_options surface_options = {0};
    surface_options.struct_size = sizeof(surface_options);
    surface_options.api = nkgpu_default_graphics_api();
    surface_options.flags = NK_SURFACE_FILL_WINDOW;
    surface_options.x = 9;
    surface_options.y = 11;
    surface_options.width = 64;
    surface_options.height = 64;
    if (surface_options.api == NK_GRAPHICS_OPENGL) {
        surface_options.major_version = 3;
        surface_options.minor_version = 3;
        surface_options.flags |= NK_SURFACE_FORWARD_COMPATIBLE;
    }

    const int passed = nk_window_create(&window_options, &window) == NK_OK &&
                       nk_surface_create(window, &surface_options, &surface) == NK_OK &&
                       matches_window(window, surface, 240, 180) &&
                       nk_window_set_bounds(window, 0, 0, 320, 230) == NK_OK &&
                       matches_window(window, surface, 320, 230) &&
                       nk_window_set_bounds(window, 0, 0, 200, 150) == NK_OK &&
                       matches_window(window, surface, 200, 150);
    if (!passed)
        fprintf(stderr, "%s\n", nk_last_error());

    if (surface != NK_INVALID_HANDLE)
        nk_surface_destroy(surface);
    if (window != NK_INVALID_HANDLE)
        nk_window_destroy(window);
    nk_shutdown();
    return passed ? 0 : 1;
}
