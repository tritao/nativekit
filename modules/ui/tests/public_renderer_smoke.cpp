#include "nativekit.h"
#include "nativekit_graphics.h"
#include "nativekit_gpu.h"
#include "nativekit_ui.h"
#include "nativekit_window.h"
#include "testing.h"

#if defined(__EMSCRIPTEN__) || defined(__ANDROID__)
#include <GLES3/gl3.h>
#elif defined(__APPLE__)
#include <OpenGL/gl3.h>
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <GL/gl.h>
#else
#include <GL/gl.h>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
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

struct PixelBounds {
    int min_x = 0;
    int min_y = 0;
    int max_x = -1;
    int max_y = -1;

    int width() const { return max_x >= min_x ? max_x - min_x + 1 : 0; }
    int height() const { return max_y >= min_y ? max_y - min_y + 1 : 0; }
};

int main(int argc, char **argv) {
    const bool stress_mode = argc > 1 && std::strcmp(argv[1], "--stress") == 0;
    constexpr uint32_t stress_seed = 0x51B6A7E1u;
    uint32_t stress_random = stress_seed;
    auto next_stress_random = [&]() {
        stress_random = stress_random * 1664525u + 1013904223u;
        return stress_random;
    };
    constexpr std::array<float, 6> pixel_scales{1.0f, 1.25f, 1.5f, 2.0f, 1.0f, 2.0f};
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
    nk_window window = 0;
    if (nk_window_create(&window_options, &window) != NK_OK)
        return 2;
    nk_surface_options surface_options{};
    surface_options.struct_size = sizeof(surface_options);
    surface_options.api = nkgpu_default_graphics_api();
    surface_options.flags = NK_SURFACE_STENCIL;
    if (surface_options.api == NK_GRAPHICS_OPENGL)
        surface_options.flags |= NK_SURFACE_FORWARD_COMPATIBLE;
    surface_options.major_version = 3;
    if (surface_options.api == NK_GRAPHICS_OPENGL)
        surface_options.minor_version = 3;
    surface_options.width = window_options.width;
    surface_options.height = window_options.height;
    nk_surface surface = 0;
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
    const nkui_gradient_stop gradient_stops[] = {
        {0.0f, {1.0f, 0.08f, 0.04f, 1.0f}},
        {0.5f, {0.95f, 0.8f, 0.08f, 1.0f}},
        {1.0f, {0.08f, 0.25f, 1.0f, 1.0f}},
    };
    nkui_resource path{}, paint{}, gradient_paint{}, stroke_paint{}, image{}, fonts{}, text{},
        scale_text{};
    nkui_display_list list{};
    nkui_display_list scale_list{};
    nkui_display_list drop_shadow_list{};
    nkui_display_list mask_list{};
    nkui_display_list image_mask_list{};
    nkui_renderer renderer{};
    if (nkui_path_create(path_elements, 5, &path) != NKUI_OK ||
        nkui_paint_create_solid({0.08f, 0.45f, 0.16f, 1.0f}, &paint) != NKUI_OK ||
        nkui_paint_create_linear_gradient(12.0f, 0.0f, 244.0f, 0.0f, gradient_stops,
                                          sizeof(gradient_stops) / sizeof(gradient_stops[0]),
                                          &gradient_paint) != NKUI_OK ||
        nkui_paint_create_solid({0.85f, 0.12f, 0.08f, 1.0f}, &stroke_paint) != NKUI_OK ||
        nkui_image_create(1, 1, NKUI_IMAGE_RGBA8, image_pixels, sizeof(image_pixels), &image) !=
            NKUI_OK ||
        nkui_font_collection_create(&fonts) != NKUI_OK ||
        nkui_font_collection_add(fonts, NKUI_TEST_FONT_PATH, NKUI_FONT_FAMILY_DEFAULT) != NKUI_OK ||
        nkui_text_layout_create(fonts, "NativeKit direct text", 220.0f, 24.0f, &text) != NKUI_OK ||
        nkui_text_layout_create(fonts, "Scale", 100.0f, 18.0f, &scale_text) != NKUI_OK ||
        nkui_display_list_create(&list) != NKUI_OK || nkui_renderer_create(&renderer) != NKUI_OK)
        return 4;
    if (nkui_text_layout_set_text(text, "NativeKit updated text") != NKUI_OK)
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
    append(commands, nkui_resource_command{{NKUI_COMMAND_SET_PAINT, NKUI_COMMAND_VERSION,
                                            sizeof(nkui_resource_command)},
                                           gradient_paint});
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
    append(commands, nkui_draw_rect_command{{NKUI_COMMAND_DRAW_TEXT_LAYOUT, NKUI_COMMAND_VERSION,
                                             sizeof(nkui_draw_rect_command)},
                                            scale_text,
                                            4.0f,
                                            140.0f,
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
    nkui_layer_effect_command blur_layer{};
    blur_layer.header = {NKUI_COMMAND_BEGIN_LAYER, NKUI_COMMAND_VERSION,
                         sizeof(nkui_layer_effect_command)};
    blur_layer.opacity = 1.0f;
    blur_layer.composite_mode = NKUI_COMPOSITE_SOURCE_OVER;
    blur_layer.x = 72.0f;
    blur_layer.y = 82.0f;
    blur_layer.width = 112.0f;
    blur_layer.height = 70.0f;
    blur_layer.flags = NKUI_LAYER_ISOLATED | NKUI_LAYER_HAS_BOUNDS;
    blur_layer.effect_kind = NKUI_EFFECT_BLUR;
    blur_layer.effect_matrix[0] = 2.0f;
    append(commands, blur_layer);
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
    if (nkui_display_list_create(&drop_shadow_list) != NKUI_OK)
        return 5;
    std::vector<uint8_t> drop_shadow_commands;
    nkui_layer_effect_command drop_shadow_layer{};
    drop_shadow_layer.header = {NKUI_COMMAND_BEGIN_LAYER, NKUI_COMMAND_VERSION,
                                sizeof(nkui_layer_effect_command)};
    drop_shadow_layer.opacity = 1.0f;
    drop_shadow_layer.composite_mode = NKUI_COMPOSITE_SOURCE_OVER;
    drop_shadow_layer.x = 72.0f;
    drop_shadow_layer.y = 82.0f;
    drop_shadow_layer.width = 112.0f;
    drop_shadow_layer.height = 70.0f;
    drop_shadow_layer.flags = NKUI_LAYER_ISOLATED | NKUI_LAYER_HAS_BOUNDS;
    drop_shadow_layer.effect_kind = NKUI_EFFECT_DROP_SHADOW;
    drop_shadow_layer.effect_matrix[0] = 2.0f;
    drop_shadow_layer.effect_matrix[3] = 6.0f;
    drop_shadow_layer.effect_matrix[7] = 0.35f;
    append(drop_shadow_commands, drop_shadow_layer);
    append(drop_shadow_commands,
           nkui_draw_rect_command{{NKUI_COMMAND_DRAW_IMAGE, NKUI_COMMAND_VERSION,
                                   sizeof(nkui_draw_rect_command)},
                                  image,
                                  72.0f,
                                  82.0f,
                                  112.0f,
                                  70.0f});
    append(drop_shadow_commands,
           nkui_command_header{NKUI_COMMAND_END_LAYER, NKUI_COMMAND_VERSION,
                               sizeof(nkui_command_header)});
    if (nkui_display_list_submit(drop_shadow_list, drop_shadow_commands.data(),
                                 drop_shadow_commands.size()) != NKUI_OK)
        return 5;
    if (nkui_display_list_create(&mask_list) != NKUI_OK)
        return 5;
    std::vector<uint8_t> mask_commands;
    nkui_layer_mask_command rounded_mask_layer{};
    rounded_mask_layer.header = {NKUI_COMMAND_BEGIN_LAYER, NKUI_COMMAND_VERSION,
                                 sizeof(nkui_layer_mask_command)};
    rounded_mask_layer.opacity = 1.0f;
    rounded_mask_layer.composite_mode = NKUI_COMPOSITE_SOURCE_OVER;
    rounded_mask_layer.x = 72.0f;
    rounded_mask_layer.y = 82.0f;
    rounded_mask_layer.width = 112.0f;
    rounded_mask_layer.height = 70.0f;
    rounded_mask_layer.flags = NKUI_LAYER_ISOLATED | NKUI_LAYER_HAS_BOUNDS;
    rounded_mask_layer.mask.kind = NKUI_MASK_ROUNDED_RECT;
    rounded_mask_layer.mask.values[0] = 10.0f;
    append(mask_commands, rounded_mask_layer);
    append(mask_commands,
           nkui_draw_rect_command{{NKUI_COMMAND_DRAW_IMAGE, NKUI_COMMAND_VERSION,
                                   sizeof(nkui_draw_rect_command)},
                                  image,
                                  72.0f,
                                  82.0f,
                                  112.0f,
                                  70.0f});
    append(mask_commands,
           nkui_command_header{NKUI_COMMAND_END_LAYER, NKUI_COMMAND_VERSION,
                               sizeof(nkui_command_header)});
    if (nkui_display_list_submit(mask_list, mask_commands.data(), mask_commands.size()) != NKUI_OK)
        return 5;
    if (nkui_display_list_create(&image_mask_list) != NKUI_OK)
        return 5;
    std::vector<uint8_t> image_mask_commands;
    nkui_layer_mask_command image_mask_layer = rounded_mask_layer;
    image_mask_layer.mask.kind = NKUI_MASK_IMAGE;
    image_mask_layer.mask.image = image;
    append(image_mask_commands, image_mask_layer);
    append(image_mask_commands,
           nkui_draw_rect_command{{NKUI_COMMAND_DRAW_IMAGE, NKUI_COMMAND_VERSION,
                                   sizeof(nkui_draw_rect_command)},
                                  image,
                                  72.0f,
                                  82.0f,
                                  112.0f,
                                  70.0f});
    append(image_mask_commands,
           nkui_command_header{NKUI_COMMAND_END_LAYER, NKUI_COMMAND_VERSION,
                               sizeof(nkui_command_header)});
    if (nkui_display_list_submit(image_mask_list, image_mask_commands.data(),
                                 image_mask_commands.size()) != NKUI_OK)
        return 5;
    if (nkui_display_list_create(&scale_list) != NKUI_OK)
        return 5;
    std::vector<uint8_t> scale_commands;
    append(scale_commands,
           nkui_draw_rect_command{{NKUI_COMMAND_DRAW_TEXT_LAYOUT, NKUI_COMMAND_VERSION,
                                   sizeof(nkui_draw_rect_command)},
                                  scale_text,
                                  4.0f,
                                  40.0f,
                                  0.0f,
                                  0.0f});
    if (nkui_display_list_submit(scale_list, scale_commands.data(), scale_commands.size()) !=
        NKUI_OK)
        return 5;
    if (nkui_resource_destroy(image) != NKUI_OK || nkui_resource_destroy(stroke_paint) != NKUI_OK ||
        nkui_resource_destroy(gradient_paint) != NKUI_OK || nkui_resource_destroy(paint) != NKUI_OK ||
        nkui_resource_destroy(path) != NKUI_OK)
        return 5;

    nkui_text_metrics stable_metrics{};
    nkui_text_position stable_start{}, stable_end{};
    nkui_text_caret stable_caret{};
    std::array<uint8_t, sizeof(nkui_text_rect) * 8> stable_selection{};
    uint32_t stable_selection_bytes = static_cast<uint32_t>(stable_selection.size());
    if (nkui_text_layout_measure(scale_text, &stable_metrics) != NKUI_OK ||
        nkui_text_layout_hit_test(scale_text, 5.0f, 8.0f, &stable_start) != NKUI_OK ||
        nkui_text_layout_hit_test(scale_text, 70.0f, 8.0f, &stable_end) != NKUI_OK ||
        nkui_text_layout_caret(scale_text, stable_start, &stable_caret) != NKUI_OK ||
        nkui_text_layout_get_selection_rects(scale_text, stable_start, stable_end,
                                             stable_selection.data(), &stable_selection_bytes) !=
            NKUI_OK)
        return 5;

    int result = 0;
    bool ready = false;
    int width = 0, height = 0, frames = 0;
    std::array<uint64_t, 6> resource_highwater{};
    const int frame_limit = stress_mode ? 120 : 30;
    while (!result && frames < frame_limit) {
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
        const float pixel_scale = pixel_scales[static_cast<size_t>(frames) % pixel_scales.size()];
        if (stress_mode && frames % 7 == 0) {
            char updated_text[64]{};
            std::snprintf(updated_text, sizeof(updated_text), "Typed frame %03d", frames);
            if (nkui_text_layout_set_text(text, updated_text) != NKUI_OK)
                result = 21;
        }
        if (result)
            break;
        const nkui_frame_info frame_info{sizeof(frame_info), width / pixel_scale,
                                         height / pixel_scale, width, height, pixel_scale};
        nkui_result render_result = NKUI_OK;
        if (!result && frames == 0)
            render_result =
                nkui_renderer_render_frame(renderer, mask_list, surface, &frame_info);
        if (!result && render_result == NKUI_OK && frames == 0)
            render_result =
                nkui_renderer_render_frame(renderer, image_mask_list, surface, &frame_info);
        if (!result && render_result == NKUI_OK && frames == 0)
            render_result =
                nkui_renderer_render_frame(renderer, drop_shadow_list, surface, &frame_info);
        if (!result && render_result == NKUI_OK)
            render_result = nkui_renderer_render_frame(renderer, list, surface, &frame_info);
        if (render_result != NKUI_OK) {
            std::fprintf(stderr, "public render result %d: %s\n", render_result,
                         nkgpu_last_error());
            result = 7;
        } else if (frames == 0) {
            if (nkui_renderer_render_frame_overlay(renderer, list, surface, &frame_info) !=
                NKUI_OK)
                result = 18;
            uint8_t gradient_left[4]{};
            uint8_t gradient_right[4]{};
            uint8_t image_sample[4]{};
            glReadPixels(24, height - 24, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, gradient_left);
            glReadPixels(230, height - 24, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, gradient_right);
            glReadPixels(120, height - 110, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, image_sample);
            if (gradient_left[0] <= gradient_left[2] ||
                gradient_right[2] <= gradient_right[0] || image_sample[2] <= image_sample[1])
                result = 11;
        }
        if (!result) {
            nkui_text_metrics current_metrics{};
            nkui_text_position current_position{};
            nkui_text_caret current_caret{};
            std::array<uint8_t, sizeof(nkui_text_rect) * 8> current_selection{};
            uint32_t current_selection_bytes =
                static_cast<uint32_t>(current_selection.size());
            if (nkui_text_layout_measure(scale_text, &current_metrics) != NKUI_OK ||
                nkui_text_layout_hit_test(scale_text, 5.0f, 8.0f, &current_position) != NKUI_OK ||
                nkui_text_layout_caret(scale_text, current_position, &current_caret) != NKUI_OK ||
                nkui_text_layout_get_selection_rects(scale_text, stable_start, stable_end,
                                                     current_selection.data(),
                                                     &current_selection_bytes) != NKUI_OK ||
                current_metrics.width != stable_metrics.width ||
                current_metrics.height != stable_metrics.height ||
                current_position.offset != stable_start.offset ||
                current_position.affinity != stable_start.affinity ||
                std::abs(current_caret.x - stable_caret.x) > 0.001f ||
                std::abs(current_caret.y - stable_caret.y) > 0.001f ||
                std::abs(current_caret.ascender - stable_caret.ascender) > 0.001f ||
                std::abs(current_caret.descender - stable_caret.descender) > 0.001f ||
                current_selection_bytes != stable_selection_bytes ||
                std::memcmp(current_selection.data(), stable_selection.data(),
                            stable_selection_bytes) != 0)
                result = 22;
        }
        if (!result && nk_surface_present(surface) != NK_OK)
            result = 10;
        if (!result && stress_mode && frames % 9 == 0) {
            nkgpu_renderer gpu_renderer{};
            nkgpu_render_target target{};
            nk_graphics_image sampled{};
            nk_graphics_image_info info{};
            info.struct_size = sizeof(info);
            const uint32_t size = 8u + (next_stress_random() & 0x0Fu);
            bool retained = false;
            bool cycle_ok = nkgpu_renderer_create(surface, &gpu_renderer) == NKGPU_OK;
            if (cycle_ok)
                cycle_ok = nkgpu_render_target_create(gpu_renderer, size, size, frames & 1u,
                                                      &target) == NKGPU_OK;
            if (cycle_ok)
                cycle_ok = nkgpu_render_target_get_image(gpu_renderer, target, &sampled) ==
                           NKGPU_OK;
            if (cycle_ok) {
                cycle_ok = nk_graphics_image_retain(sampled) == NK_OK;
                retained = cycle_ok;
            }
            if (cycle_ok) {
                cycle_ok = nkgpu_render_target_destroy(gpu_renderer, target) == NKGPU_OK;
                if (cycle_ok)
                    target = {};
            }
            if (cycle_ok)
                cycle_ok = nk_graphics_image_get_info(sampled, &info) == NK_OK &&
                           info.width == size && info.height == size;
            if (retained) {
                cycle_ok = nk_graphics_image_release(sampled) == NK_OK && cycle_ok;
                retained = false;
            }
            if (gpu_renderer.id) {
                if (nkgpu_renderer_destroy(gpu_renderer) == NKGPU_OK)
                    gpu_renderer = {};
                else
                    cycle_ok = false;
            }
            if (!cycle_ok) {
                if (retained)
                    nk_graphics_image_release(sampled);
                if (target.id && gpu_renderer.id)
                    nkgpu_render_target_destroy(gpu_renderer, target);
                if (gpu_renderer.id)
                    nkgpu_renderer_destroy(gpu_renderer);
                result = 19;
            }
        }
        if (!result && stress_mode && frames > 0 && frames % 40 == 0) {
            const int32_t requested_width = frames % 80 == 0 ? 256 : 288;
            const int32_t requested_height = frames % 80 == 0 ? 192 : 216;
            if (nk_surface_set_bounds(surface, 0, 0, requested_width, requested_height) != NK_OK)
                result = 20;
        }
        if (!result && frames == frame_limit - 3)
            nkgpu_test_lose_all_after_frames(1);
        if (!result && stress_mode && (frames + 1) % 30 == 0) {
            nkui_renderer_stats snapshot{};
            if (nkui_renderer_get_stats(renderer, &snapshot) != NKUI_OK) {
                result = 23;
            } else {
                resource_highwater[0] =
                    std::max<uint64_t>(resource_highwater[0], snapshot.buffers_live);
                resource_highwater[1] =
                    std::max<uint64_t>(resource_highwater[1], snapshot.images_live);
                resource_highwater[2] = std::max<uint64_t>(resource_highwater[2],
                                                            snapshot.render_targets_live);
                resource_highwater[3] =
                    std::max<uint64_t>(resource_highwater[3], snapshot.buffer_bytes);
                resource_highwater[4] =
                    std::max<uint64_t>(resource_highwater[4], snapshot.image_bytes);
                resource_highwater[5] = std::max<uint64_t>(resource_highwater[5],
                                                            snapshot.render_target_bytes);
            }
        }
        ++frames;
    }
    if (!result && frames != frame_limit)
        result = 8;
    if (!result) {
        auto read_text_bounds = [&]() {
            PixelBounds bounds;
            std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
            glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const size_t offset = (static_cast<size_t>(y) * width + x) * 4;
                    if (pixels[offset] < 80 || pixels[offset + 1] < 80 || pixels[offset + 2] < 80)
                        continue;
                    if (bounds.max_x < bounds.min_x) {
                        bounds.min_x = x;
                        bounds.min_y = y;
                    } else {
                        bounds.min_x = std::min(bounds.min_x, x);
                        bounds.min_y = std::min(bounds.min_y, y);
                    }
                    bounds.max_x = std::max(bounds.max_x, x);
                    bounds.max_y = std::max(bounds.max_y, y);
                }
            }
            return bounds;
        };
        auto render_scale_frame = [&](float pixel_scale, PixelBounds &bounds) {
            const nkui_frame_info frame_info{
                sizeof(frame_info), width / pixel_scale, height / pixel_scale,
                width, height, pixel_scale};
            if (nkui_renderer_render_frame(renderer, scale_list, surface, &frame_info) != NKUI_OK)
                return false;
            bounds = read_text_bounds();
            return nk_surface_present(surface) == NK_OK;
        };
        PixelBounds native_bounds;
        PixelBounds scaled_bounds;
        if (!render_scale_frame(1.0f, native_bounds) ||
            !render_scale_frame(2.0f, scaled_bounds) || native_bounds.width() < 10 ||
            native_bounds.height() < 5 || scaled_bounds.width() < native_bounds.width() * 1.5f ||
            scaled_bounds.height() < native_bounds.height() * 1.5f)
            result = 17;
    }
    nkui_renderer_stats stats{};
    if (!result && (nkui_renderer_get_stats(renderer, &stats) != NKUI_OK ||
                    stats.struct_size != sizeof(stats) || stats.path_preparations < 8 ||
                    stats.path_preparations != stats.path_cache_misses ||
                    stats.path_cache_hits == 0 || stats.path_vertices_generated == 0 ||
                    stats.path_geometry_bytes_allocated == 0 ||
                    stats.path_geometry_bytes_retained == 0 ||
                    stats.path_tessellation_nanoseconds == 0 || stats.gpu_frames < frames ||
                    stats.device_losses == 0 || stats.atlas_pages == 0 ||
                    stats.atlas_pages > 32 || stats.atlas_bytes == 0 ||
                    stats.atlas_bytes > 64u * 1024u * 1024u ||
                    stats.atlas_scale_generation < 4 || stats.glyph_uploads == 0 ||
                    stats.glyphs_rasterized == 0 || stats.atlas_rebuilds == 0 ||
                    stats.atlas_dirty_upload_bytes == 0 ||
                    stats.display_list_count < frames || stats.display_list_bytes == 0 ||
                    stats.render_plan_commands == 0 || stats.text_layout_cache_misses == 0 ||
                    (stress_mode && (stats.gpu_frames < 120 || stats.buffers_live > 16 ||
                                     stats.images_live > 64 || stats.render_targets_live > 4 ||
                                     stats.buffer_bytes > 64u * 1024u * 1024u ||
                                     resource_highwater[0] > 16 || resource_highwater[1] > 64 ||
                                     resource_highwater[2] > 4 ||
                                     resource_highwater[3] > 64u * 1024u * 1024u ||
                                     resource_highwater[4] > 64u * 1024u * 1024u ||
                                     resource_highwater[5] > 64u * 1024u * 1024u))))
        result = 16;
    if (result == 16)
        std::fprintf(stderr, "stats: paths=%llu misses=%llu hits=%llu frames=%llu losses=%llu "
                             "atlas=%llu/%llu generation=%llu glyphs=%llu/%llu rebuilds=%llu "
                             "dirty=%llu lists=%llu bytes=%llu plans=%llu layouts=%llu "
                             "buffers=%llu images=%llu targets=%llu\n",
                     static_cast<unsigned long long>(stats.path_preparations),
                     static_cast<unsigned long long>(stats.path_cache_misses),
                     static_cast<unsigned long long>(stats.path_cache_hits),
                     static_cast<unsigned long long>(stats.gpu_frames),
                     static_cast<unsigned long long>(stats.device_losses),
                     static_cast<unsigned long long>(stats.atlas_pages),
                     static_cast<unsigned long long>(stats.atlas_bytes),
                     static_cast<unsigned long long>(stats.atlas_scale_generation),
                     static_cast<unsigned long long>(stats.glyph_uploads),
                     static_cast<unsigned long long>(stats.glyphs_rasterized),
                     static_cast<unsigned long long>(stats.atlas_rebuilds),
                     static_cast<unsigned long long>(stats.atlas_dirty_upload_bytes),
                     static_cast<unsigned long long>(stats.display_list_count),
                     static_cast<unsigned long long>(stats.display_list_bytes),
                     static_cast<unsigned long long>(stats.render_plan_commands),
                     static_cast<unsigned long long>(stats.text_layout_cache_misses),
                     static_cast<unsigned long long>(stats.buffers_live),
                     static_cast<unsigned long long>(stats.images_live),
                     static_cast<unsigned long long>(stats.render_targets_live));
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
    nkui_display_list_destroy(scale_list);
    nkui_display_list_destroy(drop_shadow_list);
    nkui_display_list_destroy(mask_list);
    nkui_display_list_destroy(image_mask_list);
    nkui_display_list_destroy(list);
    nkui_resource_destroy(text);
    nkui_resource_destroy(scale_text);
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
