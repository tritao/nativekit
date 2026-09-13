#include "nativekit.h"
#include "nativekit_gpu.h"
#include "nativekit_time.h"
#include "nativekit_window.h"

#include <stdio.h>

typedef struct triangle_renderer {
    nkgpu_renderer renderer;
    nkgpu_buffer vertices;
    nkgpu_shader shader;
    nkgpu_pipeline pipeline;
} triangle_renderer;

static int report_gpu_error(const char *operation, nkgpu_result result) {
    if (result == NKGPU_OK)
        return 0;
    fprintf(stderr, "%s failed (%d): %s\n", operation, result, nkgpu_last_error());
    return 1;
}

static int triangle_renderer_create(nk_handle surface, triangle_renderer *graphics) {
    static const float vertex_data[] = {
        -0.72f, -0.62f, 0.96f, 0.30f, 0.24f,
         0.72f, -0.62f, 0.26f, 0.82f, 0.43f,
         0.00f,  0.72f, 0.25f, 0.48f, 0.98f,
    };
    static const char vertex_gl[] =
        "#version 330\n"
        "layout(location=0) in vec2 position;\n"
        "layout(location=1) in vec3 color;\n"
        "out vec3 vertex_color;\n"
        "void main(){ vertex_color=color; gl_Position=vec4(position,0.0,1.0); }\n";
    static const char fragment_gl[] =
        "#version 330\n"
        "in vec3 vertex_color;\n"
        "out vec4 fragment_color;\n"
        "void main(){ fragment_color=vec4(vertex_color,1.0); }\n";
    static const char vertex_gles[] =
        "#version 300 es\n"
        "layout(location=0) in vec2 position;\n"
        "layout(location=1) in vec3 color;\n"
        "out vec3 vertex_color;\n"
        "void main(){ vertex_color=color; gl_Position=vec4(position,0.0,1.0); }\n";
    static const char fragment_gles[] =
        "#version 300 es\n"
        "precision mediump float;\n"
        "in vec3 vertex_color;\n"
        "out vec4 fragment_color;\n"
        "void main(){ fragment_color=vec4(vertex_color,1.0); }\n";
    const int gles = nkgpu_query_graphics_api(graphics->renderer) == NK_GRAPHICS_OPENGL_ES;
    nkgpu_pipeline_builder builder = {0};

    if (report_gpu_error("nkgpu_renderer_create",
                         nkgpu_renderer_create(surface, &graphics->renderer)))
        return 0;
    if (report_gpu_error("nkgpu_buffer_create",
                         nkgpu_buffer_create(graphics->renderer,
                                             (const uint8_t *)vertex_data,
                                             sizeof(vertex_data), &graphics->vertices)) ||
        report_gpu_error("nkgpu_shader_create",
                         nkgpu_shader_create(graphics->renderer, NKGPU_SHADERLANGUAGE_GLSL,
                                             gles ? vertex_gles : vertex_gl,
                                             gles ? fragment_gles : fragment_gl,
                                             &graphics->shader)) ||
        report_gpu_error("nkgpu_pipeline_begin",
                         nkgpu_pipeline_begin(graphics->renderer, graphics->shader,
                                              5u * (uint32_t)sizeof(float), &builder)) ||
        report_gpu_error("nkgpu_pipeline_attribute(position)",
                         nkgpu_pipeline_attribute(builder, 0, 0, 0,
                                                  NKGPU_VERTEXFORMAT_FLOAT2)) ||
        report_gpu_error("nkgpu_pipeline_attribute(color)",
                         nkgpu_pipeline_attribute(builder, 1, 0,
                                                  2u * (uint32_t)sizeof(float),
                                                  NKGPU_VERTEXFORMAT_FLOAT3)) ||
        report_gpu_error("nkgpu_pipeline_end",
                         nkgpu_pipeline_end(builder, &graphics->pipeline)))
        return 0;
    return 1;
}

static int triangle_renderer_draw(triangle_renderer *graphics) {
    if (report_gpu_error("nkgpu_begin_frame", nkgpu_begin_frame(graphics->renderer)))
        return 0;
    if (report_gpu_error("nkgpu_apply_pipeline",
                         nkgpu_apply_pipeline(graphics->renderer, graphics->pipeline)) ||
        report_gpu_error("nkgpu_apply_vertex_buffer",
                         nkgpu_apply_vertex_buffer(graphics->renderer, 0,
                                                   graphics->vertices, 0)) ||
        report_gpu_error("nkgpu_draw", nkgpu_draw(graphics->renderer, 0, 3, 1))) {
        nkgpu_end_frame(graphics->renderer);
        return 0;
    }
    return !report_gpu_error("nkgpu_end_frame", nkgpu_end_frame(graphics->renderer));
}

int main(void) {
    nk_init_options init = {0};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    if (nk_init(&init) != NK_OK) {
        fprintf(stderr, "nk_init failed: %s\n", nk_last_error());
        return 1;
    }

    int result = 0;
    int running = 1;
    int ready = 0;
    int window_created = 0;
    int surface_created = 0;
    nk_window_options window_options = {0};
    window_options.struct_size = sizeof(window_options);
    window_options.flags = NK_WINDOW_RESIZABLE;
    window_options.width = 800;
    window_options.height = 600;
    window_options.title = "NativeKit GPU Triangle";
    nk_handle window = NK_INVALID_HANDLE;
    nk_handle surface = NK_INVALID_HANDLE;
    triangle_renderer graphics = {0};

    if (nk_window_create(&window_options, &window) != NK_OK) {
        fprintf(stderr, "nk_window_create failed: %s\n", nk_last_error());
        result = 1;
        goto cleanup;
    }
    window_created = 1;
    if (nkgpu_surface_create(window, window_options.width, window_options.height,
                             &surface) != NKGPU_OK) {
        fprintf(stderr, "nkgpu_surface_create failed: %s\n", nkgpu_last_error());
        result = 1;
        goto cleanup;
    }
    surface_created = 1;

    while (running) {
        nk_event event = {0};
        event.struct_size = sizeof(event);
        if (nk_wait_events_timeout(0.016) != NK_OK || nk_poll_event(&event) != NK_OK) {
            fprintf(stderr, "NativeKit event wait failed: %s\n", nk_last_error());
            result = 1;
            break;
        }
        if (event.kind == NK_EVENT_WINDOW_CLOSE && event.source == window) {
            running = 0;
        } else if (event.kind == NK_EVENT_WINDOW_RESIZE && event.source == window &&
                   event.data_size >= sizeof(nk_window_resize_event)) {
            const nk_window_resize_event *size = (const nk_window_resize_event *)event.data;
            if (nkgpu_surface_resize(surface, size->width, size->height) != NKGPU_OK) {
                fprintf(stderr, "nkgpu_surface_resize failed: %s\n", nkgpu_last_error());
                result = 1;
                running = 0;
            }
        } else if (event.kind == NK_EVENT_SURFACE_READY && event.source == surface) {
            if (!triangle_renderer_create(surface, &graphics)) {
                result = 1;
                running = 0;
            } else {
                ready = 1;
            }
        }
        nk_event_release(&event);

        if (ready && running && !triangle_renderer_draw(&graphics)) {
            result = 1;
            break;
        }
    }

cleanup:
    if (graphics.renderer.id)
        nkgpu_renderer_destroy(graphics.renderer);
    if (surface_created)
        nkgpu_surface_destroy(surface);
    if (window_created)
        nk_window_destroy(window);
    nk_shutdown();
    return result;
}
