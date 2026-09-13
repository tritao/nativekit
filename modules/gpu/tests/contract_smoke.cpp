#include "nativekit.h"
#include "nativekit_gpu.h"
#include "nativekit_window.h"

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
    nk_window_options window_options{};
    window_options.struct_size = sizeof(window_options);
    window_options.width = 192;
    window_options.height = 128;
    window_options.title = "NativeKit GPU contract";
    nk_handle window = NK_INVALID_HANDLE;
    nk_handle surface = NK_INVALID_HANDLE;
    nkgpu_renderer first{};
    nkgpu_renderer second{};
    nkgpu_buffer buffer{};
    nkgpu_buffer_builder unfinished_buffer{};
    nkgpu_shader shader{};
    nkgpu_shader_builder shader_builder{};
    nkgpu_pipeline_builder unfinished_pipeline{};
    nkgpu_pipeline pipeline{};
    nkgpu_render_target target{};
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

    EXPECT_RESULT(nkgpu_render_target_create(first, 16, 16, 0, &target), NKGPU_OK);
    EXPECT_RESULT(nkgpu_begin_render_target(second, target, 1), NKGPU_ERROR_INVALID_HANDLE);
    EXPECT_RESULT(nkgpu_begin_render_target(first, target, 2), NKGPU_ERROR_INVALID_ARGUMENT);
    EXPECT_RESULT(nkgpu_begin_render_target(first, target, 1), NKGPU_OK);
    EXPECT_RESULT(nkgpu_begin_frame(first), NKGPU_ERROR_WRONG_STATE);
    EXPECT_RESULT(nkgpu_end_frame(first), NKGPU_ERROR_WRONG_STATE);
    EXPECT_RESULT(nkgpu_render_target_destroy(first, target), NKGPU_ERROR_WRONG_STATE);
    EXPECT_RESULT(nkgpu_end_render_target(first), NKGPU_OK);

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

    EXPECT_RESULT(nkgpu_renderer_destroy(first), NKGPU_OK);
    first = {};
    EXPECT_RESULT(nkgpu_buffer_destroy(first, buffer), NKGPU_ERROR_INVALID_HANDLE);
    EXPECT_RESULT(nkgpu_buffer_end(unfinished_buffer, &buffer), NKGPU_ERROR_INVALID_HANDLE);
    EXPECT_RESULT(nkgpu_pipeline_end(unfinished_pipeline, &pipeline), NKGPU_ERROR_INVALID_HANDLE);
    EXPECT_RESULT(nkgpu_render_target_destroy(first, target), NKGPU_ERROR_INVALID_HANDLE);
    EXPECT_RESULT(nkgpu_renderer_destroy(second), NKGPU_OK);
    second = {};

cleanup:
    if (first.id) {
        nkgpu_end_render_target(first);
        nkgpu_end_frame(first);
        nkgpu_renderer_destroy(first);
    }
    if (second.id)
        nkgpu_renderer_destroy(second);
    if (surface_created)
        nk_surface_destroy(surface);
    if (window_created)
        nk_window_destroy(window);
    nk_shutdown();
    return result;
}

#undef EXPECT_RESULT
