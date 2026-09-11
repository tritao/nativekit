#include "nativekit.h"
#include "nativekit_graphics.h"
#include "nativekit_window.h"

#include "render/render_backend_factory.h"

#include <array>
#include <memory>

namespace {

struct BackendSurface {
    nk_handle window = NK_INVALID_HANDLE;
    nk_handle surface = NK_INVALID_HANDLE;
    nk_graphics_api api = 0;
    std::unique_ptr<nkui::RenderBackend> backend;
};

void destroy_backend_surface(BackendSurface &item) {
    if (item.backend) {
        if (item.surface != NK_INVALID_HANDLE)
            nk_surface_make_current(item.surface);
        item.backend.reset();
    }
    if (item.surface != NK_INVALID_HANDLE) {
        nk_surface_destroy(item.surface);
        item.surface = NK_INVALID_HANDLE;
    }
    if (item.window != NK_INVALID_HANDLE) {
        nk_window_destroy(item.window);
        item.window = NK_INVALID_HANDLE;
    }
}

bool initialize_backend_surface(BackendSurface &item, const char *title) {
    nk_window_options window_options{};
    window_options.struct_size = sizeof(window_options);
    window_options.width = 160;
    window_options.height = 120;
    window_options.title = title;
    if (nk_window_create(&window_options, &item.window) != NK_OK)
        return false;

    nk_surface_options surface_options{};
    surface_options.struct_size = sizeof(surface_options);
    surface_options.flags = NK_SURFACE_FORWARD_COMPATIBLE | NK_SURFACE_STENCIL;
    surface_options.api = item.api;
    surface_options.major_version = 3;
    surface_options.minor_version = item.api == NK_GRAPHICS_OPENGL_ES ? 0 : 3;
    surface_options.width = window_options.width;
    surface_options.height = window_options.height;
    if (nk_surface_create(item.window, &surface_options, &item.surface) != NK_OK)
        return false;
    item.backend = nkui::create_render_backend(item.api);
    if (!item.backend || nk_surface_make_current(item.surface) != NK_OK)
        return false;
    return item.backend->initialize() && item.backend->valid();
}

bool render_frame(BackendSurface &item) {
    if (nk_surface_make_current(item.surface) != NK_OK)
        return false;
    nk_surface_frame_target target{};
    target.struct_size = sizeof(target);
    if (nk_surface_get_frame_target(item.surface, &target) != NK_OK ||
        target.api != item.api || target.width <= 0 || target.height <= 0)
        return false;
    if (!item.backend->begin_window_pass(target.width, target.height, target, true) ||
        !item.backend->end_frame())
        return false;
    return nk_surface_present(item.surface) == NK_OK;
}

} // namespace

int main() {
    nk_init_options init{};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    if (nk_init(&init) != NK_OK)
        return 1;

    std::array<BackendSurface, 2> surfaces{{
        {NK_INVALID_HANDLE, NK_INVALID_HANDLE, NK_GRAPHICS_OPENGL, nullptr},
        {NK_INVALID_HANDLE, NK_INVALID_HANDLE, NK_GRAPHICS_OPENGL_ES, nullptr},
    }};
    int result = 0;
    if (!initialize_backend_surface(surfaces[0], "NativeKit GL matrix smoke") ||
        !initialize_backend_surface(surfaces[1], "NativeKit GLES matrix smoke"))
        result = 2;

    for (int frame = 0; !result && frame < 3; ++frame) {
        if (!render_frame(surfaces[0]) || !render_frame(surfaces[1]))
            result = 3;
    }

    // Destroy each backend while its matching native context is current. This
    // catches accidental cross-backend resource destruction as well as setup.
    destroy_backend_surface(surfaces[1]);
    destroy_backend_surface(surfaces[0]);
    nk_shutdown();
    return result;
}
