#include "nativekit.h"
#include "nativekit_graphics.h"
#include "nativekit_window.h"
#include "core/executor.hpp"
#include "core/frame_backend.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

namespace {

#ifdef __EMSCRIPTEN__
void report_frame_backend_stage(int stage) {
    // clang-format off
    EM_ASM({
        document.documentElement.dataset.nativekitFrameBackendResult = "stage-" + $0;
    }, stage);
    // clang-format on
}
#else
void report_frame_backend_stage(int) {}
#endif

struct FrameProbe {
    nk_surface_frame frame = NK_INVALID_HANDLE;
    nk_surface_frame_target target{};
    nk_result bind = NK_ERROR_UNKNOWN;
    nk_result submit = NK_ERROR_UNKNOWN;
    nk_result unbind = NK_ERROR_UNKNOWN;
    bool render_executor = false;
    uint64_t surface_api_violations = 0;
};

void run_frame_probe(void *data) {
    auto &probe = *static_cast<FrameProbe *>(data);
    probe.render_executor =
        !nk::core::render_executor_physical() || nk::core::executor_current() == NK_EXECUTOR_RENDER;
    nk::core::reset_render_surface_api_violations();
    nk::core::set_render_surface_api_guard(true);
    probe.bind = nk_graphics_bind_frame_target(&probe.target);
    if (probe.bind == NK_OK) {
        probe.submit = nk_frame_backend_submit(&probe.target);
        if (probe.submit == NK_OK)
            assert(nk::core::mark_frame_render_submitted(probe.frame));
        probe.unbind = nk_graphics_unbind_frame_target(&probe.target);
    }
    nk::core::set_render_surface_api_guard(false);
    probe.surface_api_violations = nk::core::render_surface_api_violations();
}

int run_frame(nk_window window, nk_surface surface, bool cancel, int32_t width, int32_t height) {
    nk_surface_frame frame = NK_INVALID_HANDLE;
    nk_surface_frame_target target{};
    target.struct_size = sizeof(target);
    if (nk_surface_acquire_frame(surface, &frame, &target) != NK_OK || frame == NK_INVALID_HANDLE ||
        target.frame != frame || target.api != NK_GRAPHICS_OPENGL_ES || target.width <= 0 ||
        target.height <= 0 || !target.device.id || !target.native_context) {
        if (frame != NK_INVALID_HANDLE)
            (void)nk_surface_cancel_frame(frame);
        return 10;
    }

    FrameProbe probe{frame, target};
    const nk_result dispatched =
        nk::core::dispatch_to_render_sync(&run_frame_probe, &probe, sizeof(probe));
    if (dispatched != NK_OK) {
        (void)nk_surface_cancel_frame(frame);
        return 20 + (-static_cast<int>(dispatched) * 100);
    }
    if (!probe.render_executor || probe.bind != NK_OK || probe.submit != NK_OK ||
        probe.unbind != NK_OK || probe.surface_api_violations != 0) {
        (void)nk_surface_cancel_frame(frame);
        return 30 + (!probe.render_executor ? 1 : 0) + (probe.bind != NK_OK ? 2 : 0) +
               (probe.submit != NK_OK ? 4 : 0) + (probe.unbind != NK_OK ? 8 : 0) +
               (probe.surface_api_violations != 0 ? 16 : 0);
    }

    const nk_result closed =
        cancel ? nk_surface_cancel_frame(frame) : nk_surface_present_frame(frame);
    if (closed != NK_OK)
        return 40;
    return nk_window_set_bounds(window, 0, 0, width, height) == NK_OK ? 0 : 50;
}

} // namespace

int main() {
    nk_init_options init{};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    const nk_result initialized = nk_init(&init);
    if (initialized != NK_OK) {
        report_frame_backend_stage(100 - static_cast<int>(initialized));
        return 100 - static_cast<int>(initialized);
    }
    report_frame_backend_stage(100);

    nk_window_options window_options{};
    window_options.struct_size = sizeof(window_options);
    window_options.width = 320;
    window_options.height = 240;
    window_options.flags = NK_WINDOW_RESIZABLE;
    nk_window window = NK_INVALID_HANDLE;
    nk_surface surface = NK_INVALID_HANDLE;
    int result = 0;

    const nk_result window_created = nk_window_create(&window_options, &window);
    if (window_created != NK_OK) {
        result = 200 - static_cast<int>(window_created);
        report_frame_backend_stage(result);
    }

    nk_surface_options surface_options{};
    surface_options.struct_size = sizeof(surface_options);
    surface_options.flags = NK_SURFACE_HIDDEN | NK_SURFACE_DEPTH | NK_SURFACE_STENCIL;
    surface_options.api = NK_GRAPHICS_OPENGL_ES;
    surface_options.width = window_options.width;
    surface_options.height = window_options.height;
    nk_result surface_created = NK_ERROR_UNKNOWN;
    if (!result)
        surface_created = nk_surface_create(window, &surface_options, &surface);
    if (!result && surface_created != NK_OK) {
        result = 300 - static_cast<int>(surface_created);
        report_frame_backend_stage(result);
    }

    if (!result) {
        /* Alternate present/cancel while resizing. This exercises the complete
           ticket close path and makes shutdown begin with no open frame. */
        const int32_t sizes[][2] = {{320, 240}, {480, 270}, {640, 360}, {320, 240}};
        for (size_t index = 0; index < sizeof(sizes) / sizeof(sizes[0]); ++index) {
            const int frame_result =
                run_frame(window, surface, index == 1, sizes[index][0], sizes[index][1]);
            if (frame_result != 0) {
                result = frame_result;
                report_frame_backend_stage(result);
                break;
            }
        }
    }

    report_frame_backend_stage(900);

    if (surface != NK_INVALID_HANDLE && nk_surface_destroy(surface) != NK_OK && !result)
        result = 5;
    if (window != NK_INVALID_HANDLE && nk_window_destroy(window) != NK_OK && !result)
        result = 6;
    nk_shutdown();

#ifdef __EMSCRIPTEN__
    // clang-format off
    EM_ASM({
        document.documentElement.dataset.nativekitFrameBackendResult =
            $0 === 0 ? "passed" : "failed-" + $0;
    }, result);
    // clang-format on
#endif
    return result;
}
