#include "nativekit.h"
#include "nativekit_gpu.h"
#include "nativekit_window.h"
#include "adapter_internal.h"
#include "core/executor.hpp"
#include "testing.h"

#include <chrono>
#include <cstdio>
#include <thread>

namespace {

struct RenderTask {
    nk_surface surface = NK_INVALID_HANDLE;
    nk_surface_frame_target target{};
    nkgpu_result result = NKGPU_OK;
};

void run_render_task(void *user_data) {
    auto *task = static_cast<RenderTask *>(user_data);
    nkgpu_renderer renderer{};
    nkgpu_batch batch{};
    if (nkgpu_renderer_create_for_frame_target(task->surface, &task->target, &renderer) != NKGPU_OK)
        task->result = NKGPU_ERROR_UNKNOWN;
    else if (nkgpu_batch_begin(renderer, &batch) != NKGPU_OK)
        task->result = NKGPU_ERROR_UNKNOWN;
    else {
        nkgpu_batch_pass pass{};
        pass.struct_size = sizeof(pass);
        pass.kind = NKGPU_BATCH_PASS_WINDOW;
        pass.clear = 1;
        pass.width = static_cast<uint32_t>(task->target.width);
        pass.height = static_cast<uint32_t>(task->target.height);
        if (nkgpu_batch_append_pass(batch, &pass) != NKGPU_OK ||
            nkgpu_batch_seal(batch) != NKGPU_OK ||
            nkgpu_batch_submit(renderer, batch, &task->target) != NKGPU_OK)
            task->result = NKGPU_ERROR_UNKNOWN;
    }
    if (batch.id && nkgpu_batch_destroy(batch) != NKGPU_OK && task->result == NKGPU_OK)
        task->result = NKGPU_ERROR_UNKNOWN;
    if (renderer.id && nkgpu_renderer_destroy(renderer) != NKGPU_OK &&
        task->result == NKGPU_OK)
        task->result = NKGPU_ERROR_UNKNOWN;
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
    window_options.title = "NativeKit physical GPU frame target";
    nk_window window = NK_INVALID_HANDLE;
    nk_surface surface = NK_INVALID_HANDLE;
    nk_surface_frame frame = NK_INVALID_HANDLE;
    nk_surface_frame_target target{};
    RenderTask task{};
    int result = 0;

    if (nk_window_create(&window_options, &window) != NK_OK ||
        nkgpu_surface_create(window, window_options.width, window_options.height, &surface) !=
            NKGPU_OK) {
        result = 2;
        goto cleanup;
    }

    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        bool ready = false;
        while (!ready && std::chrono::steady_clock::now() < deadline) {
            nk_event event{};
            event.struct_size = sizeof(event);
            if (nk_poll_event(&event) != NK_OK) {
                nk_event_release(&event);
                result = 3;
                goto cleanup;
            }
            ready = event.kind == NK_EVENT_SURFACE_READY && event.source == surface;
            nk_event_release(&event);
            if (!ready)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (!ready) {
            result = 4;
            goto cleanup;
        }
    }

    target.struct_size = sizeof(target);
    if (nk_surface_acquire_frame(surface, &frame, &target) != NK_OK ||
        target.frame != frame || target.native_context == 0 || target.native_target == 0) {
        result = 5;
        goto cleanup;
    }

    task = RenderTask{surface, target, NKGPU_OK};
    nkgpu_test_forbid_surface_target_queries();
    if (nk::core::dispatch_to_render_sync(&run_render_task, &task, sizeof(task)) != NK_OK ||
        task.result != NKGPU_OK) {
        result = 6;
    }
    nkgpu_test_allow_surface_target_queries();

    if (frame != NK_INVALID_HANDLE) {
        if (result == 0)
            result = nk_surface_present_frame(frame) == NK_OK ? 0 : 7;
        else
            (void)nk_surface_cancel_frame(frame);
        frame = NK_INVALID_HANDLE;
    }

cleanup:
    nkgpu_test_allow_surface_target_queries();
    if (frame != NK_INVALID_HANDLE)
        (void)nk_surface_cancel_frame(frame);
    if (surface != NK_INVALID_HANDLE)
        (void)nkgpu_surface_destroy(surface);
    if (window != NK_INVALID_HANDLE)
        (void)nk_window_destroy(window);
    nk_shutdown();
    return result;
}
