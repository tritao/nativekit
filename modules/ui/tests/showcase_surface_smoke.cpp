#include "nativekit.h"
#include "nativekit_gpu.h"
#include "nativekit_ui.h"
#include "nativekit_window.h"
#include "core/executor.hpp"
#include "core/frame_backend.hpp"
#include "testing.h"

#include <chrono>
#include <thread>

/* The showcase-only ABI is intentionally not part of the stable UI header. */
extern "C" nkui_result nkui_showcase_cube_create(nkui_resource *out_surface);
extern "C" nkui_result nkui_showcase_cube_set_rotation(nkui_resource surface, float radians);

namespace {

void render_barrier(void *data) {
    *static_cast<bool *>(data) = true;
}

bool drain_render_submission() {
    if (!nk::core::render_executor_physical())
        return true;
    bool complete = false;
    if (nk::core::dispatch_to_render_sync(&render_barrier, &complete, sizeof(complete)) != NK_OK ||
        !complete)
        return false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        nk_event event{};
        event.struct_size = sizeof(event);
        if (nk_poll_event(&event) != NK_OK)
            return false;
        nk_event_release(&event);
        if (nk::core::frame_ticket_count() == 0)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return nk::core::frame_ticket_count() == 0;
}

bool wait_until_ready(nk_surface surface, int32_t &width, int32_t &height) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        nk_event event{};
        event.struct_size = sizeof(event);
        if (nk_poll_event(&event) != NK_OK)
            return false;
        if (event.kind == NK_EVENT_SURFACE_READY && event.source == surface) {
            if (!nk::core::render_executor_physical() &&
                nk_surface_make_current(surface) != NK_OK) {
                nk_event_release(&event);
                return false;
            }
            const bool sized = nk_surface_get_framebuffer_size(surface, &width, &height) == NK_OK;
            nk_event_release(&event);
            return sized && width > 0 && height > 0;
        }
        if (event.kind == NK_EVENT_SURFACE_RESIZE && event.source == surface &&
            event.data_size >= sizeof(nk_surface_resize_event)) {
            const auto *resize = static_cast<const nk_surface_resize_event *>(event.data);
            width = resize->framebuffer_width;
            height = resize->framebuffer_height;
        }
        nk_event_release(&event);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

} // namespace

int main() {
    nk_init_options init{};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    if (nk_init(&init) != NK_OK)
        return 1;

    nk_window_options window_options{};
    window_options.struct_size = sizeof(window_options);
    window_options.width = 320;
    window_options.height = 240;
    window_options.flags = NK_WINDOW_RESIZABLE;
    window_options.title = "NativeKit sealed showcase surface";
    nk_window window = NK_INVALID_HANDLE;
    nk_surface surface = NK_INVALID_HANDLE;
    nkui_renderer renderer{};
    nkui_display_list list{};
    nkui_resource cube{};
    int result = 0;

    if (nk_window_create(&window_options, &window) != NK_OK)
        result = 2;
    nk_surface_options surface_options{};
    surface_options.struct_size = sizeof(surface_options);
    surface_options.api = nkgpu_default_graphics_api();
    surface_options.flags = NK_SURFACE_DEPTH | NK_SURFACE_STENCIL;
    surface_options.major_version = 3;
    surface_options.minor_version = surface_options.api == NK_GRAPHICS_OPENGL ? 3 : 0;
    surface_options.width = window_options.width;
    surface_options.height = window_options.height;
    if (!result && nk_surface_create(window, &surface_options, &surface) != NK_OK)
        result = 3;

    int32_t width = 0;
    int32_t height = 0;
    if (!result && !wait_until_ready(surface, width, height))
        result = 4;
    if (!result && nkui_renderer_create(&renderer) != NKUI_OK)
        result = 5;
    if (!result && nkui_display_list_create(&list) != NKUI_OK)
        result = 6;
    if (!result && nkui_showcase_cube_create(&cube) != NKUI_OK)
        result = 7;

    if (!result) {
        const nkui_draw_rect_command draw{
            {NKUI_COMMAND_DRAW_RENDER_TARGET, NKUI_COMMAND_VERSION, sizeof(nkui_draw_rect_command)},
            cube,
            24.0f,
            24.0f,
            272.0f,
            192.0f};
        if (nkui_display_list_submit(list, reinterpret_cast<const uint8_t *>(&draw),
                                     sizeof(draw)) != NKUI_OK)
            result = 8;
    }

    if (!result) {
        nk::core::reset_render_surface_api_violations();
        nk::core::set_render_surface_api_guard(true);
        for (int frame = 0; frame < 12 && !result; ++frame) {
            if (frame == 4 && nk_surface_set_bounds(surface, 0, 0, 480, 270) != NK_OK)
                result = 9;
            if (frame == 4) {
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                while (!result && (width < 480 || height < 270) &&
                       std::chrono::steady_clock::now() < deadline) {
                    nk_event event{};
                    event.struct_size = sizeof(event);
                    if (nk_poll_event(&event) != NK_OK) {
                        result = 10;
                        break;
                    }
                    if (event.kind == NK_EVENT_SURFACE_RESIZE && event.source == surface &&
                        event.data_size >= sizeof(nk_surface_resize_event)) {
                        const auto *resize =
                            static_cast<const nk_surface_resize_event *>(event.data);
                        width = resize->framebuffer_width;
                        height = resize->framebuffer_height;
                    }
                    nk_event_release(&event);
                }
                if (!result && (width < 480 || height < 270))
                    result = 11;
            }
            if (result)
                break;
            if (nkui_showcase_cube_set_rotation(cube, 0.2f * static_cast<float>(frame)) !=
                NKUI_OK) {
                result = 12;
                break;
            }
            const nkui_frame_info frame_info{sizeof(frame_info),
                                             static_cast<float>(width),
                                             static_cast<float>(height),
                                             width,
                                             height,
                                             1.0f};
            if (nkui_renderer_render_frame(renderer, list, surface, &frame_info) != NKUI_OK) {
                result = 13;
                break;
            }
            if (nk::core::render_executor_physical()) {
                if (!drain_render_submission())
                    result = 14;
            } else if (nk_surface_present(surface) != NK_OK) {
                result = 15;
            }
            if (!result && frame == 8)
                nkgpu_test_lose_all_after_frames(1);
        }
        nk::core::set_render_surface_api_guard(false);
        if (!result && nk::core::render_surface_api_violations() != 0)
            result = 16;
    }

    nkui_renderer_stats stats{};
    stats.struct_size = sizeof(stats);
    if (!result && (nkui_renderer_get_stats(renderer, &stats) != NKUI_OK ||
                    stats.render_plan_commands < 12 || stats.display_list_count < 12 ||
                    (nk::core::render_executor_physical() &&
                     (stats.render_submissions < 12 || stats.render_submission_executions < 12 ||
                      stats.render_submission_failures != 0 || stats.device_losses == 0))))
        result = 17;

    if (cube.id)
        (void)nkui_resource_destroy(cube);
    if (list.id)
        (void)nkui_display_list_destroy(list);
    if (renderer.id)
        (void)nkui_renderer_destroy(renderer);
    if (surface != NK_INVALID_HANDLE)
        (void)nk_surface_destroy(surface);
    if (window != NK_INVALID_HANDLE)
        (void)nk_window_destroy(window);
    nk_shutdown();
    return result;
}
