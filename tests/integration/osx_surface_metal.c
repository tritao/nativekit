#include "nativekit_graphics.h"
#include "nativekit_time.h"
#include "nativekit_window.h"
#include "core/executor.hpp"
#include "core/frame_backend.hpp"

#include <assert.h>
#include <chrono>
#include <condition_variable>
#include <mutex>

struct MetalFrameProbe {
    std::mutex mutex;
    std::condition_variable condition;
    nk_surface_frame frame = NK_INVALID_HANDLE;
    nk_surface_frame_target target = {};
    bool entered = false;
    bool release = false;
    bool complete = false;
    bool render_executor = false;
    nk_result render_result = NK_ERROR_UNKNOWN;
    uint64_t surface_api_violations = 0;
};

static void NK_CALL run_metal_frame_probe(void *data) {
    auto &probe = *static_cast<MetalFrameProbe *>(data);
    {
        std::unique_lock lock(probe.mutex);
        probe.entered = true;
        probe.condition.notify_one();
        probe.condition.wait(lock, [&probe] { return probe.release; });
    }

    probe.render_executor = nk::core::executor_current() == NK_EXECUTOR_RENDER;
    nk::core::reset_render_surface_api_violations();
    nk::core::set_render_surface_api_guard(true);
    const nk_result bound = nk_graphics_bind_frame_target(&probe.target);
    if (bound == NK_OK) {
        probe.render_result = nk_frame_backend_submit(&probe.target);
        if (probe.render_result == NK_OK)
            assert(nk::core::mark_frame_render_submitted(probe.frame));
        assert(nk_graphics_unbind_frame_target(&probe.target) == NK_OK);
    } else {
        probe.render_result = bound;
    }
    nk::core::set_render_surface_api_guard(false);
    probe.surface_api_violations = nk::core::render_surface_api_violations();

    {
        std::lock_guard lock(probe.mutex);
        probe.complete = true;
    }
    probe.condition.notify_one();
}

static void run_metal_frame_ticket(nk_surface surface, int32_t next_width, int32_t next_height) {
    nk_surface_frame frame = NK_INVALID_HANDLE;
    nk_surface_frame_target target = {};
    target.struct_size = sizeof(target);
    assert(nk_surface_acquire_frame(surface, &frame, &target) == NK_OK);
    assert(frame != NK_INVALID_HANDLE);
    assert(target.api == NK_GRAPHICS_METAL);
    assert(target.width > 0 && target.height > 0);
    assert(target.native_present_target != 0);

    MetalFrameProbe probe;
    probe.frame = frame;
    probe.target = target;
    assert(nk::core::dispatch_to_render(&run_metal_frame_probe, &probe, nullptr, sizeof(probe)) ==
           NK_OK);
    {
        std::unique_lock lock(probe.mutex);
        assert(probe.condition.wait_for(lock, std::chrono::seconds(5),
                                        [&probe] { return probe.entered; }));
    }

    /* The drawable remains prepared while RENDER owns the immutable ticket. */
    assert(nk_surface_set_bounds(surface, 12, 16, next_width, next_height) == NK_OK);
    nk_surface_frame_target pending_target = {};
    pending_target.struct_size = sizeof(pending_target);
    assert(nk_surface_get_frame_target(surface, &pending_target) == NK_OK);
    assert(pending_target.width == target.width);
    assert(pending_target.height == target.height);

    {
        std::lock_guard lock(probe.mutex);
        probe.release = true;
    }
    probe.condition.notify_one();
    {
        std::unique_lock lock(probe.mutex);
        assert(probe.condition.wait_for(lock, std::chrono::seconds(5),
                                        [&probe] { return probe.complete; }));
    }
    assert(probe.render_executor);
    assert(probe.render_result == NK_OK);
    assert(probe.surface_api_violations == 0);
    assert(nk_surface_present_frame(frame) == NK_OK);

    nk_surface_frame next_frame = NK_INVALID_HANDLE;
    nk_surface_frame_target resized_target = {};
    resized_target.struct_size = sizeof(resized_target);
    assert(nk_surface_acquire_frame(surface, &next_frame, &resized_target) == NK_OK);
    assert(resized_target.width > 0 && resized_target.height > 0);
    assert(resized_target.width != target.width || resized_target.height != target.height);
    assert(nk_surface_cancel_frame(next_frame) == NK_OK);
}

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
    surface_options.api = NK_GRAPHICS_METAL;
    surface_options.width = 160;
    surface_options.height = 120;
    nk_surface surface = NK_INVALID_HANDLE;
    const nk_result created = nk_surface_create(window, &surface_options, &surface);
    if (created == NK_ERROR_UNSUPPORTED) {
        assert(nk_window_destroy(window) == NK_OK);
        nk_shutdown();
        return 77;
    }
    assert(created == NK_OK);

    acquire_frame(window, surface);
    nk_surface_frame_target target = {0};
    target.struct_size = sizeof(target);
    assert(nk_surface_get_frame_target(surface, &target) == NK_OK);
    assert(target.api == NK_GRAPHICS_METAL);
    assert(target.width > 0 && target.height > 0);
    assert(target.device.id != 0);
    assert(target.native_target != 0);
    assert(target.native_device != 0);
    assert(target.native_context != 0);
    assert(target.native_depth_stencil_target != 0);
    assert(target.native_present_target != 0);
    assert(nk_surface_present(surface) == NK_OK);

    run_metal_frame_ticket(surface, 240, 140);

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
