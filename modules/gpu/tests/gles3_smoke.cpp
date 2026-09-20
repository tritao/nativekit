#include "nativekit.h"
#include "nativekit_gpu.h"
#include "nativekit_window.h"

#include <GLES3/gl3.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>

namespace {

struct Resources {
    bool initialized = false;
    nk_window window = 0;
    nk_surface surface = 0;
    nkgpu_renderer renderer{};
    nkgpu_shader shader{};
    nkgpu_pipeline pipeline{};
    nkgpu_buffer buffer{};

    ~Resources() {
        if (renderer.id) {
            nkgpu_end_pass(renderer);
            nkgpu_end_frame(renderer);
        }
        if (buffer.id && renderer.id)
            nkgpu_buffer_destroy(renderer, buffer);
        if (pipeline.id && renderer.id)
            nkgpu_pipeline_destroy(renderer, pipeline);
        if (shader.id && renderer.id)
            nkgpu_shader_destroy(renderer, shader);
        if (renderer.id)
            nkgpu_renderer_destroy(renderer);
        if (surface)
            nk_surface_destroy(surface);
        if (window)
            nk_window_destroy(window);
        if (initialized)
            nk_shutdown();
    }
};

bool wait_for_surface(nk_surface surface) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        nk_event event{};
        event.struct_size = sizeof(event);
        if (nk_poll_event(&event) != NK_OK)
            return false;
        const bool ready = event.kind == NK_EVENT_SURFACE_READY && event.source == surface;
        nk_event_release(&event);
        if (ready)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

bool expect(nkgpu_result actual, nkgpu_result expected, const char *expression) {
    if (actual == expected)
        return true;
    std::fprintf(stderr, "%s returned %d, expected %d: %s\n", expression, actual, expected,
                 nkgpu_last_error());
    return false;
}

} // namespace

int main() {
    Resources resources;
    nk_init_options init{};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    if (nk_init(&init) != NK_OK)
        return 1;
    resources.initialized = true;

    nk_window_options window_options{};
    window_options.struct_size = sizeof(window_options);
    window_options.width = 64;
    window_options.height = 64;
    window_options.title = "NativeKit GLES3 smoke";
    if (nk_window_create(&window_options, &resources.window) != NK_OK ||
        nkgpu_surface_create_for_api(resources.window, NK_GRAPHICS_OPENGL_ES,
                                      window_options.width, window_options.height,
                                      &resources.surface) != NKGPU_OK ||
        !wait_for_surface(resources.surface)) {
        std::fprintf(stderr, "GLES3 surface did not become ready: %s\n", nkgpu_last_error());
        return 2;
    }
    if (!expect(nkgpu_renderer_create(resources.surface, &resources.renderer), NKGPU_OK,
                "nkgpu_renderer_create") ||
        nkgpu_query_graphics_api(resources.renderer) != NK_GRAPHICS_OPENGL_ES ||
        nkgpu_query_backend(resources.renderer) != NKGPU_BACKEND_GLES3) {
        std::fprintf(stderr, "renderer did not select the GLES3 backend\n");
        return 3;
    }

    const char *vertex_source =
        "#version 300 es\n"
        "layout(location=0) in vec2 position;\n"
        "void main(){gl_Position=vec4(position,0.0,1.0);}\n";
    const char *fragment_source =
        "#version 300 es\n"
        "precision mediump float;\n"
        "out vec4 frag_color;\n"
        "void main(){frag_color=vec4(0.8,0.1,0.02,1.0);}\n";
    if (!expect(nkgpu_shader_create(resources.renderer, NKGPU_SHADERLANGUAGE_GLSL,
                                    vertex_source, fragment_source, &resources.shader),
                NKGPU_OK, "nkgpu_shader_create"))
        return 4;

    nkgpu_pipeline_builder pipeline_builder{};
    if (!expect(nkgpu_pipeline_begin(resources.renderer, resources.shader, 2 * sizeof(float),
                                     &pipeline_builder),
                NKGPU_OK, "nkgpu_pipeline_begin") ||
        !expect(nkgpu_pipeline_attribute(pipeline_builder, 0, 0, 0, NKGPU_VERTEXFORMAT_FLOAT2),
                NKGPU_OK, "nkgpu_pipeline_attribute") ||
        !expect(nkgpu_pipeline_end(pipeline_builder, &resources.pipeline), NKGPU_OK,
                "nkgpu_pipeline_end"))
        return 5;

    const float vertices[] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
    if (!expect(nkgpu_buffer_create(resources.renderer,
                                    reinterpret_cast<const uint8_t *>(vertices), sizeof(vertices),
                                    &resources.buffer),
                NKGPU_OK, "nkgpu_buffer_create"))
        return 6;

    if (!expect(nkgpu_begin_frame(resources.renderer), NKGPU_OK, "nkgpu_begin_frame") ||
        !expect(nkgpu_apply_pipeline(resources.renderer, resources.pipeline), NKGPU_OK,
                "nkgpu_apply_pipeline") ||
        !expect(nkgpu_apply_vertex_buffer(resources.renderer, 0, resources.buffer, 0), NKGPU_OK,
                "nkgpu_apply_vertex_buffer") ||
        !expect(nkgpu_draw(resources.renderer, 0, 3, 1), NKGPU_OK, "nkgpu_draw") ||
        !expect(nkgpu_end_pass(resources.renderer), NKGPU_OK, "nkgpu_end_pass"))
        return 7;

    uint8_t pixel[4]{};
    glReadPixels(window_options.width / 2, window_options.height / 2, 1, 1, GL_RGBA,
                 GL_UNSIGNED_BYTE, pixel);
    if (pixel[0] < 180 || pixel[1] > 60 || pixel[2] > 40 || pixel[3] < 240) {
        std::fprintf(stderr, "GLES3 render pixel was (%u,%u,%u,%u)\n", pixel[0], pixel[1],
                     pixel[2], pixel[3]);
        return 8;
    }
    if (!expect(nkgpu_end_frame(resources.renderer), NKGPU_OK, "nkgpu_end_frame"))
        return 9;
    return 0;
}
