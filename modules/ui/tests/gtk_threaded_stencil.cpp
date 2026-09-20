#include "nativekit.h"
#include "nativekit_graphics.h"
#include "nativekit_window.h"

#include <chrono>
#include <cstdio>
#include <thread>

int main() {
    nk_init_options init{};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    if (nk_init(&init) != NK_OK)
        return 1;

    nk_window_options window_options{};
    window_options.struct_size = sizeof(window_options);
    window_options.width = 128;
    window_options.height = 96;
    window_options.title = "NativeKit GTK threaded stencil";
    nk_window window = NK_INVALID_HANDLE;
    if (nk_window_create(&window_options, &window) != NK_OK)
        return 2;

    nk_surface_options surface_options{};
    surface_options.struct_size = sizeof(surface_options);
    surface_options.flags = NK_SURFACE_FORWARD_COMPATIBLE | NK_SURFACE_STENCIL;
    surface_options.api = NK_GRAPHICS_OPENGL;
    surface_options.major_version = 3;
    surface_options.minor_version = 3;
    surface_options.width = window_options.width;
    surface_options.height = window_options.height;
    nk_surface surface = NK_INVALID_HANDLE;
    if (nk_surface_create(window, &surface_options, &surface) != NK_OK)
        return 3;

    bool ready = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!ready && std::chrono::steady_clock::now() < deadline) {
        nk_event event{};
        event.struct_size = sizeof(event);
        if (nk_poll_event(&event) != NK_OK)
            return 4;
        ready = event.kind == NK_EVENT_SURFACE_READY && event.source == surface;
        nk_event_release(&event);
        if (!ready)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!ready)
        return 5;

    nk_surface_frame_target target{};
    target.struct_size = sizeof(target);
    if (nk_surface_make_current(surface) != NK_OK ||
        nk_surface_get_frame_target(surface, &target) != NK_OK ||
        target.api != NK_GRAPHICS_OPENGL || !target.native_target ||
        !target.native_depth_stencil_target)
        return 6;

    nk_surface_frame frame = NK_INVALID_HANDLE;
    nk_surface_frame_target acquired{};
    acquired.struct_size = sizeof(acquired);
    if (nk_surface_acquire_frame(surface, &frame, &acquired) != NK_OK || acquired.frame != frame ||
        !acquired.native_target || !acquired.native_depth_stencil_target ||
        nk_surface_cancel_frame(frame) != NK_OK)
        return 7;

    if (nk_surface_destroy(surface) != NK_OK || nk_window_destroy(window) != NK_OK) {
        nk_shutdown();
        return 8;
    }
    nk_shutdown();
    return 0;
}
