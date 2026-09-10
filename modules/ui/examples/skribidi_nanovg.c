#include "nativekit.h"
#include "nativekit_graphics.h"
#include "nativekit_window.h"

#define SOKOL_GLCORE
#include "sokol_gfx.h"
#include "nanovg.h"
#include "nanovg_sokol.h"
#include "skribidi/skb_attributes.h"
#include "skribidi/skb_font_collection.h"
#include "skribidi/skb_image_atlas.h"
#include "skribidi/skb_layout.h"
#include "skribidi/skb_rasterizer.h"

#include <GL/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { MAX_ATLAS_TEXTURES = 16, MAX_GLYPHS = 256 };

typedef struct demo_renderer {
    NVGcontext *vg;
    skb_font_collection_t *fonts;
    skb_temp_alloc_t *temp;
    skb_rasterizer_t *rasterizer;
    skb_image_atlas_t *atlas;
    skb_layout_t *layout;
    int images[MAX_ATLAS_TEXTURES];
    skb_quad_t quads[MAX_GLYPHS];
    int quad_count;
    int width;
    int height;
} demo_renderer;

static void sleep_milliseconds(unsigned milliseconds) {
    const struct timespec delay = {(time_t)(milliseconds / 1000),
                                   (long)(milliseconds % 1000) * 1000000L};
    nanosleep(&delay, NULL);
}

static int update_atlas(demo_renderer *renderer) {
    if (!skb_image_atlas_rasterize_missing_items(renderer->atlas, renderer->temp,
                                                  renderer->rasterizer))
        return 1;
    const int texture_count = skb_image_atlas_get_texture_count(renderer->atlas);
    if (texture_count > MAX_ATLAS_TEXTURES)
        return 0;
    for (int i = 0; i < texture_count; ++i) {
        const skb_rect2i_t dirty =
            skb_image_atlas_get_and_reset_texture_dirty_bounds(renderer->atlas, i);
        if (skb_rect2i_is_empty(dirty))
            continue;
        const skb_image_t *source = skb_image_atlas_get_texture(renderer->atlas, i);
        const size_t pixels = (size_t)source->width * (size_t)source->height;
        unsigned char *rgba = malloc(pixels * 4);
        if (!rgba)
            return 0;
        for (int y = 0; y < source->height; ++y) {
            const unsigned char *row = source->buffer + (size_t)y * source->stride_bytes;
            for (int x = 0; x < source->width; ++x) {
                unsigned char *dst = rgba + ((size_t)y * source->width + x) * 4;
                if (source->bpp == 1) {
                    dst[0] = dst[1] = dst[2] = 255;
                    dst[3] = row[x];
                } else {
                    memcpy(dst, row + (size_t)x * 4, 4);
                }
            }
        }
        if (!renderer->images[i])
            renderer->images[i] = nvgCreateImageRGBA(renderer->vg, source->width,
                                                      source->height, 0, rgba);
        else
            nvgUpdateImage(renderer->vg, renderer->images[i], rgba);
        free(rgba);
        if (!renderer->images[i])
            return 0;
    }
    return 1;
}

static int collect_glyphs(demo_renderer *renderer) {
    renderer->quad_count = 0;
    const skb_layout_params_t *params = skb_layout_get_params(renderer->layout);
    const skb_layout_line_t *lines = skb_layout_get_lines(renderer->layout);
    const skb_layout_run_t *runs = skb_layout_get_layout_runs(renderer->layout);
    const skb_glyph_t *glyphs = skb_layout_get_glyphs(renderer->layout);
    const int line_count = skb_layout_get_lines_count(renderer->layout);
    for (int li = 0; li < line_count; ++li) {
        for (int ri = lines[li].layout_run_range.start;
             ri < lines[li].layout_run_range.end; ++ri) {
            const skb_layout_run_t *run = &runs[ri];
            if (run->type != SKB_CONTENT_RUN_UTF8 && run->type != SKB_CONTENT_RUN_UTF32)
                continue;
            for (int gi = run->glyph_range.start; gi < run->glyph_range.end; ++gi) {
                if (renderer->quad_count == MAX_GLYPHS)
                    return 0;
                const skb_glyph_t *glyph = &glyphs[gi];
                renderer->quads[renderer->quad_count++] = skb_image_atlas_get_glyph_quad(
                    renderer->atlas, 70.0f + glyph->offset_x, 145.0f + glyph->offset_y,
                    1.0f, params->font_collection, run->font_handle, glyph->gid,
                    run->font_size, skb_rgba(255, 255, 255, 255),
                    SKB_RASTERIZE_ALPHA_MASK);
            }
        }
    }
    return update_atlas(renderer);
}

static int renderer_init(demo_renderer *renderer) {
    sg_setup(&(sg_desc){.environment.defaults = {.color_format = SG_PIXELFORMAT_RGBA8,
                                                  .depth_format = SG_PIXELFORMAT_DEPTH_STENCIL,
                                                  .sample_count = 1}});
    if (!sg_isvalid())
        return 0;
    renderer->vg = nvgCreateSokol(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
    renderer->fonts = skb_font_collection_create();
    renderer->temp = skb_temp_alloc_create(512 * 1024);
    renderer->rasterizer = skb_rasterizer_create(NULL);
    renderer->atlas = skb_image_atlas_create(NULL);
    if (!renderer->vg || !renderer->fonts || !renderer->temp || !renderer->rasterizer ||
        !renderer->atlas ||
        !skb_font_collection_add_font(renderer->fonts, NKUI_TEST_FONT_PATH,
                                      SKB_FONT_FAMILY_DEFAULT, NULL))
        return 0;

    const skb_attribute_t attributes[] = {
        skb_attribute_make_font_size(28.0f),
        skb_attribute_make_text_wrap(SKB_WRAP_WORD_CHAR),
        skb_attribute_make_paint_color(SKB_PAINT_TEXT, SKB_PAINT_STATE_DEFAULT,
                                       skb_rgba(255, 255, 255, 255)),
    };
    const skb_layout_params_t params = {.font_collection = renderer->fonts,
                                        .layout_width = 660.0f};
    const char *text = "Skribidi + NanoVG: مرحبا بالعالم — native shaping";
    renderer->layout = skb_layout_create_utf8(
        renderer->temp, &params, text, -1,
        SKB_ATTRIBUTE_SET_FROM_STATIC_ARRAY(attributes));
    return renderer->layout && collect_glyphs(renderer);
}

static void draw_glyph(demo_renderer *renderer, const skb_quad_t *quad) {
    if ((quad->flags & SKB_QUAD_IS_EMPTY) || quad->texture_idx >= MAX_ATLAS_TEXTURES)
        return;
    const skb_image_t *atlas =
        skb_image_atlas_get_texture(renderer->atlas, quad->texture_idx);
    const float u = (quad->texture.x + quad->pattern.x * quad->texture.width) /
                    (float)atlas->width;
    const float v = (quad->texture.y + quad->pattern.y * quad->texture.height) /
                    (float)atlas->height;
    const float uw = quad->pattern.width * quad->texture.width / (float)atlas->width;
    const float vh = quad->pattern.height * quad->texture.height / (float)atlas->height;
    if (uw == 0.0f || vh == 0.0f)
        return;
    const float full_width = quad->geom.width / uw;
    const float full_height = quad->geom.height / vh;
    const NVGpaint image = nvgImagePattern(
        renderer->vg, quad->geom.x - u * full_width,
        quad->geom.y - v * full_height, full_width, full_height, 0.0f,
        renderer->images[quad->texture_idx], 1.0f);
    nvgBeginPath(renderer->vg);
    nvgRect(renderer->vg, quad->geom.x, quad->geom.y, quad->geom.width,
            quad->geom.height);
    nvgFillPaint(renderer->vg, image);
    nvgFill(renderer->vg);
}

static void renderer_draw(demo_renderer *renderer) {
    GLint framebuffer = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &framebuffer);
    const sg_pass pass = {
        .action.colors[0] = {.load_action = SG_LOADACTION_CLEAR,
                             .clear_value = {0.025f, 0.035f, 0.07f, 1.0f}},
        .action.depth = {.load_action = SG_LOADACTION_CLEAR, .clear_value = 1.0f},
        .action.stencil = {.load_action = SG_LOADACTION_CLEAR, .clear_value = 0},
        .swapchain = {.width = renderer->width,
                      .height = renderer->height,
                      .sample_count = 1,
                      .color_format = SG_PIXELFORMAT_RGBA8,
                      .depth_format = SG_PIXELFORMAT_DEPTH_STENCIL,
                      .gl.framebuffer = (uint32_t)framebuffer}};
    sg_begin_pass(&pass);
    nvgBeginFrame(renderer->vg, (float)renderer->width, (float)renderer->height, 1.0f);
    nvgBeginPath(renderer->vg);
    nvgRoundedRect(renderer->vg, 42.0f, 72.0f, 716.0f, 150.0f, 20.0f);
    nvgFillColor(renderer->vg, nvgRGBA(34, 64, 122, 255));
    nvgFill(renderer->vg);
    for (int i = 0; i < renderer->quad_count; ++i)
        draw_glyph(renderer, &renderer->quads[i]);
    nvgEndFrame(renderer->vg);
    sg_end_pass();
    sg_commit();
}

static void renderer_destroy(demo_renderer *renderer) {
    for (int i = 0; i < MAX_ATLAS_TEXTURES; ++i)
        if (renderer->images[i])
            nvgDeleteImage(renderer->vg, renderer->images[i]);
    skb_layout_destroy(renderer->layout);
    skb_image_atlas_destroy(renderer->atlas);
    skb_rasterizer_destroy(renderer->rasterizer);
    skb_temp_alloc_destroy(renderer->temp);
    skb_font_collection_destroy(renderer->fonts);
    nvgDeleteSokol(renderer->vg);
    sg_shutdown();
}

int main(int argc, char **argv) {
    const int smoke = argc == 2 && strcmp(argv[1], "--smoke-test") == 0;
    if (argc > 1 && !smoke)
        return 2;
    nk_init_options init = {.struct_size = sizeof(init), .api_version = NK_API_VERSION};
    if (nk_init(&init) != NK_OK)
        return 1;
    nk_window_options wo = {.struct_size = sizeof(wo),
                            .flags = NK_WINDOW_RESIZABLE,
                            .width = 800,
                            .height = 300,
                            .title = "NativeKit UI vertical slice"};
    nk_handle window = NK_INVALID_HANDLE;
    if (nk_window_create(&wo, &window) != NK_OK)
        return 1;
    nk_surface_options so = {.struct_size = sizeof(so),
                             .flags = NK_SURFACE_FORWARD_COMPATIBLE | NK_SURFACE_STENCIL,
                             .api = NK_GRAPHICS_OPENGL,
                             .major_version = 3,
                             .minor_version = 3,
                             .width = wo.width,
                             .height = wo.height};
    nk_handle surface = NK_INVALID_HANDLE;
    if (nk_surface_create(window, &so, &surface) != NK_OK)
        return 1;

    demo_renderer renderer = {0};
    int ready = 0, running = 1, frames = 0;
    while (running) {
        nk_event event = {.struct_size = sizeof(event)};
        if (nk_poll_event(&event) != NK_OK)
            running = 0;
        else if (event.kind == NK_EVENT_WINDOW_CLOSE && event.source == window)
            running = 0;
        else if (event.kind == NK_EVENT_SURFACE_READY && event.source == surface) {
            nk_surface_make_current(surface);
            nk_surface_get_framebuffer_size(surface, &renderer.width, &renderer.height);
            ready = renderer_init(&renderer);
            if (!ready)
                running = 0;
        } else if (event.kind == NK_EVENT_SURFACE_RESIZE && event.source == surface &&
                   event.data_size >= sizeof(nk_surface_resize_event)) {
            const nk_surface_resize_event *size = event.data;
            renderer.width = size->framebuffer_width;
            renderer.height = size->framebuffer_height;
        }
        const int empty = event.kind == NK_EVENT_NONE;
        nk_event_release(&event);
        if (ready && running) {
            nk_surface_make_current(surface);
            renderer_draw(&renderer);
            nk_surface_present(surface);
            if (smoke && ++frames >= 30)
                running = 0;
            sleep_milliseconds(16);
        } else if (empty) {
            sleep_milliseconds(8);
        }
    }
    if (ready) {
        nk_surface_make_current(surface);
        renderer_destroy(&renderer);
    }
    nk_surface_destroy(surface);
    nk_window_destroy(window);
    nk_shutdown();
    if (smoke && frames == 30)
        puts("NativeKit UI Skribidi + NanoVG 30-frame smoke test passed");
    return smoke && frames != 30;
}
