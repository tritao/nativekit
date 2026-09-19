#include "nativekit.h"
#include "nativekit_graphics.h"
#include "nativekit_gpu.h"
#include "nativekit_time.h"
#include "nativekit_ui.h"
#include "nativekit_window.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

namespace {

bool check(bool result, const char *operation) {
    if (result)
        return true;
    std::fprintf(stderr, "backend renderer smoke: %s failed: %s\n", operation, nk_last_error());
    return false;
}

bool acquire_surface_frame(nk_window window, nk_surface surface, int32_t &width, int32_t &height) {
    if (!check(nk_window_activate(window) == NK_OK, "nk_window_activate"))
        return false;
    for (int attempt = 0; attempt < 500; ++attempt) {
        nk_event event{};
        event.struct_size = sizeof(event);
        if (!check(nk_poll_event(&event) == NK_OK, "nk_poll_event"))
            return false;
        nk_event_release(&event);

        const nk_result current = nk_surface_make_current(surface);
        if (current == NK_OK) {
            if (!check(nk_surface_get_framebuffer_size(surface, &width, &height) == NK_OK,
                       "nk_surface_get_framebuffer_size"))
                return false;
            if (width > 0 && height > 0)
                return true;
        } else if (current != NK_ERROR_INVALID_REQUEST) {
            return check(false, "nk_surface_make_current");
        }
        if (!check(nk_wait_events_timeout(0.01) == NK_OK, "nk_wait_events_timeout"))
            return false;
    }
    std::fprintf(stderr, "backend renderer smoke: surface frame did not become available\n");
    return false;
}

} // namespace

int main() {
    nk_window window{};
    nk_surface surface{};
    nkgpu_renderer producer{};
    nkgpu_render_target target{};
    nk_graphics_image image{};
    nk_graphics_image_info image_info{};
    nkui_resource imported_surface{};
    nkui_display_list list{};
    nkui_renderer renderer{};
    nkui_draw_rect_command composite{};
    bool initialized = false;
    int result = 0;
    int32_t surface_width = 0;
    int32_t surface_height = 0;

    nk_init_options init{};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    if (!check(nk_init(&init) == NK_OK, "nk_init"))
        return 1;
    initialized = true;
    if (!nk_executor_is_current(NK_EXECUTOR_RENDER)) {
        std::fprintf(stderr, "backend renderer smoke: skipped while GPU ownership is on RENDER\n");
        nk_shutdown();
        return 77;
    }

    nk_window_options window_options{};
    window_options.struct_size = sizeof(window_options);
    window_options.width = 256;
    window_options.height = 192;
    window_options.title = "NativeKit explicit-backend UI smoke";
    if (!check(nk_window_create(&window_options, &window) == NK_OK, "nk_window_create")) {
        result = 2;
        goto cleanup;
    }
    if (!check(nkgpu_surface_create(window, window_options.width, window_options.height,
                                    &surface) == NKGPU_OK,
               "nkgpu_surface_create")) {
        result = 3;
        goto cleanup;
    }

    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        bool ready = false;
        while (!ready && std::chrono::steady_clock::now() < deadline) {
            nk_event event{};
            event.struct_size = sizeof(event);
            if (nk_poll_event(&event) != NK_OK) {
                result = 4;
                goto cleanup;
            }
            ready = event.kind == NK_EVENT_SURFACE_READY && event.source == surface;
            nk_event_release(&event);
            if (!ready)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (!ready) {
            std::fprintf(stderr, "backend renderer smoke: surface did not become ready\n");
            result = 5;
            goto cleanup;
        }
    }

    if (!acquire_surface_frame(window, surface, surface_width, surface_height)) {
        result = 6;
        goto cleanup;
    }

    if (!check(nkgpu_renderer_create(surface, &producer) == NKGPU_OK, "nkgpu_renderer_create")) {
        result = 7;
        goto cleanup;
    }
    if (!check(nkgpu_render_target_create(producer, 32, 32, 0, &target) == NKGPU_OK,
               "nkgpu_render_target_create")) {
        result = 8;
        goto cleanup;
    }
    if (!check(nkgpu_begin_render_target(producer, target, 1) == NKGPU_OK &&
                   nkgpu_end_render_target(producer) == NKGPU_OK,
               "offscreen clear")) {
        result = 9;
        goto cleanup;
    }

    image_info.struct_size = sizeof(image_info);
    if (!check(nkgpu_render_target_get_image(producer, target, &image) == NKGPU_OK &&
                   nk_graphics_image_get_info(image, &image_info) == NK_OK &&
                   image_info.width == 32 && image_info.height == 32 &&
                   image_info.api == nkgpu_query_graphics_api(producer) &&
                   image_info.device.id != 0,
               "offscreen graphics image metadata")) {
        result = 10;
        goto cleanup;
    }
    if (!check(nkui_graphics_surface_create(image, &imported_surface) == NKUI_OK,
               "nkui_graphics_surface_create")) {
        result = 11;
        goto cleanup;
    }

    if (!check(nkui_display_list_create(&list) == NKUI_OK, "nkui_display_list_create")) {
        result = 12;
        goto cleanup;
    }
    composite.header = {NKUI_COMMAND_DRAW_RENDER_TARGET, NKUI_COMMAND_VERSION,
                        sizeof(nkui_draw_rect_command)};
    composite.resource = imported_surface;
    composite.x = 16.0f;
    composite.y = 16.0f;
    composite.width = 64.0f;
    composite.height = 64.0f;
    if (!check(nkui_display_list_submit(list, reinterpret_cast<const uint8_t *>(&composite),
                                        sizeof(composite)) == NKUI_OK,
               "submit imported image")) {
        result = 13;
        goto cleanup;
    }
    if (!check(nkui_renderer_create(&renderer) == NKUI_OK, "nkui_renderer_create")) {
        result = 14;
        goto cleanup;
    }

    {
        int32_t width = 0;
        int32_t height = 0;
        nkui_frame_info frame{};
        frame.struct_size = sizeof(frame);
        if (!check(nk_surface_get_framebuffer_size(surface, &width, &height) == NK_OK &&
                       width > 0 && height > 0,
                   "framebuffer before UI draw")) {
            result = 15;
            goto cleanup;
        }
        frame.logical_width = static_cast<float>(window_options.width);
        frame.logical_height = static_cast<float>(window_options.height);
        frame.framebuffer_width = width;
        frame.framebuffer_height = height;
        frame.pixel_scale = static_cast<float>(width) / window_options.width;
        const nkui_result render_result =
            nkui_renderer_render_frame(renderer, list, surface, &frame);
        if (render_result != NKUI_OK) {
            std::fprintf(stderr,
                         "backend renderer smoke: render imported image failed: result=%d, "
                         "gpu=%s, window=%s\n",
                         static_cast<int>(render_result), nkgpu_last_error(), nk_last_error());
            result = 16;
            goto cleanup;
        }
        if (!check(nk_surface_present(surface) == NK_OK, "nk_surface_present")) {
            result = 17;
            goto cleanup;
        }
    }
cleanup:
    if (renderer.id && nkui_renderer_destroy(renderer) != NKUI_OK)
        result = result ? result : 18;
    if (list.id && nkui_display_list_destroy(list) != NKUI_OK)
        result = result ? result : 19;
    if (imported_surface.id && nkui_resource_destroy(imported_surface) != NKUI_OK)
        result = result ? result : 20;
    if (target.id && nkgpu_render_target_destroy(producer, target) != NKGPU_OK)
        result = result ? result : 21;
    if (producer.id && nkgpu_renderer_destroy(producer) != NKGPU_OK)
        result = result ? result : 22;
    if (surface && nk_surface_destroy(surface) != NK_OK)
        result = result ? result : 23;
    if (window && nk_window_destroy(window) != NK_OK)
        result = result ? result : 24;
    if (initialized)
        nk_shutdown();
    return result;
}
