#include "nativekit.h"
#include "nativekit_graphics.h"
#include "nativekit_sokol.h"
#include "nativekit_window.h"

#include "render/sokol_backend.h"

#include <array>
#include <memory>

namespace {

struct BackendSurface {
    nk_handle window = NK_INVALID_HANDLE;
    nk_handle surface = NK_INVALID_HANDLE;
    nk_graphics_api api = 0;
    nks_renderer graphics_renderer{};
    nks_render_target render_target{};
    nk_graphics_image image{};
    bool image_retained = false;
    std::unique_ptr<nkui::RenderBackend> backend;
};

void destroy_backend_surface(BackendSurface &item) {
    if (item.backend) {
        if (item.surface != NK_INVALID_HANDLE)
            nk_surface_make_current(item.surface);
        item.backend.reset();
    }
    if (item.image_retained) {
        nk_graphics_image_release(item.image);
        item.image_retained = false;
        item.image = {};
    }
    if (item.render_target.id && item.graphics_renderer.id) {
        nks_render_target_destroy(item.graphics_renderer, item.render_target);
        item.render_target = {};
    }
    if (item.graphics_renderer.id) {
        nks_renderer_destroy(item.graphics_renderer);
        item.graphics_renderer = {};
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
    if (nk_surface_make_current(item.surface) != NK_OK)
        return false;
    nk_surface_frame_target target{};
    target.struct_size = sizeof(target);
    if (nk_surface_get_frame_target(item.surface, &target) != NK_OK || target.api != item.api ||
        !target.device.id)
        return false;
    if (nks_renderer_create(item.surface, &item.graphics_renderer) != NKS_OK)
        return false;
    const nks_backend expected_backend = item.api == NK_GRAPHICS_OPENGL_ES
                                             ? NKS_BACKEND_GLES3
                                             : NKS_BACKEND_GLCORE;
    if (nks_query_backend(item.graphics_renderer) != expected_backend ||
        nks_render_target_create(item.graphics_renderer, 32, 32, 0, &item.render_target) != NKS_OK ||
        nks_begin_render_target(item.graphics_renderer, item.render_target, 1) != NKS_OK ||
        nks_render_target_destroy(item.graphics_renderer, item.render_target) !=
            NKS_ERROR_WRONG_STATE ||
        nks_end_render_target(item.graphics_renderer) != NKS_OK ||
        nks_begin_frame(item.graphics_renderer) != NKS_OK ||
        nks_render_target_destroy(item.graphics_renderer, item.render_target) !=
            NKS_ERROR_WRONG_STATE ||
        nks_end_frame(item.graphics_renderer) != NKS_OK ||
        nks_render_target_get_image(item.graphics_renderer, item.render_target, &item.image) !=
            NKS_OK)
        return false;
    if (nk_graphics_image_retain(item.image) != NK_OK)
        return false;
    item.image_retained = true;
    nk_graphics_image_info image_info{};
    image_info.struct_size = sizeof(image_info);
    if (nk_graphics_image_get_info(item.image, &image_info) != NK_OK ||
        image_info.api != item.api || image_info.device.id != target.device.id ||
        image_info.width != 32 || image_info.height != 32)
        return false;
    item.backend = nkui::create_render_backend(item.api, target.device);
    if (!item.backend)
        return false;
    return item.backend->initialize() && item.backend->valid();
}

bool render_frame(BackendSurface &item) {
    // Exercise the public adapter's window-frame path as well as the UI
    // compositor, alternating runtime selection between GLCore and GLES3.
    if (nks_begin_frame(item.graphics_renderer) != NKS_OK ||
        nks_end_frame(item.graphics_renderer) != NKS_OK)
        return false;
    if (nk_surface_make_current(item.surface) != NK_OK)
        return false;
    nk_surface_frame_target target{};
    target.struct_size = sizeof(target);
    if (nk_surface_get_frame_target(item.surface, &target) != NK_OK ||
        target.api != item.api || target.width <= 0 || target.height <= 0)
        return false;
    const float transform[6] = {1.0f, 0.0f, 0.0f, 1.0f, 8.0f, 8.0f};
    if (!item.backend->begin_window_pass(target.width, target.height, target, true) ||
        !item.backend->draw_graphics_image(item.image, 0.0f, 0.0f, 32.0f, 32.0f, transform, 1.0f) ||
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

    std::array<BackendSurface, 2> surfaces{};
    surfaces[0].window = NK_INVALID_HANDLE;
    surfaces[0].surface = NK_INVALID_HANDLE;
    surfaces[0].api = NK_GRAPHICS_OPENGL;
    surfaces[1].window = NK_INVALID_HANDLE;
    surfaces[1].surface = NK_INVALID_HANDLE;
    surfaces[1].api = NK_GRAPHICS_OPENGL_ES;
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
