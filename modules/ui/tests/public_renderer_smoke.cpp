#include "nativekit.h"
#include "nativekit_graphics.h"
#include "nativekit_ui.h"
#include "nativekit_window.h"

#if defined(NK_SOKOL_BACKEND_GLES3)
#include <GLES3/gl3.h>
#define NKUI_TEST_SURFACE_API NK_GRAPHICS_OPENGL_ES
#else
#include <GL/gl.h>
#define NKUI_TEST_SURFACE_API NK_GRAPHICS_OPENGL
#endif

#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#ifndef NKUI_TEST_FONT_PATH
#error NKUI_TEST_FONT_PATH is required
#endif

template <class T> void append(std::vector<uint8_t> &bytes, const T &value) {
    const size_t offset = bytes.size();
    bytes.resize(offset + sizeof(value));
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

int main() {
    nk_init_options init{};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    if (nk_init(&init) != NK_OK)
        return 1;
    nk_window_options window_options{};
    window_options.struct_size = sizeof(window_options);
    window_options.width = 256;
    window_options.height = 192;
    window_options.title = "NativeKit public UI renderer";
    nk_handle window = NK_INVALID_HANDLE;
    if (nk_window_create(&window_options, &window) != NK_OK)
        return 2;
    nk_surface_options surface_options{};
    surface_options.struct_size = sizeof(surface_options);
    surface_options.flags = NK_SURFACE_FORWARD_COMPATIBLE | NK_SURFACE_STENCIL;
    surface_options.api = NKUI_TEST_SURFACE_API;
    surface_options.major_version = 3;
#if defined(NK_SOKOL_BACKEND_GLES3)
    surface_options.minor_version = 0;
#else
    surface_options.minor_version = 3;
#endif
    surface_options.width = window_options.width;
    surface_options.height = window_options.height;
    nk_handle surface = NK_INVALID_HANDLE;
    if (nk_surface_create(window, &surface_options, &surface) != NK_OK)
        return 3;

    const nkui_path_element path_elements[] = {
        {NKUI_PATH_MOVE_TO, {12.0f, 12.0f}},
        {NKUI_PATH_LINE_TO, {244.0f, 12.0f}},
        {NKUI_PATH_LINE_TO, {244.0f, 180.0f}},
        {NKUI_PATH_LINE_TO, {12.0f, 180.0f}},
        {NKUI_PATH_CLOSE, {}},
    };
    const uint8_t image_pixels[] = {30, 90, 240, 255};
    nkui_resource path{}, paint{}, stroke_paint{}, image{}, fonts{}, text{};
    nkui_display_list list{};
    nkui_renderer renderer{};
    if (nkui_path_create(path_elements, 5, &path) != NKUI_OK ||
        nkui_paint_create_solid({0.08f, 0.45f, 0.16f, 1.0f}, &paint) != NKUI_OK ||
        nkui_paint_create_solid({0.85f, 0.12f, 0.08f, 1.0f}, &stroke_paint) != NKUI_OK ||
        nkui_image_create(1, 1, NKUI_IMAGE_RGBA8, image_pixels, sizeof(image_pixels), &image) !=
            NKUI_OK ||
        nkui_font_collection_create(&fonts) != NKUI_OK ||
        nkui_font_collection_add(fonts, NKUI_TEST_FONT_PATH, NKUI_FONT_FAMILY_DEFAULT) != NKUI_OK ||
        nkui_text_layout_create(fonts, "NativeKit direct text", 220.0f, 24.0f, &text) != NKUI_OK ||
        nkui_display_list_create(&list) != NKUI_OK || nkui_renderer_create(&renderer) != NKUI_OK)
        return 4;

    std::vector<uint8_t> commands;
    append(commands, nkui_resource_command{{NKUI_COMMAND_SET_PAINT, NKUI_COMMAND_VERSION,
                                            sizeof(nkui_resource_command)},
                                           paint});
    append(commands, nkui_resource_command{{NKUI_COMMAND_DRAW_PATH, NKUI_COMMAND_VERSION,
                                            sizeof(nkui_resource_command)},
                                           path});
    append(commands, nkui_transform_command{{NKUI_COMMAND_SET_TRANSFORM, NKUI_COMMAND_VERSION,
                                             sizeof(nkui_transform_command)},
                                            {1.0f, 0.0f, 0.0f, 1.0f, 4.0f, 4.0f}});
    append(commands, nkui_resource_command{{NKUI_COMMAND_DRAW_PATH, NKUI_COMMAND_VERSION,
                                            sizeof(nkui_resource_command)},
                                           path});
    append(commands, nkui_resource_command{{NKUI_COMMAND_SET_PAINT, NKUI_COMMAND_VERSION,
                                            sizeof(nkui_resource_command)},
                                           stroke_paint});
    append(commands, nkui_transform_command{{NKUI_COMMAND_SET_TRANSFORM, NKUI_COMMAND_VERSION,
                                             sizeof(nkui_transform_command)},
                                            {1.0f, 0.0f, 0.0f, 1.0f, 2.0f, 2.0f}});
    append(commands, nkui_stroke_path_command{{NKUI_COMMAND_STROKE_PATH, NKUI_COMMAND_VERSION,
                                               sizeof(nkui_stroke_path_command)},
                                              path,
                                              5.0f,
                                              NKUI_PATH_LINE_CAP_ROUND,
                                              NKUI_PATH_LINE_JOIN_ROUND,
                                              10.0f});
    append(commands, nkui_transform_command{{NKUI_COMMAND_SET_TRANSFORM, NKUI_COMMAND_VERSION,
                                             sizeof(nkui_transform_command)},
                                            {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f}});
    append(commands, nkui_draw_rect_command{{NKUI_COMMAND_DRAW_TEXT_LAYOUT, NKUI_COMMAND_VERSION,
                                             sizeof(nkui_draw_rect_command)},
                                            text,
                                            28.0f,
                                            52.0f,
                                            0.0f,
                                            0.0f});
    append(commands, nkui_layer_command{{NKUI_COMMAND_BEGIN_LAYER, NKUI_COMMAND_VERSION,
                                         sizeof(nkui_layer_command)},
                                        0.65f,
                                        NKUI_COMPOSITE_SOURCE_OVER});
    append(commands, nkui_draw_rect_command{{NKUI_COMMAND_DRAW_IMAGE, NKUI_COMMAND_VERSION,
                                             sizeof(nkui_draw_rect_command)},
                                            image,
                                            72.0f,
                                            82.0f,
                                            112.0f,
                                            70.0f});
    append(commands, nkui_command_header{NKUI_COMMAND_END_LAYER, NKUI_COMMAND_VERSION,
                                         sizeof(nkui_command_header)});
    if (nkui_display_list_submit(list, commands.data(), commands.size()) != NKUI_OK)
        return 5;
    if (nkui_resource_destroy(text) != NKUI_OK || nkui_resource_destroy(image) != NKUI_OK ||
        nkui_resource_destroy(stroke_paint) != NKUI_OK ||
        nkui_resource_destroy(paint) != NKUI_OK || nkui_resource_destroy(path) != NKUI_OK)
        return 5;

    int result = 0;
    bool ready = false;
    int width = 0, height = 0, frames = 0;
    while (!result && frames < 30) {
        nk_event event{};
        event.struct_size = sizeof(event);
        if (nk_poll_event(&event) != NK_OK) {
            result = 6;
        } else if (event.kind == NK_EVENT_SURFACE_READY && event.source == surface) {
            ready = nk_surface_make_current(surface) == NK_OK &&
                    nk_surface_get_framebuffer_size(surface, &width, &height) == NK_OK;
        } else if (event.kind == NK_EVENT_SURFACE_RESIZE && event.source == surface &&
                   event.data_size >= sizeof(nk_surface_resize_event)) {
            const auto *resize = static_cast<const nk_surface_resize_event *>(event.data);
            width = resize->framebuffer_width;
            height = resize->framebuffer_height;
        }
        nk_event_release(&event);
        if (!ready) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        const float pixel_scale = frames == 1 ? 2.0f : 1.0f;
        const nkui_frame_info frame_info{sizeof(frame_info), width / pixel_scale,
                                         height / pixel_scale, width, height, pixel_scale};
        const nkui_result render_result =
            nkui_renderer_render_frame(renderer, list, surface, &frame_info);
        if (render_result != NKUI_OK) {
            std::fprintf(stderr, "public render result %d\n", render_result);
            result = 7;
        } else if (frames == 0) {
            uint8_t background[4]{};
            uint8_t image_sample[4]{};
            glReadPixels(24, height - 24, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, background);
            glReadPixels(120, height - 110, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, image_sample);
            if (background[1] <= background[2] || image_sample[2] <= image_sample[1])
                result = 11;
        }
        if (!result && nk_surface_present(surface) != NK_OK)
            result = 10;
        ++frames;
    }
    if (!result && frames != 30)
        result = 8;
    nkui_renderer_stats stats{};
    if (!result && (nkui_renderer_get_stats(renderer, &stats) != NKUI_OK ||
                    stats.struct_size != sizeof(stats) || stats.path_preparations == 0 ||
                    stats.path_preparations != 4 || stats.path_cache_misses != 4 ||
                    stats.path_cache_hits == 0 || stats.path_vertices_generated == 0 ||
                    stats.path_geometry_bytes_allocated == 0 ||
                    stats.path_geometry_bytes_retained == 0 ||
                    stats.path_tessellation_nanoseconds == 0))
        result = 16;
    if (ready)
        nk_surface_make_current(surface);
    if (nkui_renderer_destroy(renderer) != NKUI_OK)
        result = 9;
    else if (nkui_renderer_destroy(renderer) != NKUI_ERROR_INVALID_HANDLE)
        result = 12;
    else if (nkui_renderer_create(&renderer) != NKUI_OK)
        result = 13;
    else if (nkui_renderer_render(renderer, list, surface) != NKUI_OK)
        result = 14;
    else if (nkui_renderer_destroy(renderer) != NKUI_OK)
        result = 15;
    nkui_display_list_destroy(list);
    nkui_resource_destroy(text);
    nkui_resource_destroy(fonts);
    nkui_resource_destroy(image);
    nkui_resource_destroy(paint);
    nkui_resource_destroy(path);
    nk_surface_destroy(surface);
    nk_window_destroy(window);
    nk_shutdown();
    return result;
}
