#include "nativekit.h"
#include "nativekit_graphics.h"
#include "nativekit_gpu.h"
#include "nativekit_time.h"
#include "nativekit_ui.h"
#include "nativekit_window.h"
#include "adapter_internal.h"
#include "core/executor.hpp"
#include "core/frame_backend.hpp"
#include "testing.h"

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>

namespace {

bool check(bool result, const char *operation) {
    if (result)
        return true;
    std::fprintf(stderr, "backend renderer smoke: %s failed: %s\n", operation, nk_last_error());
    return false;
}

void print_render_metrics(nkui_renderer renderer) {
    if (!std::getenv("NKUI_RENDER_METRICS") || !renderer.id)
        return;
    nkui_renderer_stats stats{};
    stats.struct_size = sizeof(stats);
    if (nkui_renderer_get_stats(renderer, &stats) != NKUI_OK)
        return;
    std::fprintf(stderr,
                 "backend renderer metrics: submissions=%llu executions=%llu replacements=%llu "
                 "cancellations=%llu failures=%llu build_ns=%llu queue_ns=%llu execution_ns=%llu "
                 "acquire_present_ns=%llu gpu_frames=%llu\n",
                 static_cast<unsigned long long>(stats.render_submissions),
                 static_cast<unsigned long long>(stats.render_submission_executions),
                 static_cast<unsigned long long>(stats.render_submission_replacements),
                 static_cast<unsigned long long>(stats.render_submission_cancellations),
                 static_cast<unsigned long long>(stats.render_submission_failures),
                 static_cast<unsigned long long>(stats.render_submission_build_ns),
                 static_cast<unsigned long long>(stats.render_submission_queue_latency_ns),
                 static_cast<unsigned long long>(stats.render_submission_execution_ns),
                 static_cast<unsigned long long>(stats.render_submission_acquire_to_present_ns),
                 static_cast<unsigned long long>(stats.gpu_frames));
}

bool acquire_surface_frame(nk_window window, nk_surface surface, int32_t &width, int32_t &height,
                           nk_surface_frame_target &target) {
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
            target.struct_size = sizeof(target);
            if (!check(nk_surface_get_frame_target(surface, &target) == NK_OK,
                       "nk_surface_get_frame_target"))
                return false;
            if (width > 0 && height > 0 && target.width == width && target.height == height)
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

struct RenderTask {
    using Function = void (*)(RenderTask &) noexcept;

    std::mutex mutex;
    std::condition_variable condition;
    Function function = nullptr;
    bool complete = false;
    bool success = false;
    nk_surface surface = NK_INVALID_HANDLE;
    nk_surface_frame_target target{};
    nkgpu_renderer producer{};
    nkgpu_image image_resource{};
    nk_graphics_image image{};
};

struct BlockingRenderTask {
    std::mutex mutex;
    std::condition_variable condition;
    bool entered = false;
    bool release = false;
    bool complete = false;
};

void NK_CALL run_blocking_render_task(void *data) {
    auto &task = *static_cast<BlockingRenderTask *>(data);
    std::unique_lock lock(task.mutex);
    task.entered = true;
    task.condition.notify_one();
    task.condition.wait(lock, [&task] { return task.release; });
    task.complete = true;
    task.condition.notify_one();
}

bool start_blocking_render_task(BlockingRenderTask &task) {
    if (!check(nk::core::dispatch_to_render(&run_blocking_render_task, &task, nullptr,
                                            sizeof(task)) == NK_OK,
               "dispatch blocking render task"))
        return false;
    std::unique_lock lock(task.mutex);
    if (!task.condition.wait_for(lock, std::chrono::seconds(5), [&task] { return task.entered; })) {
        std::fprintf(stderr, "backend renderer smoke: blocking render task did not start\n");
        return false;
    }
    return true;
}

bool wait_surface_ready(nk_window window, nk_surface surface, int32_t &width, int32_t &height,
                        nk_surface_frame_target &target) {
    if (!check(nk_window_activate(window) == NK_OK, "activate scheduler stress window"))
        return false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        nk_event event{};
        event.struct_size = sizeof(event);
        if (!check(nk_poll_event(&event) == NK_OK, "poll scheduler stress event"))
            return false;
        nk_event_release(&event);
        if (nk_surface_make_current(surface) == NK_OK &&
            nk_surface_get_framebuffer_size(surface, &width, &height) == NK_OK && width > 0 &&
            height > 0) {
            target.struct_size = sizeof(target);
            if (nk_surface_get_frame_target(surface, &target) == NK_OK && target.width == width &&
                target.height == height)
                return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::fprintf(stderr, "backend renderer smoke: scheduler stress surface did not become ready\n");
    return false;
}

bool wait_surface_frame_state(nk_surface surface, bool open) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        if (nk::core::surface_frame_open(surface) == open)
            return true;
        nk_event event{};
        event.struct_size = sizeof(event);
        if (nk_poll_event(&event) != NK_OK)
            return false;
        nk_event_release(&event);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return nk::core::surface_frame_open(surface) == open;
}

bool wait_render_submission_executions(nkui_renderer renderer, uint64_t expected) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        nkui_renderer_stats stats{};
        if (nkui_renderer_get_stats(renderer, &stats) != NKUI_OK)
            return false;
        if (stats.render_submission_executions >= expected)
            return true;
        nk_event event{};
        event.struct_size = sizeof(event);
        if (nk_poll_event(&event) != NK_OK)
            return false;
        nk_event_release(&event);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    nkui_renderer_stats stats{};
    return nkui_renderer_get_stats(renderer, &stats) == NKUI_OK &&
           stats.render_submission_executions >= expected;
}

void NK_CALL run_render_task(void *data) {
    auto &task = *static_cast<RenderTask *>(data);
    if (!nk_executor_is_current(NK_EXECUTOR_RENDER)) {
        std::lock_guard lock(task.mutex);
        task.complete = true;
        task.condition.notify_one();
        return;
    }
    task.function(task);
    {
        std::lock_guard lock(task.mutex);
        task.complete = true;
    }
    task.condition.notify_one();
}

bool dispatch_render_task(RenderTask &task) {
    if (!nk::core::render_executor_physical()) {
        task.function(task);
        return task.success;
    }
    if (!check(nk::core::dispatch_to_render(&run_render_task, &task, nullptr, sizeof(task)) ==
                   NK_OK,
               "dispatch render task"))
        return false;
    std::unique_lock lock(task.mutex);
    if (!task.condition.wait_for(lock, std::chrono::seconds(5),
                                 [&task] { return task.complete; })) {
        std::fprintf(stderr, "backend renderer smoke: render task timed out\n");
        task.condition.wait(lock, [&task] { return task.complete; });
    }
    return task.success;
}

void create_offscreen_resources(RenderTask &task) noexcept {
    const nkgpu_result created =
        nk::core::render_executor_physical()
            ? nkgpu_renderer_create_for_frame_target(task.surface, &task.target, &task.producer)
            : nkgpu_renderer_create(task.surface, &task.producer);
    if (created != NKGPU_OK)
        return;
    nkgpu_image_desc image_desc{};
    image_desc.struct_size = sizeof(image_desc);
    image_desc.width = 32;
    image_desc.height = 32;
    image_desc.format = NKGPU_IMAGEFORMAT_RGBA8;
    image_desc.usage = NKGPU_IMAGE_SAMPLED | NKGPU_IMAGE_RENDER_TARGET;
    if (nkgpu_image_create_desc(task.producer, &image_desc, &task.image_resource) != NKGPU_OK)
        return;
    const nkgpu_result frame = nk::core::render_executor_physical()
                                   ? nkgpu_frame_begin_with_target(task.producer, &task.target)
                                   : nkgpu_frame_begin(task.producer);
    if (frame != NKGPU_OK)
        return;
    nkgpu_render_pass_desc pass{};
    pass.struct_size = sizeof(pass);
    pass.color_count = 1;
    pass.colors[0].image = task.image_resource;
    pass.colors[0].action.load_action = NKGPU_LOADACTION_CLEAR;
    pass.colors[0].action.store_action = NKGPU_STOREACTION_STORE;
    pass.colors[0].action.clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
    if (nkgpu_begin_render_pass(task.producer, &pass) != NKGPU_OK)
        return;
    if (nkgpu_end_pass(task.producer) != NKGPU_OK)
        return;
    if (nkgpu_end_frame_deferred_present(task.producer) != NKGPU_OK)
        return;
    if (nkgpu_image_get_graphics_image(task.producer, task.image_resource, &task.image) !=
        NKGPU_OK)
        return;
    task.success = true;
}

void destroy_offscreen_resources(RenderTask &task) noexcept {
    (void)nkgpu_end_pass(task.producer);
    (void)nkgpu_end_frame_deferred_present(task.producer);
    if (task.image_resource.id)
        (void)nkgpu_image_destroy(task.producer, task.image_resource);
    if (task.producer.id)
        (void)nkgpu_renderer_destroy(task.producer);
    task.image_resource = {};
    task.producer = {};
    task.image = {};
    task.success = true;
}

bool run_shutdown_orphan_completion_test() {
    if (!nk::core::render_executor_physical())
        return true;

    nk_init_options init{};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    bool initialized = false;
    nk_window first_window{};
    nk_surface first_surface{};
    nk_window second_window{};
    nk_surface second_surface{};
    nkui_display_list list{};
    nkui_renderer renderer{};

    auto shutdown_runtime = [&] {
        if (initialized) {
            nk_shutdown();
            initialized = false;
        }
    };
    auto release_blocker = [](BlockingRenderTask &task) {
        {
            std::lock_guard lock(task.mutex);
            task.release = true;
        }
        task.condition.notify_one();
    };
    auto fail = [&](const char *operation) {
        std::fprintf(stderr, "backend renderer smoke: shutdown orphan test: %s failed: %s\n",
                     operation, nk_last_error());
        shutdown_runtime();
        return false;
    };

    if (!check(nk_init(&init) == NK_OK, "shutdown orphan nk_init"))
        return false;
    initialized = true;
    nk_window_options options{};
    options.struct_size = sizeof(options);
    options.width = 256;
    options.height = 192;
    options.title = "NativeKit shutdown orphan smoke";
    if (!check(nk_window_create(&options, &first_window) == NK_OK,
               "create shutdown orphan window") ||
        !check(nkgpu_surface_create(first_window, options.width, options.height, &first_surface) ==
                   NKGPU_OK,
               "create shutdown orphan surface"))
        return fail("create first shutdown orphan runtime");
    int32_t width = 0;
    int32_t height = 0;
    nk_surface_frame_target target{};
    if (!wait_surface_ready(first_window, first_surface, width, height, target))
        return fail("wait for first shutdown orphan surface");
    if (!check(nkui_display_list_create(&list) == NKUI_OK, "create shutdown orphan display list") ||
        !check(nkui_renderer_create(&renderer) == NKUI_OK, "create shutdown orphan renderer"))
        return fail("create shutdown orphan UI resources");

    const nkui_frame_info frame{sizeof(frame),
                                static_cast<float>(options.width),
                                static_cast<float>(options.height),
                                width,
                                height,
                                1.0f};
    BlockingRenderTask blocker{};
    if (!start_blocking_render_task(blocker)) {
        release_blocker(blocker);
        return fail("start shutdown orphan blocker");
    }
    nk::core::reset_render_surface_api_violations();
    nk::core::set_render_surface_api_guard(true);
    nkgpu_test_forbid_surface_target_queries();
    nk::core::fail_next_platform_dispatch();
    const nkui_result submitted = nkui_renderer_render_frame(renderer, list, first_surface, &frame);
    if (submitted != NKUI_OK) {
        nk::core::set_render_surface_api_guard(false);
        nkgpu_test_allow_surface_target_queries();
        release_blocker(blocker);
        return fail("submit shutdown orphan frame");
    }
    release_blocker(blocker);
    RenderTask barrier{};
    barrier.function = [](RenderTask &task) noexcept { task.success = true; };
    if (!dispatch_render_task(barrier)) {
        nk::core::set_render_surface_api_guard(false);
        nkgpu_test_allow_surface_target_queries();
        return fail("drain shutdown orphan render task");
    }
    const uint64_t surface_call_violations = nk::core::render_surface_api_violations();
    nk::core::set_render_surface_api_guard(false);
    nkgpu_test_allow_surface_target_queries();
    nkui_renderer_stats stats{};
    if (!check(surface_call_violations == 0, "shutdown orphan render surface API ownership") ||
        !check(nkui_renderer_get_stats(renderer, &stats) == NKUI_OK,
               "read shutdown orphan stats") ||
        !check(stats.render_submission_failures >= 1, "shutdown orphan failure accounting") ||
        !check(nk::core::frame_ticket_count() == 1, "shutdown orphan frame ticket remains open")) {
        shutdown_runtime();
        return false;
    }

    if (!check(nkui_renderer_destroy(renderer) == NKUI_OK, "destroy shutdown orphan renderer") ||
        !check(nkui_display_list_destroy(list) == NKUI_OK,
               "destroy shutdown orphan display list")) {
        shutdown_runtime();
        return false;
    }
    renderer = {};
    list = {};
    RenderTask destroy_barrier{};
    destroy_barrier.function = [](RenderTask &task) noexcept { task.success = true; };
    if (!dispatch_render_task(destroy_barrier)) {
        shutdown_runtime();
        return false;
    }

    shutdown_runtime();
    if (!check(nk::core::frame_ticket_count() == 0, "shutdown orphan frame tickets cleared"))
        return false;

    if (!check(nk_init(&init) == NK_OK, "reinitialize after shutdown orphan"))
        return false;
    initialized = true;
    if (!check(nk_window_create(&options, &second_window) == NK_OK,
               "create reinitialized shutdown orphan window") ||
        !check(nkgpu_surface_create(second_window, options.width, options.height,
                                    &second_surface) == NKGPU_OK,
               "create reinitialized shutdown orphan surface"))
        return fail("create reinitialized shutdown orphan runtime");
    if (!wait_surface_ready(second_window, second_surface, width, height, target))
        return fail("wait for reinitialized shutdown orphan surface");
    nk_surface_frame frame_token = NK_INVALID_HANDLE;
    target.struct_size = sizeof(target);
    if (!check(nk_surface_acquire_frame(second_surface, &frame_token, &target) == NK_OK,
               "acquire frame after shutdown orphan") ||
        !check(nk_surface_cancel_frame(frame_token) == NK_OK,
               "cancel frame after shutdown orphan")) {
        if (frame_token != NK_INVALID_HANDLE)
            (void)nk_surface_cancel_frame(frame_token);
        return fail("verify reinitialized shutdown orphan frame");
    }
    if (!check(nk_surface_destroy(second_surface) == NK_OK,
               "destroy reinitialized shutdown orphan surface") ||
        !check(nk_window_destroy(second_window) == NK_OK,
               "destroy reinitialized shutdown orphan window"))
        return fail("destroy reinitialized shutdown orphan runtime");
    shutdown_runtime();
    return true;
}

} // namespace

int main() {
    nk_window window{};
    nk_surface surface{};
    nkgpu_renderer producer{};
    nkgpu_image image_resource{};
    nk_graphics_image image{};
    nk_graphics_image_info image_info{};
    nkui_resource imported_surface{};
    nkui_display_list list{};
    nkui_display_list scheduler_list{};
    nkui_renderer renderer{};
    nkui_renderer recovery_renderer{};
    nkui_resource scheduler_image{};
    nk_window scheduler_windows[2]{};
    nk_surface scheduler_surfaces[2]{};
    nk_surface_frame_target scheduler_targets[2]{};
    nkui_draw_rect_command composite{};
    bool initialized = false;
    int result = 0;
    int32_t surface_width = 0;
    int32_t surface_height = 0;
    nk_surface_frame_target setup_target{};
    RenderTask setup_task{};
    RenderTask destroy_task{};
    bool setup_ok = false;
    uint64_t setup_surface_call_violations = 0;

    nk_init_options init{};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    if (!check(nk_init(&init) == NK_OK, "nk_init"))
        return 1;
    initialized = true;

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

    if (!acquire_surface_frame(window, surface, surface_width, surface_height, setup_target)) {
        result = 6;
        goto cleanup;
    }
    if (nk::core::render_executor_physical() &&
        !check(nkgpu_bind_frame_target(&setup_target) == NKGPU_ERROR_WRONG_THREAD,
               "reject platform frame-target binding")) {
        result = 6;
        goto cleanup;
    }

    setup_task.surface = surface;
    setup_task.target = setup_target;
    setup_task.function = &create_offscreen_resources;
    if (nk::core::render_executor_physical()) {
        nk::core::reset_render_surface_api_violations();
        nk::core::set_render_surface_api_guard(true);
        nkgpu_test_forbid_surface_target_queries();
    }
    setup_ok = dispatch_render_task(setup_task);
    setup_surface_call_violations =
        nk::core::render_executor_physical() ? nk::core::render_surface_api_violations() : 0;
    if (nk::core::render_executor_physical()) {
        nk::core::set_render_surface_api_guard(false);
        nkgpu_test_allow_surface_target_queries();
    }
    if (setup_surface_call_violations != 0) {
        std::fprintf(stderr,
                     "backend renderer smoke: setup touched the surface API on RENDER (%llu)\n",
                     static_cast<unsigned long long>(setup_surface_call_violations));
        result = 7;
        goto cleanup;
    }
    producer = setup_task.producer;
    image_resource = setup_task.image_resource;
    image = setup_task.image;
    if (!setup_ok) {
        std::fprintf(stderr, "backend renderer smoke: offscreen setup failed: %s\n",
                     nkgpu_last_error());
        result = 7;
        goto cleanup;
    }
    image_info.struct_size = sizeof(image_info);
    if (!check(nk_graphics_image_get_info(image, &image_info) == NK_OK && image_info.width == 32 &&
                   image_info.height == 32 &&
                   image_info.api == nkgpu_query_graphics_api(producer) &&
                   image_info.device.id != 0,
               "offscreen graphics image metadata")) {
        result = 8;
        goto cleanup;
    }
    if (nk::core::render_executor_physical()) {
        nkgpu_render_target wrong_thread_target{};
        if (!check(nkgpu_render_target_create(producer, 4, 4, 0, &wrong_thread_target) ==
                       NKGPU_ERROR_WRONG_THREAD,
                   "reject platform mutation of render-owned renderer")) {
            result = 8;
            goto cleanup;
        }
    }
    if (!check(nk_surface_present(surface) == NK_OK, "close setup frame")) {
        result = 9;
        goto cleanup;
    }
    /* Imported images use the retained-image producer path, without a live callback. */
    if (!check(nkui_graphics_surface_create(image, &imported_surface) == NKUI_OK,
               "nkui_graphics_surface_create")) {
        result = 11;
        goto cleanup;
    }
    if (!check(nkui_graphics_surface_publish_image(imported_surface, image) == NKUI_OK,
               "nkui_graphics_surface_publish_image")) {
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
        nk::core::reset_render_surface_api_violations();
        nk::core::set_render_surface_api_guard(true);
        nkgpu_test_forbid_surface_target_queries();
        const nkui_result render_result =
            nkui_renderer_render_frame(renderer, list, surface, &frame);
        if (render_result != NKUI_OK) {
            nk::core::set_render_surface_api_guard(false);
            nkgpu_test_allow_surface_target_queries();
            std::fprintf(stderr,
                         "backend renderer smoke: render imported image failed: result=%d, "
                         "gpu=%s, window=%s\n",
                         static_cast<int>(render_result), nkgpu_last_error(), nk_last_error());
            result = 16;
            goto cleanup;
        }
        RenderTask barrier{};
        barrier.function = [](RenderTask &task) noexcept { task.success = true; };
        if (!dispatch_render_task(barrier)) {
            nk::core::set_render_surface_api_guard(false);
            nkgpu_test_allow_surface_target_queries();
            result = 17;
            goto cleanup;
        }
        const uint64_t surface_call_violations = nk::core::render_surface_api_violations();
        nk::core::set_render_surface_api_guard(false);
        nkgpu_test_allow_surface_target_queries();
        if (!check(surface_call_violations == 0, "render surface API ownership")) {
            result = 17;
            goto cleanup;
        }
        nk_event completion_event{};
        completion_event.struct_size = sizeof(completion_event);
        if (!check(nk_poll_event(&completion_event) == NK_OK, "drain render completion")) {
            result = 18;
            goto cleanup;
        }
        nk_event_release(&completion_event);
        if (!nk::core::render_executor_physical() &&
            !check(nk_surface_present(surface) == NK_OK, "nk_surface_present")) {
            result = 19;
            goto cleanup;
        }
    }

    if (nk::core::render_executor_physical()) {
        int32_t scheduler_width[2]{};
        int32_t scheduler_height[2]{};
        nk_window_options scheduler_window_options = window_options;
        scheduler_window_options.title = "NativeKit scheduler stress";
        for (size_t index = 0; index < 2; ++index) {
            if (!check(nk_window_create(&scheduler_window_options, &scheduler_windows[index]) ==
                           NK_OK,
                       "create scheduler stress window")) {
                result = 26;
                goto cleanup;
            }
            bool surface_created = false;
            if (index == 0) {
                surface_created = nkgpu_surface_create(
                                      scheduler_windows[index], scheduler_window_options.width,
                                      scheduler_window_options.height, &scheduler_surfaces[index]) ==
                                  NKGPU_OK;
            } else {
                const nk_graphics_api api = scheduler_targets[0].api;
                if (api == NK_GRAPHICS_OPENGL || api == NK_GRAPHICS_OPENGL_ES) {
                    /* Keep the GL stress test within the provisioned runtime:
                       the second surface uses a distinct target but shares
                       surface zero's context/share group. */
                    nk_surface_options shared_options{};
                    shared_options.struct_size = sizeof(shared_options);
                    shared_options.flags = api == NK_GRAPHICS_OPENGL
                                               ? NK_SURFACE_FORWARD_COMPATIBLE
                                               : 0;
                    shared_options.api = api;
                    shared_options.major_version = 3;
                    shared_options.minor_version = api == NK_GRAPHICS_OPENGL ? 3 : 0;
                    shared_options.width = scheduler_window_options.width;
                    shared_options.height = scheduler_window_options.height;
                    shared_options.share_surface = scheduler_surfaces[0];
                    surface_created = nk_surface_create(scheduler_windows[index], &shared_options,
                                                         &scheduler_surfaces[index]) == NK_OK;
                } else {
                    /* Explicit backends own their device/swapchain setup;
                       create a second target through the public factory and
                       let the scheduler classify cross-device switching. */
                    surface_created = nkgpu_surface_create_for_api(
                                          scheduler_windows[index], api, scheduler_window_options.width,
                                          scheduler_window_options.height,
                                          &scheduler_surfaces[index]) == NKGPU_OK;
                }
            }
            if (!check(surface_created, "create scheduler stress surface") ||
                !wait_surface_ready(scheduler_windows[index], scheduler_surfaces[index],
                                    scheduler_width[index], scheduler_height[index],
                                    scheduler_targets[index])) {
                result = 26;
                goto cleanup;
            }
        }

        /* Exercise the frame-ticket lifecycle at the same platform boundary
           used by the asynchronous scheduler.  A resize while a frame is
           open must not mutate the immutable target snapshot; the resize is
           applied when the ticket is cancelled and the next acquire observes
           the new dimensions. */
        {
            nk_surface_frame lifecycle_frame = NK_INVALID_HANDLE;
            nk_surface_frame_target lifecycle_target{};
            lifecycle_target.struct_size = sizeof(lifecycle_target);
            if (!check(nk_surface_acquire_frame(scheduler_surfaces[1], &lifecycle_frame,
                                                &lifecycle_target) == NK_OK,
                       "acquire lifecycle frame") ||
                !check(lifecycle_target.frame == lifecycle_frame, "lifecycle frame target token")) {
                result = 26;
                goto cleanup;
            }
            const int32_t previous_width = lifecycle_target.width;
            const int32_t previous_height = lifecycle_target.height;
            const int32_t resized_logical_width = scheduler_window_options.width + 32;
            const int32_t resized_logical_height = scheduler_window_options.height + 16;
            if (!check(nkgpu_surface_resize(scheduler_surfaces[1], resized_logical_width,
                                            resized_logical_height) == NKGPU_OK,
                       "resize open lifecycle frame") ||
                !check(lifecycle_target.width == previous_width &&
                           lifecycle_target.height == previous_height,
                       "immutable lifecycle frame target")) {
                (void)nk_surface_cancel_frame(lifecycle_frame);
                result = 26;
                goto cleanup;
            }
            if (!check(nk_surface_cancel_frame(lifecycle_frame) == NK_OK,
                       "cancel lifecycle frame") ||
                !check(nk_surface_cancel_frame(lifecycle_frame) == NK_ERROR_INVALID_HANDLE,
                       "reject repeated lifecycle cancel")) {
                result = 26;
                goto cleanup;
            }

            int32_t resized_width = 0;
            int32_t resized_height = 0;
            nk_surface_frame_target resized_target{};
            if (!wait_surface_ready(scheduler_windows[1], scheduler_surfaces[1], resized_width,
                                    resized_height, resized_target) ||
                !check(resized_width > 0 && resized_height > 0 &&
                           (resized_width != previous_width || resized_height != previous_height),
                       "observe resized lifecycle surface")) {
                result = 26;
                goto cleanup;
            }
            nk_surface_frame resized_frame = NK_INVALID_HANDLE;
            nk_surface_frame_target resized_frame_target{};
            resized_frame_target.struct_size = sizeof(resized_frame_target);
            if (!check(nk_surface_acquire_frame(scheduler_surfaces[1], &resized_frame,
                                                &resized_frame_target) == NK_OK,
                       "acquire resized lifecycle frame") ||
                !check(resized_frame_target.width == resized_width &&
                           resized_frame_target.height == resized_height,
                       "resized lifecycle frame target")) {
                (void)nk_surface_cancel_frame(resized_frame);
                result = 26;
                goto cleanup;
            }
            if (!check(nk_surface_cancel_frame(resized_frame) == NK_OK,
                       "cancel resized lifecycle frame") ||
                !check(nkgpu_surface_resize(scheduler_surfaces[1], scheduler_window_options.width,
                                            scheduler_window_options.height) == NKGPU_OK,
                       "restore lifecycle surface size")) {
                result = 26;
                goto cleanup;
            }
            int32_t restored_width = 0;
            int32_t restored_height = 0;
            nk_surface_frame_target restored_target{};
            if (!wait_surface_ready(scheduler_windows[1], scheduler_surfaces[1], restored_width,
                                    restored_height, restored_target)) {
                result = 26;
                goto cleanup;
            }
        }

        if (!check(nkui_display_list_create(&scheduler_list) == NKUI_OK,
                   "create scheduler display list")) {
            result = 26;
            goto cleanup;
        }
        const uint8_t scheduler_pixels[] = {64, 160, 240, 255};
        if (!check(nkui_image_create(1, 1, NKUI_IMAGE_RGBA8, scheduler_pixels,
                                     sizeof(scheduler_pixels), &scheduler_image) == NKUI_OK,
                   "create scheduler image")) {
            result = 26;
            goto cleanup;
        }
        nkui_draw_rect_command scheduler_composite{};
        scheduler_composite.header = {NKUI_COMMAND_DRAW_IMAGE, NKUI_COMMAND_VERSION,
                                      sizeof(nkui_draw_rect_command)};
        scheduler_composite.resource = scheduler_image;
        scheduler_composite.x = 16.0f;
        scheduler_composite.y = 16.0f;
        scheduler_composite.width = 64.0f;
        scheduler_composite.height = 64.0f;
        if (!check(nkui_display_list_submit(scheduler_list,
                                            reinterpret_cast<const uint8_t *>(&scheduler_composite),
                                            sizeof(scheduler_composite)) == NKUI_OK,
                   "submit scheduler display list")) {
            result = 26;
            goto cleanup;
        }

        BlockingRenderTask blocker{};
        if (!start_blocking_render_task(blocker)) {
            result = 27;
            goto cleanup;
        }
        nk::core::reset_render_surface_api_violations();
        nk::core::set_render_surface_api_guard(true);
        nkgpu_test_forbid_surface_target_queries();
        nkui_renderer_stats before_scheduler_stats{};
        if (!check(nkui_renderer_get_stats(renderer, &before_scheduler_stats) == NKUI_OK,
                   "read pre-scheduler stats")) {
            result = 27;
            {
                std::lock_guard lock(blocker.mutex);
                blocker.release = true;
            }
            blocker.condition.notify_one();
            goto cleanup;
        }
        const nkui_frame_info scheduler_frame{sizeof(scheduler_frame),
                                              static_cast<float>(scheduler_width[0]),
                                              static_cast<float>(scheduler_height[0]),
                                              scheduler_width[0],
                                              scheduler_height[0],
                                              1.0f};
        /* Keep one render active and replace the single pending slot once.
           The third submission is deliberately newer than the second; only
           the first and third plans should execute. */
        const nk_surface scheduler_submission_surfaces[] = {
            scheduler_surfaces[0], scheduler_surfaces[1], scheduler_surfaces[1]};
        for (size_t index = 0; index < 3 && !result; ++index) {
            const nkui_result submitted = nkui_renderer_render_frame(
                renderer, scheduler_list, scheduler_submission_surfaces[index], &scheduler_frame);
            if (submitted != NKUI_OK) {
                std::fprintf(stderr,
                             "backend renderer smoke: scheduler submission %zu failed: %d\n", index,
                             submitted);
                result = 28;
            }
        }
        {
            std::lock_guard lock(blocker.mutex);
            blocker.release = true;
        }
        blocker.condition.notify_one();
        RenderTask scheduler_barrier{};
        scheduler_barrier.function = [](RenderTask &task) noexcept { task.success = true; };
        if (!result && !dispatch_render_task(scheduler_barrier))
            result = 29;
        if (!result) {
            nk_event completion_event{};
            completion_event.struct_size = sizeof(completion_event);
            if (!check(nk_poll_event(&completion_event) == NK_OK, "drain scheduler completion"))
                result = 29;
            nk_event_release(&completion_event);
        }
        if (!result &&
            (!wait_render_submission_executions(
                 renderer, before_scheduler_stats.render_submission_executions + 2) ||
             !wait_surface_frame_state(scheduler_surfaces[0], false) ||
             !wait_surface_frame_state(scheduler_surfaces[1], false)))
            result = 29;
        const uint64_t scheduler_surface_call_violations =
            nk::core::render_surface_api_violations();
        nk::core::set_render_surface_api_guard(false);
        nkgpu_test_allow_surface_target_queries();
        if (!result && !check(scheduler_surface_call_violations == 0,
                              "scheduler render surface API ownership"))
            result = 29;
        nkui_renderer_stats scheduler_stats{};
        const bool context_backend = scheduler_targets[0].api == NK_GRAPHICS_OPENGL ||
                                     scheduler_targets[0].api == NK_GRAPHICS_OPENGL_ES;
        const bool shared_native_context =
            context_backend && scheduler_targets[0].native_context != 0 &&
            scheduler_targets[0].native_context == scheduler_targets[1].native_context;
        const bool shared_native_device =
            scheduler_targets[0].native_device != 0 &&
            scheduler_targets[0].native_device == scheduler_targets[1].native_device;
        const bool shared_gpu_runtime =
            shared_native_context || (!context_backend && shared_native_device);
        const bool independent_gl_contexts = context_backend && !shared_native_context;
        const bool supports_target_switch = shared_gpu_runtime || independent_gl_contexts;
        if (!result &&
            (!check(nkui_renderer_get_stats(renderer, &scheduler_stats) == NKUI_OK,
                    "read scheduler stats") ||
             scheduler_stats.render_submissions < 2 ||
             scheduler_stats.render_submission_replacements < 1 ||
             scheduler_stats.render_submissions != before_scheduler_stats.render_submissions + 3 ||
             scheduler_stats.render_submission_replacements !=
                 before_scheduler_stats.render_submission_replacements + 1 ||
             (supports_target_switch &&
              scheduler_stats.render_submission_cancellations !=
                  before_scheduler_stats.render_submission_cancellations) ||
             (shared_gpu_runtime && scheduler_stats.render_submission_failures !=
                                        before_scheduler_stats.render_submission_failures) ||
             (shared_gpu_runtime &&
              scheduler_stats.gpu_frames != before_scheduler_stats.gpu_frames + 2) ||
             (shared_gpu_runtime &&
              scheduler_stats.resource_creations < before_scheduler_stats.resource_creations) ||
             (shared_native_context &&
              scheduler_stats.surface_recreations <
                  before_scheduler_stats.surface_recreations + 1) ||
             (supports_target_switch && scheduler_stats.render_submission_failures !=
                                            before_scheduler_stats.render_submission_failures) ||
             (supports_target_switch &&
              scheduler_stats.gpu_frames != before_scheduler_stats.gpu_frames + 2) ||
             (supports_target_switch &&
              scheduler_stats.resource_creations <= before_scheduler_stats.resource_creations) ||
             (independent_gl_contexts && scheduler_stats.surface_recreations <
                                             before_scheduler_stats.surface_recreations + 1) ||
             (!supports_target_switch &&
              scheduler_stats.render_submission_failures <
                  before_scheduler_stats.render_submission_failures + 1) ||
             (!supports_target_switch &&
              scheduler_stats.gpu_frames != before_scheduler_stats.gpu_frames) ||
             scheduler_stats.render_submission_build_ns <=
                 before_scheduler_stats.render_submission_build_ns ||
             scheduler_stats.render_submission_queue_latency_ns <=
                 before_scheduler_stats.render_submission_queue_latency_ns ||
             scheduler_stats.render_submission_execution_ns <=
                 before_scheduler_stats.render_submission_execution_ns ||
             scheduler_stats.render_submission_acquire_to_present_ns <=
                 before_scheduler_stats.render_submission_acquire_to_present_ns ||
            scheduler_stats.render_submission_executions !=
                 before_scheduler_stats.render_submission_executions + 2)) {
            std::fprintf(
                stderr,
                "backend renderer smoke: scheduler counters unexpected: submitted=%llu "
                "replaced=%llu cancelled=%llu failed=%llu executions=%llu (before submitted=%llu "
                "replaced=%llu cancelled=%llu failed=%llu executions=%llu)\n",
                static_cast<unsigned long long>(scheduler_stats.render_submissions),
                static_cast<unsigned long long>(scheduler_stats.render_submission_replacements),
                static_cast<unsigned long long>(scheduler_stats.render_submission_cancellations),
                static_cast<unsigned long long>(scheduler_stats.render_submission_failures),
                static_cast<unsigned long long>(scheduler_stats.render_submission_executions),
                static_cast<unsigned long long>(before_scheduler_stats.render_submissions),
                static_cast<unsigned long long>(
                    before_scheduler_stats.render_submission_replacements),
                static_cast<unsigned long long>(
                    before_scheduler_stats.render_submission_cancellations),
                static_cast<unsigned long long>(before_scheduler_stats.render_submission_failures),
                static_cast<unsigned long long>(
                    before_scheduler_stats.render_submission_executions));
            std::fprintf(
                stderr,
                "backend renderer smoke: scheduler GPU counters before=(frames=%llu passes=%llu "
                "draws=%llu resources=%llu recreations=%llu) after=(frames=%llu passes=%llu "
                "draws=%llu resources=%llu recreations=%llu)\n",
                static_cast<unsigned long long>(before_scheduler_stats.gpu_frames),
                static_cast<unsigned long long>(before_scheduler_stats.gpu_passes),
                static_cast<unsigned long long>(before_scheduler_stats.gpu_draw_calls),
                static_cast<unsigned long long>(before_scheduler_stats.resource_creations),
                static_cast<unsigned long long>(before_scheduler_stats.surface_recreations),
                static_cast<unsigned long long>(scheduler_stats.gpu_frames),
                static_cast<unsigned long long>(scheduler_stats.gpu_passes),
                static_cast<unsigned long long>(scheduler_stats.gpu_draw_calls),
                static_cast<unsigned long long>(scheduler_stats.resource_creations),
                static_cast<unsigned long long>(scheduler_stats.surface_recreations));
            result = 30;
        }

        /* Resize a surface while its ticket is owned by RENDER.  The backend
           must defer swapchain/drawable mutation until the frame finishes, so
           the next acquire observes the new dimensions. */
        if (!result) {
            BlockingRenderTask resize_blocker{};
            if (!start_blocking_render_task(resize_blocker)) {
                result = 36;
                goto cleanup;
            }
            if (!wait_surface_frame_state(scheduler_surfaces[0], false)) {
                result = 36;
                {
                    std::lock_guard lock(resize_blocker.mutex);
                    resize_blocker.release = true;
                }
                resize_blocker.condition.notify_one();
                goto cleanup;
            }
            nk::core::reset_render_surface_api_violations();
            nk::core::set_render_surface_api_guard(true);
            nkgpu_test_forbid_surface_target_queries();
            const int32_t previous_width = scheduler_width[0];
            const int32_t previous_height = scheduler_height[0];
            const int32_t resized_logical_width = scheduler_window_options.width + 64;
            const int32_t resized_logical_height = scheduler_window_options.height + 48;
            if (!check(nkui_renderer_render_frame(renderer, scheduler_list, scheduler_surfaces[0],
                                                  &scheduler_frame) == NKUI_OK,
                       "submit resize-boundary frame") ||
                !wait_surface_frame_state(scheduler_surfaces[0], true) ||
                !check(nkgpu_surface_resize(scheduler_surfaces[0], resized_logical_width,
                                            resized_logical_height) == NKGPU_OK,
                       "resize render-owned surface")) {
                nk::core::set_render_surface_api_guard(false);
                nkgpu_test_allow_surface_target_queries();
                {
                    std::lock_guard lock(resize_blocker.mutex);
                    resize_blocker.release = true;
                }
                resize_blocker.condition.notify_one();
                result = 36;
                goto cleanup;
            }
            {
                std::lock_guard lock(resize_blocker.mutex);
                resize_blocker.release = true;
            }
            resize_blocker.condition.notify_one();
            RenderTask resize_barrier{};
            resize_barrier.function = [](RenderTask &task) noexcept { task.success = true; };
            if (!dispatch_render_task(resize_barrier)) {
                nk::core::set_render_surface_api_guard(false);
                nkgpu_test_allow_surface_target_queries();
                result = 36;
                goto cleanup;
            }
            nk_event resize_completion{};
            resize_completion.struct_size = sizeof(resize_completion);
            if (!check(nk_poll_event(&resize_completion) == NK_OK,
                       "drain resize-boundary completion")) {
                nk::core::set_render_surface_api_guard(false);
                nkgpu_test_allow_surface_target_queries();
                result = 36;
                goto cleanup;
            }
            nk_event_release(&resize_completion);
            const uint64_t resize_surface_call_violations =
                nk::core::render_surface_api_violations();
            nk::core::set_render_surface_api_guard(false);
            nkgpu_test_allow_surface_target_queries();
            int32_t resized_width = 0;
            int32_t resized_height = 0;
            nk_surface_frame_target resized_target{};
            if (!check(resize_surface_call_violations == 0,
                       "resize-boundary render surface API ownership") ||
                !check(wait_surface_ready(scheduler_windows[0], scheduler_surfaces[0],
                                          resized_width, resized_height, resized_target),
                       "observe resize-boundary dimensions") ||
                !check(resized_width != previous_width || resized_height != previous_height,
                       "resize-boundary dimensions changed")) {
                result = 36;
                goto cleanup;
            }
            nk_surface_frame resized_frame = NK_INVALID_HANDLE;
            nk_surface_frame_target resized_frame_target{};
            resized_frame_target.struct_size = sizeof(resized_frame_target);
            if (!check(nk_surface_acquire_frame(scheduler_surfaces[0], &resized_frame,
                                                &resized_frame_target) == NK_OK,
                       "acquire resized render-owned surface") ||
                !check(resized_frame_target.width == resized_width &&
                           resized_frame_target.height == resized_height,
                       "resized render-owned frame target")) {
                if (resized_frame != NK_INVALID_HANDLE)
                    (void)nk_surface_cancel_frame(resized_frame);
                result = 36;
                goto cleanup;
            }
            if (!check(nk_surface_cancel_frame(resized_frame) == NK_OK,
                       "cancel resized render-owned surface")) {
                result = 36;
                goto cleanup;
            }
            if (!check(nkgpu_surface_resize(scheduler_surfaces[0], scheduler_window_options.width,
                                            scheduler_window_options.height) == NKGPU_OK,
                       "restore resize-boundary surface")) {
                result = 36;
                goto cleanup;
            }
            int32_t restored_width = 0;
            int32_t restored_height = 0;
            nk_surface_frame_target restored_target{};
            if (!wait_surface_ready(scheduler_windows[0], scheduler_surfaces[0], restored_width,
                                    restored_height, restored_target)) {
                result = 36;
                goto cleanup;
            }
        }

        /* A failed RENDER-to-PLATFORM completion must retain its frame ticket
           until the next platform turn can cancel it.  Force that dispatch to
           fail, then submit another frame: enqueue_render_submission() drains
           the orphan before the second acquire. */
        if (!result) {
            BlockingRenderTask orphan_blocker{};
            if (!start_blocking_render_task(orphan_blocker)) {
                result = 35;
                goto cleanup;
            }
            nkui_renderer_stats before_orphan_stats{};
            if (!check(nkui_renderer_get_stats(renderer, &before_orphan_stats) == NKUI_OK,
                       "read pre-orphan stats")) {
                {
                    std::lock_guard lock(orphan_blocker.mutex);
                    orphan_blocker.release = true;
                }
                orphan_blocker.condition.notify_one();
                result = 35;
                goto cleanup;
            }
            nk::core::reset_render_surface_api_violations();
            nk::core::set_render_surface_api_guard(true);
            nkgpu_test_forbid_surface_target_queries();
            nk::core::fail_next_platform_dispatch();
            if (!check(nkui_renderer_render_frame(renderer, scheduler_list, scheduler_surfaces[0],
                                                  &scheduler_frame) == NKUI_OK,
                       "submit orphan completion frame")) {
                nk::core::set_render_surface_api_guard(false);
                nkgpu_test_allow_surface_target_queries();
                {
                    std::lock_guard lock(orphan_blocker.mutex);
                    orphan_blocker.release = true;
                }
                orphan_blocker.condition.notify_one();
                result = 35;
                goto cleanup;
            }
            {
                std::lock_guard lock(orphan_blocker.mutex);
                orphan_blocker.release = true;
            }
            orphan_blocker.condition.notify_one();
            RenderTask orphan_barrier{};
            orphan_barrier.function = [](RenderTask &task) noexcept { task.success = true; };
            if (!dispatch_render_task(orphan_barrier)) {
                nk::core::set_render_surface_api_guard(false);
                nkgpu_test_allow_surface_target_queries();
                result = 35;
                goto cleanup;
            }

            if (!check(nkui_renderer_render_frame(renderer, scheduler_list, scheduler_surfaces[0],
                                                  &scheduler_frame) == NKUI_OK,
                       "submit post-orphan frame")) {
                nk::core::set_render_surface_api_guard(false);
                nkgpu_test_allow_surface_target_queries();
                result = 35;
                goto cleanup;
            }
            RenderTask post_orphan_barrier{};
            post_orphan_barrier.function = [](RenderTask &task) noexcept { task.success = true; };
            if (!dispatch_render_task(post_orphan_barrier)) {
                nk::core::set_render_surface_api_guard(false);
                nkgpu_test_allow_surface_target_queries();
                result = 35;
                goto cleanup;
            }
            nk_event completion_event{};
            completion_event.struct_size = sizeof(completion_event);
            if (!check(nk_poll_event(&completion_event) == NK_OK, "drain post-orphan completion")) {
                nk::core::set_render_surface_api_guard(false);
                nkgpu_test_allow_surface_target_queries();
                result = 35;
                goto cleanup;
            }
            nk_event_release(&completion_event);
            const uint64_t orphan_surface_call_violations =
                nk::core::render_surface_api_violations();
            nk::core::set_render_surface_api_guard(false);
            nkgpu_test_allow_surface_target_queries();
            nkui_renderer_stats orphan_stats{};
            nk_surface_frame probe_frame = NK_INVALID_HANDLE;
            nk_surface_frame_target probe_target{};
            probe_target.struct_size = sizeof(probe_target);
            if (!check(orphan_surface_call_violations == 0,
                       "orphan completion render surface API ownership") ||
                !check(nkui_renderer_get_stats(renderer, &orphan_stats) == NKUI_OK,
                       "read post-orphan stats") ||
                !check(orphan_stats.render_submission_failures >=
                           before_orphan_stats.render_submission_failures + 1,
                       "orphan completion failure accounting") ||
                !check(orphan_stats.render_submission_cancellations >=
                           before_orphan_stats.render_submission_cancellations + 1,
                       "orphan completion cancellation accounting") ||
                !check(nk_surface_acquire_frame(scheduler_surfaces[0], &probe_frame,
                                                &probe_target) == NK_OK,
                       "acquire after orphan completion")) {
                if (probe_frame != NK_INVALID_HANDLE)
                    (void)nk_surface_cancel_frame(probe_frame);
                result = 35;
                goto cleanup;
            }
            if (!check(nk_surface_cancel_frame(probe_frame) == NK_OK,
                       "cancel orphan probe frame")) {
                result = 35;
                goto cleanup;
            }
        }

        /* Device loss is recoverable at the scheduler boundary: the first
           submission observes the invalidated renderer, and the next sealed
           plan retires it and creates a fresh GPU renderer on RENDER. */
        if (!result) {
            if (!check(nkui_renderer_create(&recovery_renderer) == NKUI_OK,
                       "create recovery renderer")) {
                result = 31;
                goto cleanup;
            }
            nk::core::reset_render_surface_api_violations();
            nk::core::set_render_surface_api_guard(true);
            nkgpu_test_forbid_surface_target_queries();
            const nk_surface recovery_surface =
                shared_gpu_runtime ? scheduler_surfaces[1] : surface;
            const int32_t recovery_width = shared_gpu_runtime ? scheduler_width[1] : surface_width;
            const int32_t recovery_height =
                shared_gpu_runtime ? scheduler_height[1] : surface_height;
            const nkui_frame_info recovery_frame{
                sizeof(recovery_frame),
                static_cast<float>(scheduler_window_options.width),
                static_cast<float>(scheduler_window_options.height),
                recovery_width,
                recovery_height,
                1.0f};
            auto submit_and_drain = [&](nkui_renderer frame_renderer, const char *operation) {
                if (nkui_renderer_render_frame(frame_renderer, scheduler_list, recovery_surface,
                                               &recovery_frame) != NKUI_OK) {
                    std::fprintf(stderr, "backend renderer smoke: %s submission failed\n",
                                 operation);
                    return false;
                }
                RenderTask barrier{};
                barrier.function = [](RenderTask &task) noexcept { task.success = true; };
                if (!dispatch_render_task(barrier))
                    return false;
                nk_event completion_event{};
                completion_event.struct_size = sizeof(completion_event);
                if (nk_poll_event(&completion_event) != NK_OK)
                    return false;
                nk_event_release(&completion_event);
                /* Completion is delivered on PLATFORM after RENDER has
                   finished.  Pump until the ticket is closed before the
                   next submission; otherwise the deferred scheduler task
                   can remain queued behind this test's next barrier. */
                const bool closed = wait_surface_frame_state(recovery_surface, false);
                return closed;
            };
            if (!submit_and_drain(recovery_renderer, "recovery warmup") ||
                !check(nk::core::render_surface_api_violations() == 0,
                       "recovery warmup render surface API ownership")) {
                nk::core::set_render_surface_api_guard(false);
                nkgpu_test_allow_surface_target_queries();
                result = 31;
                goto cleanup;
            }
            nkui_renderer_stats before_loss_stats{};
            if (!check(nkui_renderer_get_stats(recovery_renderer, &before_loss_stats) == NKUI_OK,
                       "read pre-loss stats")) {
                result = 31;
                goto cleanup;
            }
            nkgpu_test_invalidate_all();
            nk::core::reset_render_surface_api_violations();
            if (!submit_and_drain(recovery_renderer, "device-loss") ||
                !check(nk::core::render_surface_api_violations() == 0,
                       "device-loss render surface API ownership")) {
                nk::core::set_render_surface_api_guard(false);
                nkgpu_test_allow_surface_target_queries();
                result = 31;
                goto cleanup;
            }
            nkui_renderer_stats after_loss_stats{};
            if (!check(nkui_renderer_get_stats(recovery_renderer, &after_loss_stats) == NKUI_OK,
                       "read post-loss stats") ||
                !check(after_loss_stats.render_submissions ==
                           before_loss_stats.render_submissions + 1,
                       "device-loss submission count") ||
                !check(after_loss_stats.render_submission_executions ==
                           before_loss_stats.render_submission_executions + 1,
                       "device-loss execution count")) {
                nk::core::set_render_surface_api_guard(false);
                nkgpu_test_allow_surface_target_queries();
                result = 32;
                goto cleanup;
            }
            if (!submit_and_drain(recovery_renderer, "device-loss recovery") ||
                !check(nk::core::render_surface_api_violations() == 0,
                       "recovery render surface API ownership")) {
                nk::core::set_render_surface_api_guard(false);
                nkgpu_test_allow_surface_target_queries();
                result = 33;
                goto cleanup;
            }
            nkui_renderer_stats recovered_stats{};
            nk::core::set_render_surface_api_guard(false);
            nkgpu_test_allow_surface_target_queries();
            if (!check(nkui_renderer_get_stats(recovery_renderer, &recovered_stats) == NKUI_OK,
                       "read recovered stats") ||
                !check(recovered_stats.render_submissions ==
                           before_loss_stats.render_submissions + 2,
                       "recovery submission count") ||
                !check(recovered_stats.render_submission_executions ==
                           before_loss_stats.render_submission_executions + 2,
                       "recovery execution count") ||
                !check(recovered_stats.device_losses > before_loss_stats.device_losses,
                       "device-loss accounting") ||
                !check(recovered_stats.gpu_frames >= before_loss_stats.gpu_frames + 1,
                       "recovery completed frame")) {
                std::fprintf(
                    stderr,
                    "backend renderer smoke: loss stats before=(sub=%llu exec=%llu "
                    "fail=%llu passes=%llu draws=%llu frames=%llu losses=%llu) "
                    "after=(sub=%llu exec=%llu fail=%llu passes=%llu draws=%llu "
                    "frames=%llu losses=%llu) recovered=(sub=%llu exec=%llu fail=%llu "
                    "passes=%llu draws=%llu frames=%llu losses=%llu)\n",
                    static_cast<unsigned long long>(before_loss_stats.render_submissions),
                    static_cast<unsigned long long>(before_loss_stats.render_submission_executions),
                    static_cast<unsigned long long>(before_loss_stats.render_submission_failures),
                    static_cast<unsigned long long>(before_loss_stats.gpu_passes),
                    static_cast<unsigned long long>(before_loss_stats.gpu_draw_calls),
                    static_cast<unsigned long long>(before_loss_stats.gpu_frames),
                    static_cast<unsigned long long>(before_loss_stats.device_losses),
                    static_cast<unsigned long long>(after_loss_stats.render_submissions),
                    static_cast<unsigned long long>(after_loss_stats.render_submission_executions),
                    static_cast<unsigned long long>(after_loss_stats.render_submission_failures),
                    static_cast<unsigned long long>(after_loss_stats.gpu_passes),
                    static_cast<unsigned long long>(after_loss_stats.gpu_draw_calls),
                    static_cast<unsigned long long>(after_loss_stats.gpu_frames),
                    static_cast<unsigned long long>(after_loss_stats.device_losses),
                    static_cast<unsigned long long>(recovered_stats.render_submissions),
                    static_cast<unsigned long long>(recovered_stats.render_submission_executions),
                    static_cast<unsigned long long>(recovered_stats.render_submission_failures),
                    static_cast<unsigned long long>(recovered_stats.gpu_passes),
                    static_cast<unsigned long long>(recovered_stats.gpu_draw_calls),
                    static_cast<unsigned long long>(recovered_stats.gpu_frames),
                    static_cast<unsigned long long>(recovered_stats.device_losses));
                result = 34;
                goto cleanup;
            }
        }
    }
cleanup:
    nk::core::set_render_surface_api_guard(false);
    print_render_metrics(renderer);
    if (renderer.id && nkui_renderer_destroy(renderer) != NKUI_OK)
        result = result ? result : 20;
    if (recovery_renderer.id && nkui_renderer_destroy(recovery_renderer) != NKUI_OK)
        result = result ? result : 20;
    if (scheduler_list.id && nkui_display_list_destroy(scheduler_list) != NKUI_OK)
        result = result ? result : 21;
    if (list.id && nkui_display_list_destroy(list) != NKUI_OK)
        result = result ? result : 22;
    if (imported_surface.id && nkui_resource_destroy(imported_surface) != NKUI_OK)
        result = result ? result : 23;
    if (scheduler_image.id && nkui_resource_destroy(scheduler_image) != NKUI_OK)
        result = result ? result : 23;
    if (producer.id) {
        destroy_task.surface = surface;
        destroy_task.producer = producer;
        destroy_task.image_resource = image_resource;
        destroy_task.function = &destroy_offscreen_resources;
        if (!dispatch_render_task(destroy_task))
            result = result ? result : 24;
    }
    if (surface && nk_surface_destroy(surface) != NK_OK)
        result = result ? result : 25;
    if (window && nk_window_destroy(window) != NK_OK)
        result = result ? result : 26;
    /* Surface one shares surface zero's GL context, so release the dependent
       before destroying its shared parent. */
    for (size_t index = 2; index-- > 0;) {
        if (scheduler_surfaces[index] && nk_surface_destroy(scheduler_surfaces[index]) != NK_OK)
            result = result ? result : 27;
    }
    for (size_t index = 0; index < 2; ++index) {
        if (scheduler_windows[index] && nk_window_destroy(scheduler_windows[index]) != NK_OK)
            result = result ? result : 28;
    }
    if (initialized)
        nk_shutdown();
    if (!result && !run_shutdown_orphan_completion_test())
        result = 35;
    return result;
}
