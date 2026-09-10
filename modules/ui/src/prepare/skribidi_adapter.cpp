#include "skribidi_adapter.h"

#include "skribidi/skb_attributes.h"
#include "skribidi/skb_font_collection.h"
#include "skribidi/skb_image_atlas.h"
#include "skribidi/skb_layout.h"
#include "skribidi/skb_rasterizer.h"

#include <cstring>
#include <string>

namespace nkui {

struct SkribidiAdapter::State {
    skb_font_collection_t *fonts = nullptr;
    skb_temp_alloc_t *temporary = nullptr;
    skb_rasterizer_t *rasterizer = nullptr;
    skb_image_atlas_t *atlas = nullptr;
    skb_layout_t *layout = nullptr;
    uint16_t next_texture_slot = 1;
    uint16_t texture_namespace = 1;
    std::string cached_text;
    float cached_width = 0.0f;
    float cached_font_size = 0.0f;
    uint32_t font_generation = 0;
    uint32_t cached_font_generation = 0;
    uint32_t layout_builds = 0;
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

void atlas_texture_created(skb_image_atlas_t *atlas, uint8_t texture_index, void *context) {
    auto &state = *static_cast<SkribidiAdapter::State *>(context);
    const uint32_t id = (uint32_t(1) << 28) | (uint32_t(state.texture_namespace & 0x0FFF) << 16) |
                        state.next_texture_slot++;
    skb_image_atlas_set_texture_user_data(atlas, texture_index, id);
}

} // namespace

SkribidiAdapter::SkribidiAdapter() : state_(new State) {
    state_->fonts = skb_font_collection_create();
    state_->temporary = skb_temp_alloc_create(512 * 1024);
    state_->rasterizer = skb_rasterizer_create(nullptr);
    state_->atlas = skb_image_atlas_create(nullptr);
    if (state_->atlas)
        skb_image_atlas_set_create_texture_callback(state_->atlas, atlas_texture_created, state_);
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

bool SkribidiAdapter::set_atlas_namespace(uint16_t value) {
    if (!value || value > 0x0FFF || state_->next_texture_slot != 1)
        return false;
    state_->texture_namespace = value;
    return true;
}

bool SkribidiAdapter::add_font(const char *path, FontFamily family) {
    const uint8_t skb_family =
        family == FontFamily::Emoji ? SKB_FONT_FAMILY_EMOJI : SKB_FONT_FAMILY_DEFAULT;
    if (!path || !skb_font_collection_add_font(state_->fonts, path, skb_family, nullptr))
        return false;
    ++state_->font_generation;
    return true;
}

bool SkribidiAdapter::layout_utf8(const char *text, float width, float font_size) {
    if (!valid() || !text || width <= 0.0f || font_size <= 0.0f)
        return false;
    if (state_->layout && state_->cached_text == text && state_->cached_width == width &&
        state_->cached_font_size == font_size &&
        state_->cached_font_generation == state_->font_generation)
        return true;
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
    if (state_->layout) {
        state_->cached_text = text;
        state_->cached_width = width;
        state_->cached_font_size = font_size;
        state_->cached_font_generation = state_->font_generation;
        ++state_->layout_builds;
    }
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
                const AtlasTextureId atlas_id{static_cast<uint32_t>(
                    skb_image_atlas_get_texture_user_data(state_->atlas, quad.texture_idx))};
                if (!atlas_id.value)
                    return false;
                if (output.batches.empty() || output.batches.back().atlas.value != atlas_id.value ||
                    output.batches.back().mode != actual_mode) {
                    output.batches.push_back({atlas_id, actual_mode,
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

TextRect SkribidiAdapter::bounds() const {
    if (!state_->layout)
        return {};
    const skb_rect2_t value = skb_layout_get_bounds(state_->layout);
    return {value.x, value.y, value.width, value.height};
}

TextPosition SkribidiAdapter::hit_test(float x, float y) const {
    if (!state_->layout)
        return {};
    const skb_text_position_t value = skb_layout_hit_test(state_->layout, SKB_MOVEMENT_CARET, x, y);
    return {value.offset, static_cast<uint8_t>(value.affinity)};
}

TextCaret SkribidiAdapter::caret(TextPosition position) const {
    if (!state_->layout)
        return {};
    const skb_text_position_t value = {position.offset,
                                       static_cast<skb_caret_affinity_t>(position.affinity)};
    const skb_caret_info_t result = skb_layout_get_caret_info_at(state_->layout, value);
    return {result.x, result.y, result.ascender, result.descender, result.slope, result.direction};
}

int32_t SkribidiAdapter::next_grapheme(int32_t offset) const {
    return state_->layout ? skb_layout_get_next_grapheme_offset(state_->layout, offset) : 0;
}

int32_t SkribidiAdapter::previous_grapheme(int32_t offset) const {
    return state_->layout ? skb_layout_get_prev_grapheme_offset(state_->layout, offset) : 0;
}

int32_t SkribidiAdapter::align_grapheme(int32_t offset) const {
    return state_->layout ? skb_layout_align_grapheme_offset(state_->layout, offset) : 0;
}

std::vector<TextRect> SkribidiAdapter::selection_rects(TextPosition start, TextPosition end) const {
    std::vector<TextRect> rectangles;
    if (!state_->layout)
        return rectangles;
    const skb_text_range_t range = {
        {start.offset, static_cast<skb_caret_affinity_t>(start.affinity)},
        {end.offset, static_cast<skb_caret_affinity_t>(end.affinity)}};
    const auto collect = [](skb_rect2_t rect, void *context) {
        static_cast<std::vector<TextRect> *>(context)->push_back(
            {rect.x, rect.y, rect.width, rect.height});
    };
    skb_layout_iterate_text_range_bounds(state_->layout, range, collect, &rectangles);
    return rectangles;
}

uint32_t SkribidiAdapter::layout_build_count() const {
    return state_->layout_builds;
}

uint32_t SkribidiAdapter::atlas_texture_count() const {
    return state_->atlas ? static_cast<uint32_t>(skb_image_atlas_get_texture_count(state_->atlas))
                         : 0;
}

std::vector<AtlasUpload> SkribidiAdapter::pending_atlas_uploads() const {
    return atlas_uploads(false);
}

std::vector<AtlasUpload> SkribidiAdapter::atlas_uploads(bool include_clean) const {
    std::vector<AtlasUpload> uploads;
    if (!state_->atlas)
        return uploads;
    const int count = skb_image_atlas_get_texture_count(state_->atlas);
    for (int index = 0; index < count; ++index) {
        const skb_rect2i_t dirty = skb_image_atlas_get_texture_dirty_bounds(state_->atlas, index);
        const bool is_dirty = !skb_rect2i_is_empty(dirty);
        if (!is_dirty && !include_clean)
            continue;
        const skb_image_t *image = skb_image_atlas_get_texture(state_->atlas, index);
        const AtlasTextureId texture{
            static_cast<uint32_t>(skb_image_atlas_get_texture_user_data(state_->atlas, index))};
        if (!image || !texture.value)
            continue;
        uploads.push_back({texture, static_cast<uint8_t>(index), image->bpp, image->width,
                           image->height, image->stride_bytes, is_dirty ? dirty.x : 0,
                           is_dirty ? dirty.y : 0, is_dirty ? dirty.width : image->width,
                           is_dirty ? dirty.height : image->height, image->buffer, is_dirty});
    }
    return uploads;
}

bool SkribidiAdapter::acknowledge_atlas_upload(AtlasTextureId texture) {
    if (!state_->atlas || !texture.value)
        return false;
    const int count = skb_image_atlas_get_texture_count(state_->atlas);
    for (int index = 0; index < count; ++index) {
        if (skb_image_atlas_get_texture_user_data(state_->atlas, index) != texture.value)
            continue;
        const skb_rect2i_t dirty =
            skb_image_atlas_get_and_reset_texture_dirty_bounds(state_->atlas, index);
        return !skb_rect2i_is_empty(dirty);
    }
    return false;
}

} // namespace nkui
