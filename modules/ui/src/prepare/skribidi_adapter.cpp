#include "skribidi_adapter.h"

#include "skribidi/skb_attributes.h"
#include "skribidi/skb_font_collection.h"
#include "skribidi/skb_image_atlas.h"
#include "skribidi/skb_layout.h"
#include "skribidi/skb_rasterizer.h"

#include <cstring>

namespace nkui {

struct SkribidiAdapter::State {
    skb_font_collection_t *fonts = nullptr;
    skb_temp_alloc_t *temporary = nullptr;
    skb_rasterizer_t *rasterizer = nullptr;
    skb_image_atlas_t *atlas = nullptr;
    skb_layout_t *layout = nullptr;
};

namespace {

skb_rasterize_alpha_mode_t raster_mode(GlyphMode mode) {
    return mode == GlyphMode::Sdf ? SKB_RASTERIZE_ALPHA_SDF : SKB_RASTERIZE_ALPHA_MASK;
}

GlyphMode quad_mode(const skb_quad_t &quad, GlyphMode requested) {
    if (quad.flags & SKB_QUAD_IS_COLOR)
        return GlyphMode::Color;
    if (quad.flags & SKB_QUAD_IS_SDF)
        return GlyphMode::Sdf;
    return requested == GlyphMode::Color ? GlyphMode::Alpha : requested;
}

void append_quad(const skb_quad_t &quad, const skb_image_t &atlas, PreparedGlyphs &output) {
    const float u0 =
        (quad.texture.x + quad.pattern.x * quad.texture.width) / static_cast<float>(atlas.width);
    const float v0 =
        (quad.texture.y + quad.pattern.y * quad.texture.height) / static_cast<float>(atlas.height);
    const float u1 = u0 + quad.pattern.width * quad.texture.width / static_cast<float>(atlas.width);
    const float v1 =
        v0 + quad.pattern.height * quad.texture.height / static_cast<float>(atlas.height);
    const uint32_t base = static_cast<uint32_t>(output.vertices.size());
    const auto vertex = [&quad](float x, float y, float u, float v) {
        return GlyphVertex{x, y, u, v, quad.color.r, quad.color.g, quad.color.b, quad.color.a};
    };
    output.vertices.push_back(vertex(quad.geom.x, quad.geom.y, u0, v0));
    output.vertices.push_back(vertex(quad.geom.x + quad.geom.width, quad.geom.y, u1, v0));
    output.vertices.push_back(
        vertex(quad.geom.x + quad.geom.width, quad.geom.y + quad.geom.height, u1, v1));
    output.vertices.push_back(vertex(quad.geom.x, quad.geom.y + quad.geom.height, u0, v1));
    const uint32_t indices[] = {base, base + 1, base + 2, base, base + 2, base + 3};
    output.indices.insert(output.indices.end(), std::begin(indices), std::end(indices));
}

} // namespace

SkribidiAdapter::SkribidiAdapter() : state_(new State) {
    state_->fonts = skb_font_collection_create();
    state_->temporary = skb_temp_alloc_create(512 * 1024);
    state_->rasterizer = skb_rasterizer_create(nullptr);
    state_->atlas = skb_image_atlas_create(nullptr);
}

SkribidiAdapter::~SkribidiAdapter() {
    if (state_->layout)
        skb_layout_destroy(state_->layout);
    if (state_->atlas)
        skb_image_atlas_destroy(state_->atlas);
    if (state_->rasterizer)
        skb_rasterizer_destroy(state_->rasterizer);
    if (state_->temporary)
        skb_temp_alloc_destroy(state_->temporary);
    if (state_->fonts)
        skb_font_collection_destroy(state_->fonts);
    delete state_;
}

bool SkribidiAdapter::valid() const {
    return state_->fonts && state_->temporary && state_->rasterizer && state_->atlas;
}

bool SkribidiAdapter::add_font(const char *path) {
    return path &&
           skb_font_collection_add_font(state_->fonts, path, SKB_FONT_FAMILY_DEFAULT, nullptr);
}

bool SkribidiAdapter::layout_utf8(const char *text, float width, float font_size) {
    if (!valid() || !text || width <= 0.0f || font_size <= 0.0f)
        return false;
    if (state_->layout) {
        skb_layout_destroy(state_->layout);
        state_->layout = nullptr;
    }
    const skb_attribute_t attributes[] = {
        skb_attribute_make_font_size(font_size),
        skb_attribute_make_text_wrap(SKB_WRAP_WORD_CHAR),
        skb_attribute_make_paint_color(SKB_PAINT_TEXT, SKB_PAINT_STATE_DEFAULT,
                                       skb_rgba(255, 255, 255, 255)),
    };
    const skb_layout_params_t params = {.font_collection = state_->fonts, .layout_width = width};
    state_->layout = skb_layout_create_utf8(state_->temporary, &params, text, -1,
                                            SKB_ATTRIBUTE_SET_FROM_STATIC_ARRAY(attributes));
    return state_->layout != nullptr;
}

bool SkribidiAdapter::prepare_glyphs(float origin_x, float origin_y, float pixel_scale,
                                     GlyphMode mode, PreparedGlyphs &output) {
    if (!state_->layout || pixel_scale <= 0.0f)
        return false;
    output = {};
    const auto *params = skb_layout_get_params(state_->layout);
    const auto *lines = skb_layout_get_lines(state_->layout);
    const auto *runs = skb_layout_get_layout_runs(state_->layout);
    const auto *glyphs = skb_layout_get_glyphs(state_->layout);
    const int line_count = skb_layout_get_lines_count(state_->layout);
    for (int line_index = 0; line_index < line_count; ++line_index) {
        const auto range = lines[line_index].layout_run_range;
        for (int run_index = range.start; run_index < range.end; ++run_index) {
            const auto &run = runs[run_index];
            if (run.type != SKB_CONTENT_RUN_UTF8 && run.type != SKB_CONTENT_RUN_UTF32)
                continue;
            for (int glyph_index = run.glyph_range.start; glyph_index < run.glyph_range.end;
                 ++glyph_index) {
                const auto &glyph = glyphs[glyph_index];
                const skb_quad_t quad = skb_image_atlas_get_glyph_quad(
                    state_->atlas, origin_x + glyph.offset_x, origin_y + glyph.offset_y,
                    pixel_scale, params->font_collection, run.font_handle, glyph.gid, run.font_size,
                    skb_rgba(255, 255, 255, 255), raster_mode(mode));
                if (quad.flags & SKB_QUAD_IS_EMPTY)
                    continue;
                const GlyphMode actual_mode = quad_mode(quad, mode);
                if (output.batches.empty() ||
                    output.batches.back().atlas_texture != quad.texture_idx ||
                    output.batches.back().mode != actual_mode) {
                    output.batches.push_back({quad.texture_idx, actual_mode,
                                              static_cast<uint32_t>(output.vertices.size()), 0,
                                              static_cast<uint32_t>(output.indices.size()), 0});
                }
                const skb_image_t *atlas =
                    skb_image_atlas_get_texture(state_->atlas, quad.texture_idx);
                if (!atlas)
                    return false;
                append_quad(quad, *atlas, output);
                auto &batch = output.batches.back();
                batch.vertex_count += 4;
                batch.index_count += 6;
            }
        }
    }
    return skb_image_atlas_rasterize_missing_items(state_->atlas, state_->temporary,
                                                   state_->rasterizer);
}

uint32_t SkribidiAdapter::atlas_texture_count() const {
    return state_->atlas ? static_cast<uint32_t>(skb_image_atlas_get_texture_count(state_->atlas))
                         : 0;
}

} // namespace nkui
