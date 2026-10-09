#include "nativekit.h"
#include "nativekit_gpu.h"
#include "nativekit_graphics.h"
#include "nativekit_time.h"
#include "nativekit_window.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

// Measure the public NativeKit frame path, without a second windowing toolkit,
// a producer timer, readback, or a UI framework. CPU timings are not scanout FPS.
namespace {
using Clock = std::chrono::steady_clock;
double now() {
    return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
}

enum class Mode { clear, triangle, composite };
struct Sample {
    double started, render_ms;
};
struct App {
    Mode mode = Mode::clear;
    const char *mode_name = "clear";
    double seconds = 6, warmup = 1, first_frame = 0;
    int width = 1600, height = 1000, target_width = 0, target_height = 0;
    int framebuffer_width = 0, framebuffer_height = 0;
    bool running = true, failed = false;
    nk_window window = NK_INVALID_HANDLE;
    nk_surface surface = NK_INVALID_HANDLE;
    nkgpu_renderer renderer{};
    nkgpu_buffer triangle_vertices{}, quad_vertices{};
    nkgpu_shader color_shader{}, texture_shader{};
    nkgpu_pipeline triangle_pipeline{}, offscreen_pipeline{}, composite_pipeline{};
    nkgpu_image target{};
    nkgpu_sampler sampler{};
    std::vector<Sample> samples;
    FILE *csv = nullptr;

    bool gpu(nkgpu_result result, const char *operation) {
        if (result == NKGPU_OK)
            return true;
        std::fprintf(stderr, "%s failed (%d): %s\n", operation, result, nkgpu_last_error());
        failed = true;
        running = false;
        return false;
    }
    bool native(nk_result result, const char *operation) {
        if (result == NK_OK)
            return true;
        std::fprintf(stderr, "%s failed (%d): %s\n", operation, result, nk_last_error());
        failed = true;
        running = false;
        return false;
    }

    bool pipeline(nkgpu_shader shader, bool offscreen, nkgpu_pipeline *out) {
        nkgpu_pipeline_builder builder{};
        return gpu(nkgpu_pipeline_begin(renderer, shader, 4 * sizeof(float), &builder),
                   "pipeline begin") &&
               gpu(nkgpu_pipeline_attribute(builder, 0, 0, 0, NKGPU_VERTEXFORMAT_FLOAT2),
                   "position attribute") &&
               gpu(nkgpu_pipeline_attribute(builder, 1, 0, 2 * sizeof(float),
                                            NKGPU_VERTEXFORMAT_FLOAT2), "UV attribute") &&
               (!offscreen || gpu(nkgpu_pipeline_depth_stencil(builder, 0), "offscreen depth")) &&
               gpu(nkgpu_pipeline_end(builder, out), "pipeline end");
    }

    bool initialize() {
        if (!gpu(nkgpu_renderer_create(surface, &renderer), "renderer create"))
            return false;
        if (mode == Mode::clear)
            return true;
        const auto api = nkgpu_query_graphics_api(renderer);
        if (api != NK_GRAPHICS_OPENGL && api != NK_GRAPHICS_OPENGL_ES) {
            std::fprintf(stderr, "Triangle/composite modes require a GLCore or GLES3 build.\n");
            failed = true;
            running = false;
            return false;
        }
        const char *prefix = api == NK_GRAPHICS_OPENGL_ES
            ? "#version 300 es\nprecision highp float;\n" : "#version 330\n";
        char vs[512], fs[512];
        std::snprintf(vs, sizeof(vs), "%s%s", prefix,
            "layout(location=0) in vec2 position;\n"
            "layout(location=1) in vec2 uv;\n"
            "out vec2 texcoord;\n"
            "void main(){texcoord=uv;gl_Position=vec4(position,0,1);}\n");
        std::snprintf(fs, sizeof(fs), "%s%s", prefix,
            "in vec2 texcoord;out vec4 color;\n"
            "void main(){color=vec4(texcoord,0.8,1);}\n");
        const float triangle[] = {-0.7f, -0.7f, 0, 0, 0.7f, -0.7f, 1, 0, 0, 0.7f, 0.5f, 1};
        if (!gpu(nkgpu_shader_create(renderer, NKGPU_SHADERLANGUAGE_GLSL, vs, fs,
                                      &color_shader), "color shader") ||
            !gpu(nkgpu_buffer_create(renderer, reinterpret_cast<const uint8_t *>(triangle),
                                      sizeof(triangle), &triangle_vertices), "triangle vertices"))
            return false;
        if (mode == Mode::triangle)
            return pipeline(color_shader, false, &triangle_pipeline);
        const float quad[] = {-1, -1, 0, 0, 1, -1, 1, 0, 1, 1, 1, 1,
                              -1, -1, 0, 0, 1, 1, 1, 1, -1, 1, 0, 1};
        std::snprintf(fs, sizeof(fs), "%s%s", prefix,
            "uniform sampler2D source_texture;in vec2 texcoord;out vec4 color;\n"
            "void main(){color=texture(source_texture,texcoord);}\n");
        nkgpu_shader_builder builder{};
        return pipeline(color_shader, true, &offscreen_pipeline) &&
               gpu(nkgpu_buffer_create(renderer, reinterpret_cast<const uint8_t *>(quad),
                                       sizeof(quad), &quad_vertices), "quad vertices") &&
               gpu(nkgpu_shader_begin(renderer, NKGPU_SHADERLANGUAGE_GLSL, vs, fs, &builder),
                   "texture shader begin") &&
               gpu(nkgpu_shader_texture(builder, 0, 0, NKGPU_SHADERSTAGE_FRAGMENT,
                                         "source_texture"), "texture binding") &&
               gpu(nkgpu_shader_end(builder, &texture_shader), "texture shader end") &&
               pipeline(texture_shader, false, &composite_pipeline) &&
               gpu(nkgpu_sampler_create(renderer, NKGPU_FILTER_LINEAR, NKGPU_FILTER_LINEAR,
                                         NKGPU_WRAP_CLAMP_TO_EDGE, NKGPU_WRAP_CLAMP_TO_EDGE,
                                         &sampler), "sampler");
    }

    bool resize_target(int width, int height) {
        if (target.id && target_width == width && target_height == height)
            return true;
        nkgpu_image_desc desc{};
        desc.struct_size = sizeof(desc);
        desc.width = width;
        desc.height = height;
        desc.format = NKGPU_IMAGEFORMAT_RGBA8;
        desc.usage = NKGPU_IMAGE_RENDER_TARGET | NKGPU_IMAGE_SAMPLED;
        nkgpu_image replacement{};
        if (!gpu(nkgpu_image_create_desc(renderer, &desc, &replacement), "offscreen image"))
            return false;
        if (target.id && !gpu(nkgpu_image_destroy(renderer, target), "old offscreen image"))
            return false;
        target = replacement;
        target_width = width;
        target_height = height;
        return true;
    }

    bool draw(nkgpu_pipeline state, nkgpu_buffer vertices, int count) {
        return gpu(nkgpu_apply_pipeline(renderer, state), "apply pipeline") &&
               gpu(nkgpu_apply_vertex_buffer(renderer, 0, vertices, 0), "apply vertices") &&
               gpu(nkgpu_draw(renderer, 0, count, 1), "draw");
    }

    bool render(int width, int height) {
        if (mode != Mode::composite) {
            if (!gpu(nkgpu_begin_frame(renderer), "begin frame"))
                return false;
            if (mode == Mode::triangle && !draw(triangle_pipeline, triangle_vertices, 3)) {
                nkgpu_frame_abort(renderer);
                return false;
            }
            return gpu(nkgpu_end_frame(renderer), "end frame");
        }
        if (!resize_target(width, height) ||
            !gpu(nkgpu_frame_begin(renderer), "begin composite frame"))
            return false;
        nkgpu_render_pass_desc pass{};
        pass.struct_size = sizeof(pass);
        pass.color_count = 1;
        pass.colors[0].image = target;
        pass.colors[0].action.load_action = NKGPU_LOADACTION_CLEAR;
        pass.colors[0].action.store_action = NKGPU_STOREACTION_STORE;
        pass.colors[0].action.clear_color = {0.08f, 0.12f, 0.18f, 1.0f};
        if (!gpu(nkgpu_begin_render_pass(renderer, &pass), "offscreen pass") ||
            !draw(offscreen_pipeline, triangle_vertices, 3) ||
            !gpu(nkgpu_end_pass(renderer), "end offscreen pass") ||
            !gpu(nkgpu_begin_window_pass(renderer, width, height, 1), "window pass") ||
            !gpu(nkgpu_apply_image(renderer, 0, target), "apply image") ||
            !gpu(nkgpu_apply_sampler(renderer, 0, sampler), "apply sampler") ||
            !draw(composite_pipeline, quad_vertices, 6)) {
            nkgpu_frame_abort(renderer);
            return false;
        }
        return gpu(nkgpu_end_frame(renderer), "end composite frame");
    }
};

void NK_CALL frame(nk_surface, int32_t width, int32_t height, void *data) {
    auto &app = *static_cast<App *>(data);
    if (!app.running)
        return;
    if (!app.renderer.id && !app.initialize())
        return;
    const double started = now();
    if (!app.first_frame)
        app.first_frame = started;
    if (started - app.first_frame >= app.warmup + app.seconds) {
        app.running = false;
        return;
    }
    if (!app.render(width, height))
        return;
    app.framebuffer_width = width;
    app.framebuffer_height = height;
    if (started - app.first_frame >= app.warmup)
        app.samples.push_back({started, (now() - started) * 1000});
}

double percentile(std::vector<double> values, double fraction) {
    if (values.empty())
        return 0;
    std::sort(values.begin(), values.end());
    return values[static_cast<size_t>((values.size() - 1) * fraction)];
}
} // namespace

int main(int argc, char **argv) {
    App app;
    const char *csv_path = nullptr;
    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (!std::strcmp(arg, "--help")) {
            std::puts("frame_pacing [--mode clear|triangle|composite] [--seconds N] [--warmup N]\n"
                      "             [--width N] [--height N] [--csv PATH]\n"
                      "Reports callback cadence and CPU submission duration, not scanout FPS.");
            return 0;
        }
        if (++i == argc) {
            std::fprintf(stderr, "Missing value for %s\n", arg);
            return 1;
        }
        const char *value = argv[i];
        if (!std::strcmp(arg, "--mode")) {
            if (!std::strcmp(value, "clear"))
                app.mode = Mode::clear;
            else if (!std::strcmp(value, "triangle"))
                app.mode = Mode::triangle;
            else if (!std::strcmp(value, "composite"))
                app.mode = Mode::composite;
            else {
                std::fprintf(stderr, "Unknown mode: %s\n", value);
                return 1;
            }
            app.mode_name = value;
        } else if (!std::strcmp(arg, "--seconds") || !std::strcmp(arg, "--warmup")) {
            char *end = nullptr;
            const double number = std::strtod(value, &end);
            if (end == value || *end || !std::isfinite(number) || number < 0 || number > 300 ||
                (!std::strcmp(arg, "--seconds") && number == 0)) {
                std::fprintf(stderr, "Invalid duration: %s\n", value);
                return 1;
            }
            if (!std::strcmp(arg, "--seconds"))
                app.seconds = number;
            else
                app.warmup = number;
        } else if (!std::strcmp(arg, "--width") || !std::strcmp(arg, "--height")) {
            char *end = nullptr;
            const long number = std::strtol(value, &end, 10);
            if (end == value || *end || number < 1 || number > 16384) {
                std::fprintf(stderr, "Invalid dimension: %s\n", value);
                return 1;
            }
            if (!std::strcmp(arg, "--width"))
                app.width = static_cast<int>(number);
            else
                app.height = static_cast<int>(number);
        } else if (!std::strcmp(arg, "--csv")) {
            csv_path = value;
        } else {
            std::fprintf(stderr, "Unknown option: %s\n", arg);
            return 1;
        }
    }
    if (csv_path) {
        app.csv = std::fopen(csv_path, "w");
        if (!app.csv) {
            std::perror(csv_path);
            return 1;
        }
    }
    app.samples.reserve(static_cast<size_t>(app.seconds * 1000) + 1);
    nk_init_options init{};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    if (!app.native(nk_init(&init), "initialize NativeKit"))
        return 1;
    nk_window_options window{};
    window.struct_size = sizeof(window);
    window.flags = NK_WINDOW_RESIZABLE;
    window.width = app.width;
    window.height = app.height;
    window.title = "NativeKit GPU frame pacing";
    if (app.native(nk_window_create(&window, &app.window), "create window") &&
        app.gpu(nkgpu_surface_create(app.window, app.width, app.height, &app.surface),
                "create surface") &&
        app.native(nk_surface_set_frame_mode(app.surface, NK_SURFACE_FRAME_CONTINUOUS),
                   "continuous frames") &&
        app.native(nk_surface_set_frame_callback(app.surface, frame, &app), "frame callback")) {
        const double deadline = now() + app.warmup + app.seconds + 10;
        while (app.running && now() < deadline) {
            nk_event event{};
            event.struct_size = sizeof(event);
            if (!app.native(nk_poll_event(&event), "poll event"))
                break;
            const bool idle = event.kind == NK_EVENT_NONE;
            if (event.kind == NK_EVENT_WINDOW_CLOSE && event.source == app.window)
                app.running = false;
            if (event.kind == NK_EVENT_WINDOW_RESIZE && event.source == app.window &&
                event.data_size >= sizeof(nk_window_resize_event)) {
                const auto *size = static_cast<const nk_window_resize_event *>(event.data);
                if (size->width > 0 && size->height > 0)
                    app.gpu(nkgpu_surface_resize(app.surface, size->width, size->height),
                            "resize surface");
            }
            nk_event_release(&event);
            if (idle && app.running &&
                !app.native(nk_wait_events_timeout(0.1), "wait events"))
                break;
        }
        if (app.running) {
            std::fprintf(stderr, "Frame delivery timed out.\n");
            app.failed = true;
        }
    }
    if (app.surface != NK_INVALID_HANDLE)
        app.native(nk_surface_set_frame_callback(app.surface, nullptr, nullptr), "remove callback");
    if (app.renderer.id)
        app.gpu(nkgpu_renderer_destroy(app.renderer), "destroy renderer");
    if (app.surface != NK_INVALID_HANDLE)
        app.gpu(nkgpu_surface_destroy(app.surface), "destroy surface");
    if (app.window != NK_INVALID_HANDLE)
        app.native(nk_window_destroy(app.window), "destroy window");
    nk_shutdown();

    std::vector<double> intervals, render;
    if (app.csv)
        std::fprintf(app.csv, "started_monotonic_seconds,render_ms\n");
    for (size_t i = 0; i < app.samples.size(); ++i) {
        const auto &sample = app.samples[i];
        render.push_back(sample.render_ms);
        if (i)
            intervals.push_back((sample.started - app.samples[i - 1].started) * 1000);
        if (app.csv)
            std::fprintf(app.csv, "%.9f,%.6f\n", sample.started, sample.render_ms);
    }
    if (app.csv)
        std::fclose(app.csv);
    const double span = app.samples.size() > 1
        ? app.samples.back().started - app.samples.front().started : 0;
    std::printf("{\"mode\":\"%s\",\"frames\":%zu,\"spanSeconds\":%.6f,"
                "\"callbackFps\":%.3f,\"framebufferWidth\":%d,\"framebufferHeight\":%d,"
                "\"intervalMedianMs\":%.3f,\"intervalP95Ms\":%.3f,"
                "\"renderMedianMs\":%.3f,\"renderP95Ms\":%.3f}\n",
                app.mode_name, app.samples.size(), span, span > 0 ? intervals.size() / span : 0,
                app.framebuffer_width, app.framebuffer_height,
                percentile(intervals, 0.5), percentile(intervals, 0.95),
                percentile(render, 0.5), percentile(render, 0.95));
    return app.failed || app.samples.size() < 2 ? 1 : 0;
}
