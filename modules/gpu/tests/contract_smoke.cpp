#include "nativekit.h"
#include "nativekit_gpu.h"
#include "nativekit_window.h"
#include "testing.h"

#include <chrono>
#include <cstdio>
#include <thread>

#define EXPECT_RESULT(expression, expected)                                                         \
    do {                                                                                            \
        const nkgpu_result actual_result = (expression);                                            \
        if (actual_result != (expected)) {                                                          \
            std::fprintf(stderr, "%s returned %d, expected %d: %s\n", #expression, actual_result, \
                         (expected), nkgpu_last_error());                                            \
            result = __LINE__;                                                                      \
            goto cleanup;                                                                           \
        }                                                                                           \
    } while (0)

int main() {
    nk_init_options init{};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    if (nk_init(&init) != NK_OK)
        return 1;

    int result = 0;
    bool window_created = false;
    bool surface_created = false;
    bool other_window_created = false;
    bool other_surface_created = false;
    nk_window_options window_options{};
    window_options.struct_size = sizeof(window_options);
    window_options.width = 192;
    window_options.height = 128;
    window_options.title = "NativeKit GPU contract";
    nk_window window = 0;
    nk_surface surface = 0;
    nk_window other_window = 0;
    nk_surface other_surface = 0;
    nkgpu_renderer first{};
    nkgpu_renderer second{};
    nkgpu_renderer foreign_renderer{};
    nkgpu_buffer buffer{};
    nkgpu_buffer_builder unfinished_buffer{};
    nkgpu_shader shader{};
    nkgpu_shader_builder shader_builder{};
    nkgpu_pipeline_builder unfinished_pipeline{};
    nkgpu_pipeline pipeline{};
    nkgpu_render_target target{};
    nkgpu_render_target lost_target{};
    nk_graphics_image retained_image{};
    nk_graphics_image foreign_image{};
    const uint8_t buffer_data[] = {0, 0, 0, 0};
    bool gles = false;
    const char *vertex_source = nullptr;
    const char *fragment_source = nullptr;

    if (nk_window_create(&window_options, &window) != NK_OK) {
        result = 2;
        goto cleanup;
    }
    window_created = true;
    if (nkgpu_surface_create(window, window_options.width, window_options.height, &surface) !=
        NKGPU_OK) {
        std::fprintf(stderr, "surface creation failed: %s\n", nkgpu_last_error());
        result = 3;
        goto cleanup;
    }
    surface_created = true;

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
            std::fprintf(stderr, "surface did not become ready\n");
            result = 5;
            goto cleanup;
        }
    }

    EXPECT_RESULT(nkgpu_renderer_create(surface, &first), NKGPU_OK);
    EXPECT_RESULT(nkgpu_renderer_create(surface, &second), NKGPU_OK);

#if defined(NK_GPU_TEST_BACKEND_MATRIX)
    {
        nk_window_options other_options = window_options;
        other_options.title = "NativeKit GPU foreign-device test";
        if (nk_window_create(&other_options, &other_window) != NK_OK) {
            result = 6;
            goto cleanup;
        }
        other_window_created = true;
        const nk_graphics_api foreign_api = nkgpu_query_graphics_api(first) == NK_GRAPHICS_OPENGL
                                                ? NK_GRAPHICS_OPENGL_ES
                                                : NK_GRAPHICS_OPENGL;
        if (nkgpu_surface_create_for_api(other_window, foreign_api, other_options.width,
                                         other_options.height, &other_surface) != NKGPU_OK) {
            std::fprintf(stderr, "foreign surface creation failed: %s\n", nkgpu_last_error());
            result = 7;
            goto cleanup;
        }
        other_surface_created = true;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        bool ready = false;
        while (!ready && std::chrono::steady_clock::now() < deadline) {
            nk_event event{};
            event.struct_size = sizeof(event);
            if (nk_poll_event(&event) != NK_OK) {
                result = 8;
                goto cleanup;
            }
            ready = event.kind == NK_EVENT_SURFACE_READY && event.source == other_surface;
            nk_event_release(&event);
            if (!ready)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (!ready) {
            std::fprintf(stderr, "foreign surface did not become ready\n");
            result = 9;
            goto cleanup;
        }
        EXPECT_RESULT(nkgpu_renderer_create(other_surface, &foreign_renderer), NKGPU_OK);
        nkgpu_render_target foreign_target{};
        EXPECT_RESULT(nkgpu_render_target_create(foreign_renderer, 8, 8, 0, &foreign_target),
                      NKGPU_OK);
        EXPECT_RESULT(nkgpu_render_target_get_image(foreign_renderer, foreign_target,
                                                    &foreign_image),
                      NKGPU_OK);
        EXPECT_RESULT(nk_graphics_image_retain(foreign_image), NK_OK);
        EXPECT_RESULT(nkgpu_render_target_destroy(foreign_renderer, foreign_target), NKGPU_OK);
        EXPECT_RESULT(nkgpu_frame_begin(first), NKGPU_OK);
        EXPECT_RESULT(nkgpu_begin_window_pass(first, window_options.width,
                                              window_options.height, 1),
                      NKGPU_OK);
        EXPECT_RESULT(nkgpu_apply_graphics_image(first, 0, foreign_image),
                      NKGPU_ERROR_INVALID_HANDLE);
        EXPECT_RESULT(nkgpu_end_frame(first), NKGPU_OK);
        EXPECT_RESULT(nk_graphics_image_release(foreign_image), NK_OK);
        foreign_image = {};
        EXPECT_RESULT(nkgpu_renderer_destroy(foreign_renderer), NKGPU_OK);
        foreign_renderer = {};
    }
#endif

    EXPECT_RESULT(nkgpu_end_frame(first), NKGPU_ERROR_WRONG_STATE);
    EXPECT_RESULT(nkgpu_draw(first, 0, 3, 1), NKGPU_ERROR_WRONG_STATE);
    EXPECT_RESULT(nkgpu_begin_frame(first), NKGPU_OK);
    EXPECT_RESULT(nkgpu_begin_frame(second), NKGPU_ERROR_WRONG_STATE);
    EXPECT_RESULT(nkgpu_end_render_target(first), NKGPU_ERROR_WRONG_STATE);
    EXPECT_RESULT(nkgpu_renderer_destroy(first), NKGPU_ERROR_WRONG_STATE);
    EXPECT_RESULT(nkgpu_render_target_create(first, 16, 16, 0, &target), NKGPU_ERROR_WRONG_STATE);
    EXPECT_RESULT(nkgpu_buffer_create(first, buffer_data, sizeof(buffer_data), &buffer),
                  NKGPU_ERROR_WRONG_STATE);
    EXPECT_RESULT(nkgpu_end_frame(first), NKGPU_OK);
    {
        nkgpu_renderer_state state = NKGPU_RENDERER_LOST;
        EXPECT_RESULT(nkgpu_renderer_get_state(first, &state), NKGPU_OK);
        if (state != NKGPU_RENDERER_READY) {
            result = __LINE__;
            goto cleanup;
        }
    }

    EXPECT_RESULT(nkgpu_render_target_create(first, 16, 16, 0, &target), NKGPU_OK);
    EXPECT_RESULT(nkgpu_begin_render_target(second, target, 1), NKGPU_ERROR_INVALID_HANDLE);
    EXPECT_RESULT(nkgpu_begin_render_target(first, target, 2), NKGPU_ERROR_INVALID_ARGUMENT);
    EXPECT_RESULT(nkgpu_begin_render_target(first, target, 1), NKGPU_OK);
    {
        nkgpu_renderer_state state = NKGPU_RENDERER_READY;
        EXPECT_RESULT(nkgpu_renderer_get_state(first, &state), NKGPU_OK);
        if (state != NKGPU_RENDERER_RENDER_TARGET_ACTIVE) {
            result = __LINE__;
            goto cleanup;
        }
    }
    EXPECT_RESULT(nkgpu_begin_frame(first), NKGPU_ERROR_WRONG_STATE);
    EXPECT_RESULT(nkgpu_end_frame(first), NKGPU_ERROR_WRONG_STATE);
    EXPECT_RESULT(nkgpu_render_target_destroy(first, target), NKGPU_ERROR_WRONG_STATE);
    EXPECT_RESULT(nkgpu_end_render_target(first), NKGPU_OK);
    EXPECT_RESULT(nkgpu_render_target_destroy(first, target), NKGPU_OK);

    for (int iteration = 0; iteration < 1000; ++iteration) {
        nk_graphics_image borrowed{};
        nk_graphics_image_info info{};
        info.struct_size = sizeof(info);
        EXPECT_RESULT(nkgpu_render_target_create(first, 8, 8, 1, &target), NKGPU_OK);
        EXPECT_RESULT(nkgpu_render_target_get_image(first, target, &borrowed), NKGPU_OK);
        EXPECT_RESULT(nk_graphics_image_retain(borrowed), NKGPU_OK);
        retained_image = borrowed;
        EXPECT_RESULT(nkgpu_render_target_destroy(first, target), NKGPU_OK);
        EXPECT_RESULT(nk_graphics_image_get_info(retained_image, &info), NKGPU_OK);
        if (info.width != 8 || info.height != 8 || !info.device.id) {
            result = __LINE__;
            goto cleanup;
        }
        EXPECT_RESULT(nk_graphics_image_release(retained_image), NKGPU_OK);
        retained_image = {};
    }

    {
        nkgpu_renderer_stats stats{};
        EXPECT_RESULT(nkgpu_renderer_get_stats(first, &stats), NKGPU_OK);
        if (stats.struct_size != sizeof(stats) || stats.render_targets_live != 0 ||
            stats.resource_creations < 1000 || stats.resource_destructions < 1000 ||
            stats.render_target_bytes != 0) {
            result = __LINE__;
            goto cleanup;
        }
    }

    {
        const uint8_t pixel[] = {255, 255, 255, 255};
        nkgpu_image failed_image{};
        nkgpu_buffer failed_buffer{};
        nkgpu_test_fail_next_image_creation();
        EXPECT_RESULT(nkgpu_image_create(first, 1, 1, NKGPU_IMAGEFORMAT_RGBA8, pixel,
                                         sizeof(pixel), 0, &failed_image),
                      NKGPU_ERROR_OUT_OF_MEMORY);
        nkgpu_test_fail_next_buffer_creation();
        EXPECT_RESULT(nkgpu_buffer_create(first, pixel, sizeof(pixel), &failed_buffer),
                      NKGPU_ERROR_OUT_OF_MEMORY);
        nkgpu_renderer_stats stats{};
        EXPECT_RESULT(nkgpu_renderer_get_stats(first, &stats), NKGPU_OK);
        if (stats.failed_allocations < 2) {
            result = __LINE__;
            goto cleanup;
        }
    }

    EXPECT_RESULT(nkgpu_surface_resize(surface, 240, 160), NKGPU_OK);
    {
        nkgpu_renderer_state state = NKGPU_RENDERER_LOST;
        EXPECT_RESULT(nkgpu_renderer_get_state(first, &state), NKGPU_OK);
        if (state != NKGPU_RENDERER_READY) {
            result = __LINE__;
            goto cleanup;
        }
    }

    EXPECT_RESULT(nkgpu_buffer_create(first, buffer_data, sizeof(buffer_data), &buffer), NKGPU_OK);
    EXPECT_RESULT(nkgpu_buffer_destroy(second, buffer), NKGPU_ERROR_INVALID_HANDLE);
    EXPECT_RESULT(nkgpu_shader_create(first, 0, "void main(){}", "void main(){}", &shader),
                  NKGPU_ERROR_INVALID_ARGUMENT);
    EXPECT_RESULT(nkgpu_shader_begin(first, 0, "void main(){}", "void main(){}", &shader_builder),
                  NKGPU_ERROR_INVALID_ARGUMENT);

    gles = nkgpu_query_graphics_api(first) == NK_GRAPHICS_OPENGL_ES;
    vertex_source = gles ? "#version 300 es\nvoid main(){gl_Position=vec4(0.0);}\n"
                         : "#version 330\nvoid main(){gl_Position=vec4(0.0);}\n";
    fragment_source =
        gles ? "#version 300 es\nprecision mediump float; out vec4 c; void main(){c=vec4(1.0);}\n"
             : "#version 330\nout vec4 c; void main(){c=vec4(1.0);}\n";
    EXPECT_RESULT(nkgpu_shader_create(first, NKGPU_SHADERLANGUAGE_GLSL, vertex_source,
                                      fragment_source, &shader),
                  NKGPU_OK);
    EXPECT_RESULT(nkgpu_shader_begin(first, NKGPU_SHADERLANGUAGE_GLSL, vertex_source,
                                     fragment_source, &shader_builder),
                  NKGPU_OK);
    EXPECT_RESULT(nkgpu_shader_end(shader_builder, &shader), NKGPU_OK);
    EXPECT_RESULT(nkgpu_pipeline_begin(second, shader, 4, &unfinished_pipeline),
                  NKGPU_ERROR_INVALID_HANDLE);
    EXPECT_RESULT(nkgpu_pipeline_begin(first, shader, 4, &unfinished_pipeline), NKGPU_OK);
    EXPECT_RESULT(nkgpu_buffer_begin(first, 16, &unfinished_buffer), NKGPU_OK);

    EXPECT_RESULT(nkgpu_render_target_create(second, 8, 8, 0, &lost_target), NKGPU_OK);
    EXPECT_RESULT(nkgpu_test_lose_after_frames(second, 1), NKGPU_OK);
    EXPECT_RESULT(nkgpu_begin_frame(second), NKGPU_OK);
    EXPECT_RESULT(nkgpu_end_frame(second), NKGPU_OK);
    {
        nkgpu_renderer_state state = NKGPU_RENDERER_READY;
        EXPECT_RESULT(nkgpu_renderer_get_state(second, &state), NKGPU_OK);
        if (state != NKGPU_RENDERER_LOST) {
            result = __LINE__;
            goto cleanup;
        }
    }
    EXPECT_RESULT(nkgpu_begin_frame(second), NKGPU_ERROR_DEVICE_LOST);
    EXPECT_RESULT(nkgpu_buffer_create_stream(second, 16, NKGPU_BUFFER_VERTEX, &buffer),
                  NKGPU_ERROR_DEVICE_LOST);
    {
        const uint8_t pixel[] = {255, 255, 255, 255};
        nkgpu_image lost_image{};
        nkgpu_render_target new_target{};
        EXPECT_RESULT(nkgpu_image_create(second, 1, 1, NKGPU_IMAGEFORMAT_RGBA8, pixel,
                                         sizeof(pixel), 0, &lost_image),
                      NKGPU_ERROR_DEVICE_LOST);
        EXPECT_RESULT(nkgpu_render_target_create(second, 8, 8, 0, &new_target),
                      NKGPU_ERROR_DEVICE_LOST);
    }
    EXPECT_RESULT(nkgpu_draw(second, 0, 3, 1), NKGPU_ERROR_DEVICE_LOST);
    EXPECT_RESULT(nkgpu_render_target_destroy(second, lost_target), NKGPU_OK);
    lost_target = {};
    {
        nkgpu_renderer_stats stats{};
        EXPECT_RESULT(nkgpu_renderer_get_stats(second, &stats), NKGPU_OK);
        if (stats.device_losses != 1 || stats.render_targets_live != 0) {
            result = __LINE__;
            goto cleanup;
        }
    }
    EXPECT_RESULT(nkgpu_renderer_destroy(second), NKGPU_OK);
    second = {};

    {
        nkgpu_renderer third{};
        EXPECT_RESULT(nkgpu_renderer_create(surface, &third), NKGPU_OK);
        EXPECT_RESULT(nkgpu_test_invalidate_surface(third), NKGPU_OK);
        EXPECT_RESULT(nkgpu_begin_frame(third), NKGPU_ERROR_DEVICE_LOST);
        EXPECT_RESULT(nkgpu_renderer_destroy(third), NKGPU_OK);
    }

    EXPECT_RESULT(nkgpu_render_target_create(first, 16, 16, 0, &target), NKGPU_OK);
    EXPECT_RESULT(nkgpu_render_target_get_image(first, target, &retained_image), NKGPU_OK);
    EXPECT_RESULT(nk_graphics_image_retain(retained_image), NK_OK);
    EXPECT_RESULT(nkgpu_render_target_destroy(first, target), NKGPU_OK);
    nkgpu_test_fail_next_present();
    EXPECT_RESULT(nkgpu_begin_frame(first), NKGPU_OK);
    EXPECT_RESULT(nkgpu_end_frame(first), NKGPU_ERROR_DEVICE_LOST);
    EXPECT_RESULT(nkgpu_renderer_destroy(first), NKGPU_OK);
    first = {};
    EXPECT_RESULT(nkgpu_buffer_destroy(first, buffer), NKGPU_ERROR_INVALID_HANDLE);
    EXPECT_RESULT(nkgpu_buffer_end(unfinished_buffer, &buffer), NKGPU_ERROR_INVALID_HANDLE);
    EXPECT_RESULT(nkgpu_pipeline_end(unfinished_pipeline, &pipeline), NKGPU_ERROR_INVALID_HANDLE);
    EXPECT_RESULT(nkgpu_render_target_destroy(first, target), NKGPU_ERROR_INVALID_HANDLE);
    {
        nk_graphics_image_info info{};
        info.struct_size = sizeof(info);
        EXPECT_RESULT(nk_graphics_image_get_info(retained_image, &info), NK_OK);
        const nk_graphics_image stale_image = retained_image;
        nkgpu_renderer recreated{};
        EXPECT_RESULT(nkgpu_renderer_create(surface, &recreated), NKGPU_OK);
        EXPECT_RESULT(nkgpu_begin_frame(recreated), NKGPU_OK);
        EXPECT_RESULT(nkgpu_apply_graphics_image(recreated, 0, retained_image), NKGPU_OK);
        EXPECT_RESULT(nkgpu_end_frame(recreated), NKGPU_OK);
        EXPECT_RESULT(nkgpu_renderer_destroy(recreated), NKGPU_OK);
        EXPECT_RESULT(nk_graphics_image_release(retained_image), NK_OK);
        retained_image = {};
        EXPECT_RESULT(nk_graphics_image_get_info(stale_image, &info), NK_ERROR_INVALID_HANDLE);
    }

cleanup:
    if (retained_image.id)
        nk_graphics_image_release(retained_image);
    if (foreign_image.id)
        nk_graphics_image_release(foreign_image);
    if (first.id) {
        nkgpu_end_render_target(first);
        nkgpu_end_frame(first);
        nkgpu_renderer_destroy(first);
    }
    if (second.id)
        nkgpu_renderer_destroy(second);
    if (foreign_renderer.id)
        nkgpu_renderer_destroy(foreign_renderer);
    if (other_surface_created)
        nk_surface_destroy(other_surface);
    if (other_window_created)
        nk_window_destroy(other_window);
    if (surface_created)
        nk_surface_destroy(surface);
    if (window_created)
        nk_window_destroy(window);
    nk_shutdown();
    return result;
}

#undef EXPECT_RESULT
