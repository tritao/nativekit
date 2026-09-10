#include "nativekit.h"
#include "nativekit_graphics.h"
#include "nativekit_window.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <time.h>
#endif

typedef unsigned int gl_enum;
typedef unsigned int gl_uint;
typedef int gl_int;
typedef int gl_size;
typedef ptrdiff_t gl_sizeiptr;
typedef char gl_char;
typedef float gl_float;
typedef unsigned char gl_boolean;
typedef void(NK_CALL *gl_viewport_proc)(gl_int, gl_int, gl_size, gl_size);
typedef void(NK_CALL *gl_clear_color_proc)(gl_float, gl_float, gl_float, gl_float);
typedef void(NK_CALL *gl_clear_proc)(gl_enum);
typedef gl_uint(NK_CALL *gl_create_shader_proc)(gl_enum);
typedef void(NK_CALL *gl_shader_source_proc)(gl_uint, gl_size, const gl_char *const *,
                                             const gl_int *);
typedef void(NK_CALL *gl_compile_shader_proc)(gl_uint);
typedef void(NK_CALL *gl_get_shader_iv_proc)(gl_uint, gl_enum, gl_int *);
typedef void(NK_CALL *gl_get_shader_log_proc)(gl_uint, gl_size, gl_size *, gl_char *);
typedef gl_uint(NK_CALL *gl_create_program_proc)(void);
typedef void(NK_CALL *gl_attach_shader_proc)(gl_uint, gl_uint);
typedef void(NK_CALL *gl_link_program_proc)(gl_uint);
typedef void(NK_CALL *gl_get_program_iv_proc)(gl_uint, gl_enum, gl_int *);
typedef void(NK_CALL *gl_get_program_log_proc)(gl_uint, gl_size, gl_size *, gl_char *);
typedef void(NK_CALL *gl_use_program_proc)(gl_uint);
typedef void(NK_CALL *gl_gen_vertex_arrays_proc)(gl_size, gl_uint *);
typedef void(NK_CALL *gl_bind_vertex_array_proc)(gl_uint);
typedef void(NK_CALL *gl_gen_buffers_proc)(gl_size, gl_uint *);
typedef void(NK_CALL *gl_bind_buffer_proc)(gl_enum, gl_uint);
typedef void(NK_CALL *gl_buffer_data_proc)(gl_enum, gl_sizeiptr, const void *, gl_enum);
typedef void(NK_CALL *gl_vertex_attrib_pointer_proc)(gl_uint, gl_int, gl_enum, gl_boolean, gl_size,
                                                     const void *);
typedef void(NK_CALL *gl_enable_vertex_attrib_proc)(gl_uint);
typedef void(NK_CALL *gl_draw_arrays_proc)(gl_enum, gl_int, gl_size);
typedef void(NK_CALL *gl_delete_shader_proc)(gl_uint);
typedef void(NK_CALL *gl_delete_program_proc)(gl_uint);
typedef void(NK_CALL *gl_delete_vertex_arrays_proc)(gl_size, const gl_uint *);
typedef void(NK_CALL *gl_delete_buffers_proc)(gl_size, const gl_uint *);

enum {
    GL_FALSE_VALUE = 0,
    GL_FLOAT_VALUE = 0x1406,
    GL_TRIANGLES_VALUE = 0x0004,
    GL_COLOR_BUFFER_BIT_VALUE = 0x00004000,
    GL_ARRAY_BUFFER_VALUE = 0x8892,
    GL_STATIC_DRAW_VALUE = 0x88E4,
    GL_VERTEX_SHADER_VALUE = 0x8B31,
    GL_FRAGMENT_SHADER_VALUE = 0x8B30,
    GL_COMPILE_STATUS_VALUE = 0x8B81,
    GL_LINK_STATUS_VALUE = 0x8B82
};

typedef struct renderer {
    gl_viewport_proc viewport;
    gl_clear_color_proc clear_color;
    gl_clear_proc clear;
    gl_create_shader_proc create_shader;
    gl_shader_source_proc shader_source;
    gl_compile_shader_proc compile_shader;
    gl_get_shader_iv_proc get_shader_iv;
    gl_get_shader_log_proc get_shader_log;
    gl_create_program_proc create_program;
    gl_attach_shader_proc attach_shader;
    gl_link_program_proc link_program;
    gl_get_program_iv_proc get_program_iv;
    gl_get_program_log_proc get_program_log;
    gl_use_program_proc use_program;
    gl_gen_vertex_arrays_proc gen_vertex_arrays;
    gl_bind_vertex_array_proc bind_vertex_array;
    gl_gen_buffers_proc gen_buffers;
    gl_bind_buffer_proc bind_buffer;
    gl_buffer_data_proc buffer_data;
    gl_vertex_attrib_pointer_proc vertex_attrib_pointer;
    gl_enable_vertex_attrib_proc enable_vertex_attrib;
    gl_draw_arrays_proc draw_arrays;
    gl_delete_shader_proc delete_shader;
    gl_delete_program_proc delete_program;
    gl_delete_vertex_arrays_proc delete_vertex_arrays;
    gl_delete_buffers_proc delete_buffers;
    gl_uint program;
    gl_uint vertex_array;
    gl_uint vertex_buffer;
    int framebuffer_width;
    int framebuffer_height;
    unsigned frame;
} renderer;

static void sleep_milliseconds(unsigned milliseconds) {
#if defined(_WIN32)
    Sleep(milliseconds);
#else
    struct timespec delay = {(time_t)(milliseconds / 1000),
                             (long)(milliseconds % 1000) * 1000000L};
    nanosleep(&delay, NULL);
#endif
}

static int load_proc(nk_handle surface, const char *name, nk_graphics_proc *out) {
    const nk_result result = nk_surface_get_proc_address(surface, name, out);
    if (result == NK_OK)
        return 1;
    fprintf(stderr, "Could not load %s: %s\n", name, nk_last_error());
    return 0;
}

#define LOAD_GL(field, type, name)                                                                 \
    do {                                                                                           \
        nk_graphics_proc proc = NULL;                                                              \
        if (!load_proc(surface, name, &proc))                                                      \
            return 0;                                                                              \
        graphics->field = (type)proc;                                                              \
    } while (0)

static gl_uint compile_shader(renderer *graphics, gl_enum kind, const char *source) {
    const gl_uint shader = graphics->create_shader(kind);
    graphics->shader_source(shader, 1, &source, NULL);
    graphics->compile_shader(shader);
    gl_int compiled = 0;
    graphics->get_shader_iv(shader, GL_COMPILE_STATUS_VALUE, &compiled);
    if (!compiled) {
        char log[1024] = {0};
        graphics->get_shader_log(shader, (gl_size)sizeof(log), NULL, log);
        fprintf(stderr, "Shader compilation failed: %s\n", log);
        graphics->delete_shader(shader);
        return 0;
    }
    return shader;
}

static int renderer_init(renderer *graphics, nk_handle surface) {
    LOAD_GL(viewport, gl_viewport_proc, "glViewport");
    LOAD_GL(clear_color, gl_clear_color_proc, "glClearColor");
    LOAD_GL(clear, gl_clear_proc, "glClear");
    LOAD_GL(create_shader, gl_create_shader_proc, "glCreateShader");
    LOAD_GL(shader_source, gl_shader_source_proc, "glShaderSource");
    LOAD_GL(compile_shader, gl_compile_shader_proc, "glCompileShader");
    LOAD_GL(get_shader_iv, gl_get_shader_iv_proc, "glGetShaderiv");
    LOAD_GL(get_shader_log, gl_get_shader_log_proc, "glGetShaderInfoLog");
    LOAD_GL(create_program, gl_create_program_proc, "glCreateProgram");
    LOAD_GL(attach_shader, gl_attach_shader_proc, "glAttachShader");
    LOAD_GL(link_program, gl_link_program_proc, "glLinkProgram");
    LOAD_GL(get_program_iv, gl_get_program_iv_proc, "glGetProgramiv");
    LOAD_GL(get_program_log, gl_get_program_log_proc, "glGetProgramInfoLog");
    LOAD_GL(use_program, gl_use_program_proc, "glUseProgram");
    LOAD_GL(gen_vertex_arrays, gl_gen_vertex_arrays_proc, "glGenVertexArrays");
    LOAD_GL(bind_vertex_array, gl_bind_vertex_array_proc, "glBindVertexArray");
    LOAD_GL(gen_buffers, gl_gen_buffers_proc, "glGenBuffers");
    LOAD_GL(bind_buffer, gl_bind_buffer_proc, "glBindBuffer");
    LOAD_GL(buffer_data, gl_buffer_data_proc, "glBufferData");
    LOAD_GL(vertex_attrib_pointer, gl_vertex_attrib_pointer_proc, "glVertexAttribPointer");
    LOAD_GL(enable_vertex_attrib, gl_enable_vertex_attrib_proc, "glEnableVertexAttribArray");
    LOAD_GL(draw_arrays, gl_draw_arrays_proc, "glDrawArrays");
    LOAD_GL(delete_shader, gl_delete_shader_proc, "glDeleteShader");
    LOAD_GL(delete_program, gl_delete_program_proc, "glDeleteProgram");
    LOAD_GL(delete_vertex_arrays, gl_delete_vertex_arrays_proc, "glDeleteVertexArrays");
    LOAD_GL(delete_buffers, gl_delete_buffers_proc, "glDeleteBuffers");

    static const char vertex_source[] =
        "#version 330 core\n"
        "layout(location=0) in vec2 position; layout(location=1) in vec3 color;\n"
        "out vec3 vertex_color; void main(){vertex_color=color;gl_Position=vec4(position,0,1);}";
    static const char fragment_source[] =
        "#version 330 core\n"
        "in vec3 vertex_color; out vec4 pixel; void main(){pixel=vec4(vertex_color,1);}";
    const gl_uint vertex = compile_shader(graphics, GL_VERTEX_SHADER_VALUE, vertex_source);
    const gl_uint fragment = compile_shader(graphics, GL_FRAGMENT_SHADER_VALUE, fragment_source);
    if (!vertex || !fragment) {
        if (vertex)
            graphics->delete_shader(vertex);
        if (fragment)
            graphics->delete_shader(fragment);
        return 0;
    }
    graphics->program = graphics->create_program();
    graphics->attach_shader(graphics->program, vertex);
    graphics->attach_shader(graphics->program, fragment);
    graphics->link_program(graphics->program);
    graphics->delete_shader(vertex);
    graphics->delete_shader(fragment);
    gl_int linked = 0;
    graphics->get_program_iv(graphics->program, GL_LINK_STATUS_VALUE, &linked);
    if (!linked) {
        char log[1024] = {0};
        graphics->get_program_log(graphics->program, (gl_size)sizeof(log), NULL, log);
        fprintf(stderr, "Program link failed: %s\n", log);
        return 0;
    }

    static const gl_float vertices[] = {
        0.0f,  0.72f, 1.0f, 0.30f, 0.35f, -0.72f, -0.58f, 0.25f,
        0.85f, 0.55f, 0.72f, -0.58f, 0.30f, 0.55f, 1.0f,
    };
    graphics->gen_vertex_arrays(1, &graphics->vertex_array);
    graphics->bind_vertex_array(graphics->vertex_array);
    graphics->gen_buffers(1, &graphics->vertex_buffer);
    graphics->bind_buffer(GL_ARRAY_BUFFER_VALUE, graphics->vertex_buffer);
    graphics->buffer_data(GL_ARRAY_BUFFER_VALUE, (gl_sizeiptr)sizeof(vertices), vertices,
                          GL_STATIC_DRAW_VALUE);
    graphics->vertex_attrib_pointer(0, 2, GL_FLOAT_VALUE, GL_FALSE_VALUE,
                                    5 * (gl_size)sizeof(gl_float), NULL);
    graphics->enable_vertex_attrib(0);
    graphics->vertex_attrib_pointer(1, 3, GL_FLOAT_VALUE, GL_FALSE_VALUE,
                                    5 * (gl_size)sizeof(gl_float),
                                    (const void *)(2 * sizeof(gl_float)));
    graphics->enable_vertex_attrib(1);
    return 1;
}

static void renderer_draw(renderer *graphics) {
    const gl_float pulse = (gl_float)(graphics->frame++ % 240) / 240.0f;
    const gl_float blue = pulse <= 0.5f ? pulse : 1.0f - pulse;
    graphics->viewport(0, 0, graphics->framebuffer_width, graphics->framebuffer_height);
    graphics->clear_color(0.035f, 0.055f, 0.09f + blue * 0.08f, 1.0f);
    graphics->clear(GL_COLOR_BUFFER_BIT_VALUE);
    graphics->use_program(graphics->program);
    graphics->bind_vertex_array(graphics->vertex_array);
    graphics->draw_arrays(GL_TRIANGLES_VALUE, 0, 3);
}

static void renderer_destroy(renderer *graphics) {
    if (graphics->delete_buffers && graphics->vertex_buffer)
        graphics->delete_buffers(1, &graphics->vertex_buffer);
    if (graphics->delete_vertex_arrays && graphics->vertex_array)
        graphics->delete_vertex_arrays(1, &graphics->vertex_array);
    if (graphics->delete_program && graphics->program)
        graphics->delete_program(graphics->program);
}

int main(int argc, char **argv) {
    const int smoke_test = argc == 2 && strcmp(argv[1], "--smoke-test") == 0;
    if (argc > 1 && !smoke_test) {
        fprintf(stderr, "Usage: %s [--smoke-test]\n", argv[0]);
        return 2;
    }
    nk_init_options init = {0};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    if (nk_init(&init) != NK_OK) {
        fprintf(stderr, "nk_init failed: %s\n", nk_last_error());
        return 1;
    }
    if (!(nk_get_capabilities() & NK_CAP_OPENGL_SURFACE)) {
        fprintf(stderr, "This NativeKit backend does not provide OpenGL surfaces.\n");
        nk_shutdown();
        return 1;
    }

    nk_window_options window_options = {0};
    window_options.struct_size = sizeof(window_options);
    window_options.flags = NK_WINDOW_RESIZABLE;
    window_options.width = 800;
    window_options.height = 600;
    window_options.title = "NativeKit OpenGL Triangle";
    nk_handle window = NK_INVALID_HANDLE;
    if (nk_window_create(&window_options, &window) != NK_OK) {
        fprintf(stderr, "nk_window_create failed: %s\n", nk_last_error());
        nk_shutdown();
        return 1;
    }

    nk_surface_options surface_options = {0};
    surface_options.struct_size = sizeof(surface_options);
    surface_options.api = NK_GRAPHICS_OPENGL;
    surface_options.major_version = 3;
    surface_options.minor_version = 3;
    surface_options.width = window_options.width;
    surface_options.height = window_options.height;
    nk_handle surface = NK_INVALID_HANDLE;
    if (nk_surface_create(window, &surface_options, &surface) != NK_OK) {
        fprintf(stderr, "nk_surface_create failed: %s\n", nk_last_error());
        nk_window_destroy(window);
        nk_shutdown();
        return 1;
    }

    renderer graphics = {0};
    int ready = 0;
    int running = 1;
    unsigned rendered_frames = 0;
    while (running) {
        nk_event event = {0};
        event.struct_size = sizeof(event);
        if (nk_poll_event(&event) != NK_OK) {
            fprintf(stderr, "nk_poll_event failed: %s\n", nk_last_error());
            break;
        }
        const int queue_was_empty = event.kind == NK_EVENT_NONE;
        if (event.kind == NK_EVENT_WINDOW_CLOSE && event.source == window) {
            running = 0;
        } else if (event.kind == NK_EVENT_WINDOW_RESIZE && event.source == window &&
                   event.data_size >= sizeof(nk_window_resize_event)) {
            const nk_window_resize_event *size = event.data;
            nk_surface_set_bounds(surface, 0, 0, size->width, size->height);
        } else if (event.kind == NK_EVENT_SURFACE_READY && event.source == surface) {
            if (nk_surface_make_current(surface) != NK_OK || !renderer_init(&graphics, surface)) {
                fprintf(stderr, "Could not initialize renderer: %s\n", nk_last_error());
                running = 0;
            } else {
                nk_surface_get_framebuffer_size(surface, &graphics.framebuffer_width,
                                                &graphics.framebuffer_height);
                ready = 1;
            }
        } else if (event.kind == NK_EVENT_SURFACE_RESIZE && event.source == surface &&
                   event.data_size >= sizeof(nk_surface_resize_event)) {
            const nk_surface_resize_event *size = event.data;
            graphics.framebuffer_width = size->framebuffer_width;
            graphics.framebuffer_height = size->framebuffer_height;
        }
        nk_event_release(&event);

        if (ready && running) {
            if (nk_surface_make_current(surface) != NK_OK) {
                fprintf(stderr, "nk_surface_make_current failed: %s\n", nk_last_error());
                break;
            }
            renderer_draw(&graphics);
            if (nk_surface_present(surface) != NK_OK) {
                fprintf(stderr, "nk_surface_present failed: %s\n", nk_last_error());
                break;
            }
            ++rendered_frames;
            if (smoke_test && rendered_frames == 10) {
                int32_t x = 0, y = 0;
                nk_window_get_position(window, &x, &y);
                nk_window_set_bounds(window, x, y, 520, 360);
            }
            if (smoke_test && rendered_frames >= 30)
                running = 0;
            sleep_milliseconds(16);
        } else if (queue_was_empty) {
            sleep_milliseconds(8);
        }
    }

    if (ready) {
        nk_surface_make_current(surface);
        renderer_destroy(&graphics);
    }
    nk_surface_destroy(surface);
    nk_window_destroy(window);
    nk_shutdown();
    return 0;
}
