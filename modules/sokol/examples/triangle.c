#include "nativekit.h"
#include "nativekit_graphics.h"
#include "nativekit_window.h"

#if !defined(__linux__)
#error "The first NativeKit/Sokol prototype currently targets desktop Linux/OpenGL."
#endif

#define SOKOL_IMPL
#define SOKOL_GLCORE
#include "sokol_gfx.h"

#include <GL/gl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

typedef struct prototype_renderer {
    sg_buffer vertices;
    sg_shader shader;
    sg_pipeline pipeline;
    sg_bindings bindings;
    int framebuffer_width;
    int framebuffer_height;
} prototype_renderer;

static void sleep_milliseconds(unsigned milliseconds) {
    const struct timespec delay = {(time_t)(milliseconds / 1000),
                                   (long)(milliseconds % 1000) * 1000000L};
    nanosleep(&delay, NULL);
}

static int renderer_init(prototype_renderer *renderer) {
    sg_setup(&(sg_desc){
        .environment.defaults =
            {
                .color_format = SG_PIXELFORMAT_RGBA8,
                .depth_format = SG_PIXELFORMAT_NONE,
                .sample_count = 1,
            },
    });
    if (!sg_isvalid()) {
        fprintf(stderr, "sg_setup failed\n");
        return 0;
    }

    static const float vertices[] = {
        0.0f,  0.72f, 1.0f,  0.30f,  0.35f, -0.72f, -0.58f, 0.25f,
        0.85f, 0.55f, 0.72f, -0.58f, 0.30f, 0.55f,  1.0f,
    };
    renderer->vertices = sg_make_buffer(&(sg_buffer_desc){.data = SG_RANGE(vertices)});
    renderer->shader = sg_make_shader(&(sg_shader_desc){
        .vertex_func.source = "#version 330\n"
                              "layout(location=0) in vec2 position;\n"
                              "layout(location=1) in vec3 color0;\n"
                              "out vec3 color;\n"
                              "void main(){ color=color0; gl_Position=vec4(position,0.0,1.0); }\n",
        .fragment_func.source = "#version 330\n"
                                "in vec3 color; out vec4 frag_color;\n"
                                "void main(){ frag_color=vec4(color,1.0); }\n",
    });
    renderer->pipeline = sg_make_pipeline(&(sg_pipeline_desc){
        .shader = renderer->shader,
        .layout.attrs =
            {
                [0].format = SG_VERTEXFORMAT_FLOAT2,
                [1].format = SG_VERTEXFORMAT_FLOAT3,
            },
    });
    renderer->bindings.vertex_buffers[0] = renderer->vertices;

    return sg_query_buffer_state(renderer->vertices) == SG_RESOURCESTATE_VALID &&
           sg_query_shader_state(renderer->shader) == SG_RESOURCESTATE_VALID &&
           sg_query_pipeline_state(renderer->pipeline) == SG_RESOURCESTATE_VALID;
}

static void renderer_draw(const prototype_renderer *renderer) {
    GLint framebuffer = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &framebuffer);

    const sg_pass pass = {
        .action.colors[0] =
            {
                .load_action = SG_LOADACTION_CLEAR,
                .clear_value = {0.035f, 0.055f, 0.11f, 1.0f},
            },
        .swapchain =
            {
                .width = renderer->framebuffer_width,
                .height = renderer->framebuffer_height,
                .sample_count = 1,
                .color_format = SG_PIXELFORMAT_RGBA8,
                .depth_format = SG_PIXELFORMAT_NONE,
                .gl.framebuffer = (uint32_t)framebuffer,
            },
    };
    sg_begin_pass(&pass);
    sg_apply_pipeline(renderer->pipeline);
    sg_apply_bindings(&renderer->bindings);
    sg_draw(0, 3, 1);
    sg_end_pass();
    sg_commit();
}

int main(int argc, char **argv) {
    const int smoke_test = argc == 2 && strcmp(argv[1], "--smoke-test") == 0;
    if (argc > 1 && !smoke_test) {
        fprintf(stderr, "Usage: %s [--smoke-test]\n", argv[0]);
        return 2;
    }

    nk_init_options init = {.struct_size = sizeof(init), .api_version = NK_API_VERSION};
    if (nk_init(&init) != NK_OK) {
        fprintf(stderr, "nk_init failed: %s\n", nk_last_error());
        return 1;
    }

    nk_window_options window_options = {
        .struct_size = sizeof(window_options),
        .flags = NK_WINDOW_RESIZABLE,
        .width = 800,
        .height = 600,
        .title = "Sokol over NativeKit",
    };
    nk_handle window = NK_INVALID_HANDLE;
    if (nk_window_create(&window_options, &window) != NK_OK) {
        fprintf(stderr, "nk_window_create failed: %s\n", nk_last_error());
        nk_shutdown();
        return 1;
    }

    nk_surface_options surface_options = {
        .struct_size = sizeof(surface_options),
        .flags = NK_SURFACE_FORWARD_COMPATIBLE,
        .api = NK_GRAPHICS_OPENGL,
        .major_version = 3,
        .minor_version = 3,
        .width = window_options.width,
        .height = window_options.height,
    };
    nk_handle surface = NK_INVALID_HANDLE;
    if (nk_surface_create(window, &surface_options, &surface) != NK_OK) {
        fprintf(stderr, "nk_surface_create failed: %s\n", nk_last_error());
        nk_window_destroy(window);
        nk_shutdown();
        return 1;
    }

    prototype_renderer renderer = {0};
    int ready = 0;
    int running = 1;
    unsigned rendered_frames = 0;
    while (running) {
        nk_event event = {.struct_size = sizeof(event)};
        if (nk_poll_event(&event) != NK_OK) {
            fprintf(stderr, "nk_poll_event failed: %s\n", nk_last_error());
            running = 0;
        } else if (event.kind == NK_EVENT_WINDOW_CLOSE && event.source == window) {
            running = 0;
        } else if (event.kind == NK_EVENT_WINDOW_RESIZE && event.source == window &&
                   event.data_size >= sizeof(nk_window_resize_event)) {
            const nk_window_resize_event *size = event.data;
            nk_surface_set_bounds(surface, 0, 0, size->width, size->height);
        } else if (event.kind == NK_EVENT_SURFACE_READY && event.source == surface) {
            if (nk_surface_make_current(surface) != NK_OK || !renderer_init(&renderer)) {
                fprintf(stderr, "Could not initialize Sokol renderer: %s\n", nk_last_error());
                running = 0;
            } else {
                nk_surface_get_framebuffer_size(surface, &renderer.framebuffer_width,
                                                &renderer.framebuffer_height);
                ready = 1;
            }
        } else if (event.kind == NK_EVENT_SURFACE_RESIZE && event.source == surface &&
                   event.data_size >= sizeof(nk_surface_resize_event)) {
            const nk_surface_resize_event *size = event.data;
            renderer.framebuffer_width = size->framebuffer_width;
            renderer.framebuffer_height = size->framebuffer_height;
        }
        const int queue_was_empty = event.kind == NK_EVENT_NONE;
        nk_event_release(&event);

        if (ready && running) {
            if (nk_surface_make_current(surface) != NK_OK) {
                fprintf(stderr, "nk_surface_make_current failed: %s\n", nk_last_error());
                break;
            }
            renderer_draw(&renderer);
            if (nk_surface_present(surface) != NK_OK) {
                fprintf(stderr, "nk_surface_present failed: %s\n", nk_last_error());
                break;
            }
            if (smoke_test && ++rendered_frames >= 30)
                running = 0;
            sleep_milliseconds(16);
        } else if (queue_was_empty) {
            sleep_milliseconds(8);
        }
    }

    if (ready) {
        nk_surface_make_current(surface);
        sg_shutdown();
    }
    nk_surface_destroy(surface);
    nk_window_destroy(window);
    nk_shutdown();
    return 0;
}
