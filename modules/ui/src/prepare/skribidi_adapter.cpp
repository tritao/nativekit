#include "skribidi_adapter.h"

#include "system_fonts.h"

#include "skribidi/skb_attributes.h"
#include "skribidi/skb_font_collection.h"
#include "skribidi/skb_image_atlas.h"
#include "skribidi/skb_layout.h"
#include "skribidi/skb_rasterizer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace nkui {

struct SkribidiAdapter::State {
    struct RetainedLayout {
        ~RetainedLayout() {
            if (layout)
                skb_layout_destroy(layout);
        }

        skb_layout_t *layout = nullptr;
        std::string text;
        float width = 0.0f;
        TextLayoutOptions options{};
        uint64_t font_generation = 0;
        uint64_t last_used = 0;
        TextLayoutResult result{};
        std::vector<skb_range_t> line_ranges;
    };

    skb_font_collection_t *fonts = nullptr;
    skb_temp_alloc_t *temporary = nullptr;
    skb_rasterizer_t *rasterizer = nullptr;
    skb_image_atlas_t *atlas = nullptr;
    uint16_t next_texture_slot = 1;
    uint16_t texture_namespace = 1;
    TextLayoutId next_layout_id = 1;
    TextLayoutId active_layout_id = 0;
    uint64_t layout_use_sequence = 0;
    std::unordered_map<TextLayoutId, std::unique_ptr<RetainedLayout>> layouts;
    uint32_t layout_builds = 0;
    uint64_t prepared_batch_count = 0;
    std::unordered_set<std::string> system_fonts_loaded;
    std::vector<std::shared_ptr<std::vector<uint8_t>>> font_data;
};

namespace {

SkribidiAdapter::State::RetainedLayout *find_layout(SkribidiAdapter::State &state,
                                                     TextLayoutId id) {
    const auto found = state.layouts.find(id);
    return found == state.layouts.end() ? nullptr : found->second.get();
}

const SkribidiAdapter::State::RetainedLayout *find_layout(const SkribidiAdapter::State &state,
                                                          TextLayoutId id) {
    const auto found = state.layouts.find(id);
    return found == state.layouts.end() ? nullptr : found->second.get();
}

SkribidiAdapter::State::RetainedLayout *active_layout(SkribidiAdapter::State &state) {
    return find_layout(state, state.active_layout_id);
}

const SkribidiAdapter::State::RetainedLayout *active_layout(
    const SkribidiAdapter::State &state) {
    return find_layout(state, state.active_layout_id);
}

} // namespace

namespace {

bool system_font_fallback(skb_font_collection_t *font_collection, const char *, uint8_t script,
                          uint8_t font_family, void *context) {
    auto *state = static_cast<SkribidiAdapter::State *>(context);
    const uint32_t script_tag = skb_script_to_iso15924_tag(script);
    const bool emoji = font_family == SKB_FONT_FAMILY_EMOJI;
    if (std::getenv("NKUI_DEBUG_GLYPHS"))
        std::fprintf(stderr, "fallback request %c%c%c%c family=%u\n",
                     static_cast<char>(script_tag >> 24), static_cast<char>(script_tag >> 16),
                     static_cast<char>(script_tag >> 8), static_cast<char>(script_tag),
                     static_cast<unsigned>(font_family));
    bool added = false;
    const auto &fallbacks = system_font_fallbacks();
    for (int pass = 0; pass < 2 && !added; ++pass) {
        for (const auto &font : fallbacks) {
            if (emoji && ((pass == 0) != font.color))
                continue;
            if (font.emoji != emoji ||
                (!emoji && font.script_tag != 0 && font.script_tag != script_tag))
                continue;
            const std::string key = std::to_string(static_cast<unsigned>(font_family)) + ":" +
                                    font.path;
            if (!state->system_fonts_loaded.insert(key).second)
                continue;
            if (skb_font_collection_add_font(font_collection, font.path.c_str(), font_family,
                                              nullptr)) {
                added = true;
                if (std::getenv("NKUI_DEBUG_GLYPHS"))
                    std::fprintf(stderr, "fallback font %s color=%u\n", font.path.c_str(),
                                 font.color ? 1u : 0u);
                // Family-only candidates, used by the Windows registry adapter,
                // need to be loaded as a group before font selection is retried.
                if (!emoji && font.script_tag == 0)
                    continue;
                return true;
            }
        }
    }
    return added;
}

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

std::vector<std::size_t> utf8_codepoint_offsets(const char *text) {
    std::vector<std::size_t> offsets;
    if (!text)
        return offsets;
    const auto continuation = [](unsigned char value) {
        return (value & 0xC0u) == 0x80u;
    };
    const std::size_t length = std::strlen(text);
    offsets.reserve(length + 1);
    std::size_t index = 0;
    while (index < length) {
        offsets.push_back(index);
        const unsigned char first = static_cast<unsigned char>(text[index]);
        std::size_t sequence_length = 1;
        if (first >= 0xC2u && first <= 0xDFu)
            sequence_length = 2;
        else if (first >= 0xE0u && first <= 0xEFu)
            sequence_length = 3;
        else if (first >= 0xF0u && first <= 0xF4u)
            sequence_length = 4;
        if (sequence_length > 1 &&
            (index + sequence_length > length ||
             !std::all_of(text + index + 1, text + index + sequence_length,
                          [&](char value) {
                              return continuation(static_cast<unsigned char>(value));
                          })))
            sequence_length = 1;
        index += sequence_length;
    }
    offsets.push_back(length);
    return offsets;
}

AtlasTextureFormat atlas_format(skb_image_atlas_texture_format_t format) {
    switch (format) {
    case SKB_IMAGE_ATLAS_FORMAT_R8_SDF:
        return AtlasTextureFormat::R8Sdf;
    case SKB_IMAGE_ATLAS_FORMAT_RGBA8_PREMULTIPLIED:
        return AtlasTextureFormat::Rgba8Premultiplied;
    case SKB_IMAGE_ATLAS_FORMAT_R8_MASK:
    default:
        return AtlasTextureFormat::R8Mask;
    }
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

struct RenderGlyphContext {
    SkribidiAdapter::State *state = nullptr;
    skb_layout_t *layout = nullptr;
    float origin_x = 0.0f;
    float origin_y = 0.0f;
    float pixel_scale = 1.0f;
    GlyphMode requested_mode = GlyphMode::Alpha;
    PreparedGlyphs *output = nullptr;
    int32_t line_start = -1;
    int32_t line_end = -1;
};

bool append_render_glyph(const skb_layout_render_glyph_t *glyph, void *context) {
    auto &render = *static_cast<RenderGlyphContext *>(context);
    if (render.line_start >= 0 &&
        (glyph->text_range.end <= render.line_start || glyph->text_range.start >= render.line_end))
        return true;
    if (std::getenv("NKUI_DEBUG_GLYPHS")) {
        const uint32_t script = skb_script_to_iso15924_tag(glyph->script);
        const uint32_t *text = skb_layout_get_text(render.layout);
        std::fprintf(stderr, "glyph %c%c%c%c size=%.1f font=%u gid=%u range=%d..%d cp=%x offset=%.1f,%.1f\n",
                     static_cast<char>(script >> 24), static_cast<char>(script >> 16),
                     static_cast<char>(script >> 8), static_cast<char>(script),
                     glyph->font_size, static_cast<unsigned>(glyph->font_handle),
                     static_cast<unsigned>(glyph->glyph_id), glyph->text_range.start,
                     glyph->text_range.end,
                     text ? text[glyph->text_range.start] : 0u, glyph->offset_x, glyph->offset_y);
    }
    const skb_quad_t quad = skb_image_atlas_get_glyph_quad(
        render.state->atlas, render.origin_x + glyph->offset_x, render.origin_y + glyph->offset_y,
        render.pixel_scale, render.state->fonts, glyph->font_handle, glyph->glyph_id,
        glyph->font_size, glyph->color, raster_mode(render.requested_mode));
    if (quad.flags & SKB_QUAD_IS_EMPTY)
        return true;

    const GlyphMode actual_mode = quad_mode(quad, render.requested_mode);
    const AtlasTextureId atlas_id{static_cast<uint32_t>(
        skb_image_atlas_get_texture_user_data(render.state->atlas, quad.texture_idx))};
    if (!atlas_id.value)
        return false;
    if (render.output->batches.empty() || render.output->batches.back().atlas.value != atlas_id.value ||
        render.output->batches.back().atlas_generation != quad.texture_generation ||
        render.output->batches.back().mode != actual_mode) {
        render.output->batches.push_back(
            {atlas_id, quad.texture_generation, actual_mode,
             static_cast<uint32_t>(render.output->vertices.size()), 0,
             static_cast<uint32_t>(render.output->indices.size()), 0});
    }
    const skb_image_t *atlas =
        skb_image_atlas_get_texture(render.state->atlas, quad.texture_idx);
    if (!atlas)
        return false;
    append_quad(quad, *atlas, *render.output);
    auto &batch = render.output->batches.back();
    batch.vertex_count += 4;
    batch.index_count += 6;
    return true;
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
    state_->layouts.clear();
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
    return true;
}

bool SkribidiAdapter::add_font_from_data(const char *name, const void *data, std::size_t bytes,
                                         FontFamily family) {
    if (!name || !*name || !data || !bytes || !valid())
        return false;
    try {
        auto owned = std::make_shared<std::vector<uint8_t>>(
            static_cast<const uint8_t *>(data), static_cast<const uint8_t *>(data) + bytes);
        return add_font_from_shared_data(name, owned, family);
    } catch (...) {
        return false;
    }
}

bool SkribidiAdapter::add_font_from_shared_data(
    const char *name, const std::shared_ptr<std::vector<uint8_t>> &data, FontFamily family) {
    const uint8_t skb_family =
        family == FontFamily::Emoji ? SKB_FONT_FAMILY_EMOJI : SKB_FONT_FAMILY_DEFAULT;
    if (!name || !*name || !data || data->empty() || !valid())
        return false;
    if (!skb_font_collection_add_font_from_data(state_->fonts, name, data->data(), data->size(),
                                                nullptr, nullptr, skb_family, nullptr))
        return false;
    state_->font_data.push_back(data);
    return true;
}

bool SkribidiAdapter::add_system_fallbacks() {
    if (!valid())
        return false;
    skb_font_collection_set_on_font_fallback(state_->fonts, system_font_fallback, state_);
    return true;
}

bool SkribidiAdapter::measure_intrinsic_utf8(const char *text, const TextLayoutOptions &options,
                                             TextRect *result) {
    if (!valid() || !text || !std::isfinite(options.font_size) || options.font_size <= 0.0f ||
        !std::isfinite(options.letter_spacing) || !std::isfinite(options.line_height) ||
        options.line_height < 0.0f)
        return false;

    const skb_attribute_t attributes[] = {
        skb_attribute_make_font_size(options.font_size),
        skb_attribute_make_font_family(static_cast<uint8_t>(options.family)),
        skb_attribute_make_letter_spacing(options.letter_spacing),
        skb_attribute_make_line_height(options.line_height > 0.0f ? SKB_LINE_HEIGHT_ABSOLUTE
                                                                  : SKB_LINE_HEIGHT_NORMAL,
                                       options.line_height),
        skb_attribute_make_paint_color(SKB_PAINT_TEXT, SKB_PAINT_STATE_DEFAULT,
                                       skb_rgba(255, 255, 255, 255))};
    const skb_attribute_t layout_attributes[] = {skb_attribute_make_text_wrap(SKB_WRAP_NONE),
                                                  skb_attribute_make_horizontal_align(SKB_ALIGN_START)};
    const skb_layout_params_t params = {
        .font_collection = state_->fonts,
        .layout_width = 1000000.0f,
        .layout_attributes = SKB_ATTRIBUTE_SET_FROM_STATIC_ARRAY(layout_attributes)};
    skb_layout_t *layout = skb_layout_create(&params);
    if (!layout)
        return false;
    skb_layout_set_utf8(layout, state_->temporary, &params, text, -1,
                        SKB_ATTRIBUTE_SET_FROM_STATIC_ARRAY(attributes));
    const skb_rect2_t bounds = skb_layout_get_bounds(layout);
    skb_layout_destroy(layout);
    if (result)
        *result = {bounds.x, bounds.y, bounds.width, bounds.height};
    return true;
}

bool SkribidiAdapter::layout_utf8(const char *text, float width, float font_size) {
    TextLayoutOptions options;
    options.font_size = font_size;
    return layout_utf8(text, width, options);
}

bool SkribidiAdapter::layout_utf8(const char *text, float width,
                                  const TextLayoutOptions &options) {
    return layout_utf8(text, width, options, nullptr);
}

bool SkribidiAdapter::layout_utf8(const char *text, float width,
                                  const TextLayoutOptions &options, TextLayoutResult *result) {
    if (!valid() || !text || !std::isfinite(width) || width <= 0.0f ||
        !std::isfinite(options.font_size) || options.font_size <= 0.0f ||
        !std::isfinite(options.letter_spacing) ||
        !std::isfinite(options.line_height) || options.line_height < 0.0f)
        return false;
    const uint64_t font_generation = skb_font_collection_get_generation(state_->fonts);
    for (auto &entry : state_->layouts) {
        auto &cached = *entry.second;
        if (cached.font_generation == font_generation && cached.text == text &&
            cached.width == width && cached.options.font_size == options.font_size &&
            cached.options.letter_spacing == options.letter_spacing &&
            cached.options.line_height == options.line_height &&
            cached.options.family == options.family && cached.options.wrap == options.wrap &&
            cached.options.alignment == options.alignment) {
            cached.last_used = ++state_->layout_use_sequence;
            state_->active_layout_id = entry.first;
            if (result)
                *result = cached.result;
            return true;
        }
    }
    const skb_text_wrap_t wrap = options.wrap == TextWrapMode::None
                                     ? SKB_WRAP_NONE
                                 : options.wrap == TextWrapMode::Word
                                     ? SKB_WRAP_WORD
                                     : SKB_WRAP_WORD_CHAR;
    const skb_align_t align = options.alignment == TextAlignment::Center
                                  ? SKB_ALIGN_CENTER
                              : options.alignment == TextAlignment::End ? SKB_ALIGN_END
                                                                        : SKB_ALIGN_START;
    const skb_line_height_t line_height_type = options.line_height > 0.0f
                                                   ? SKB_LINE_HEIGHT_ABSOLUTE
                                                   : SKB_LINE_HEIGHT_NORMAL;
    const skb_attribute_t attributes[] = {
        skb_attribute_make_font_size(options.font_size),
        skb_attribute_make_font_family(static_cast<uint8_t>(options.family)),
        skb_attribute_make_letter_spacing(options.letter_spacing),
        skb_attribute_make_line_height(line_height_type, options.line_height),
        skb_attribute_make_paint_color(SKB_PAINT_TEXT, SKB_PAINT_STATE_DEFAULT,
                                       skb_rgba(255, 255, 255, 255))};
    const skb_attribute_t layout_attributes[] = {skb_attribute_make_text_wrap(wrap),
                                                  skb_attribute_make_horizontal_align(align)};
    const skb_layout_params_t params = {
        .font_collection = state_->fonts,
        .layout_width = width,
        .layout_attributes = SKB_ATTRIBUTE_SET_FROM_STATIC_ARRAY(layout_attributes)};
    auto retained = std::make_unique<State::RetainedLayout>();
    retained->layout = skb_layout_create(&params);
    if (!retained->layout)
        return false;
    skb_layout_set_utf8(retained->layout, state_->temporary, &params, text, -1,
                        SKB_ATTRIBUTE_SET_FROM_STATIC_ARRAY(attributes));
    retained->text = text;
    retained->width = width;
    retained->options = options;
    retained->font_generation = font_generation;
    retained->last_used = ++state_->layout_use_sequence;
    TextLayoutResult layout_result;
    layout_result.id = state_->next_layout_id++;
    if (!layout_result.id)
        layout_result.id = state_->next_layout_id++;
    const TextLayoutId layout_id = layout_result.id;
    const skb_rect2_t layout_bounds = skb_layout_get_bounds(retained->layout);
    layout_result.bounds = {layout_bounds.x, layout_bounds.y, layout_bounds.width,
                            layout_bounds.height};
    const auto offsets = utf8_codepoint_offsets(text);
    const int32_t lines_count = skb_layout_get_lines_count(retained->layout);
    const skb_layout_line_t *lines = skb_layout_get_lines(retained->layout);
    if (lines_count > 0 && !lines)
        return false;
    layout_result.lines.reserve(static_cast<std::size_t>(std::max(lines_count, 0)));
    retained->line_ranges.reserve(static_cast<std::size_t>(std::max(lines_count, 0)));
    const int32_t text_count = skb_layout_get_text_count(retained->layout);
    for (int32_t index = 0; index < lines_count; ++index) {
        const skb_layout_line_t &line = lines[index];
        if (line.text_range.start < 0 || line.text_range.end < line.text_range.start ||
            line.text_range.end > text_count ||
            static_cast<std::size_t>(line.text_range.end) >= offsets.size())
            return false;
        const std::size_t start = offsets[static_cast<std::size_t>(line.text_range.start)];
        const std::size_t end = offsets[static_cast<std::size_t>(line.text_range.end)];
        retained->line_ranges.push_back(line.text_range);
        layout_result.lines.push_back(
            {start, end - start,
             {line.bounds.x, line.bounds.y, line.bounds.width, line.bounds.height}});
    }
    retained->result = std::move(layout_result);
    state_->active_layout_id = layout_id;
    if (result)
        *result = retained->result;
    state_->layouts.emplace(layout_id, std::move(retained));
    ++state_->layout_builds;
    return true;
}

void SkribidiAdapter::prune_layout_cache(const std::vector<TextLayoutId> &retained_ids,
                                         std::size_t max_entries) {
    const uint64_t font_generation = skb_font_collection_get_generation(state_->fonts);
    const std::unordered_set<TextLayoutId> retained(retained_ids.begin(), retained_ids.end());

    for (auto entry = state_->layouts.begin(); entry != state_->layouts.end();) {
        if (entry->second->font_generation != font_generation &&
            retained.find(entry->first) == retained.end())
            entry = state_->layouts.erase(entry);
        else
            ++entry;
    }

    while (state_->layouts.size() > max_entries) {
        auto oldest = state_->layouts.end();
        for (auto entry = state_->layouts.begin(); entry != state_->layouts.end(); ++entry) {
            if (retained.find(entry->first) != retained.end())
                continue;
            if (oldest == state_->layouts.end() ||
                entry->second->last_used < oldest->second->last_used)
                oldest = entry;
        }
        if (oldest == state_->layouts.end())
            break;
        state_->layouts.erase(oldest);
    }
}

bool SkribidiAdapter::has_layout(TextLayoutId id) const {
    return id != 0 && find_layout(*state_, id) != nullptr;
}

bool SkribidiAdapter::prepare_glyphs(float origin_x, float origin_y, float pixel_scale,
                                     GlyphMode mode, PreparedGlyphs &output) {
    return prepare_glyphs_internal(state_->active_layout_id, origin_x, origin_y, pixel_scale, mode,
                                   output, -1, -1, 0.0f, 0.0f);
}

bool SkribidiAdapter::prepare_glyphs_for_line(uint32_t line_index, float origin_x, float origin_y,
                                              float pixel_scale, GlyphMode mode,
                                              PreparedGlyphs &output) {
    return prepare_glyphs_for_line(state_->active_layout_id, line_index, origin_x, origin_y,
                                   pixel_scale, mode, output);
}

bool SkribidiAdapter::prepare_glyphs_for_line(TextLayoutId id, uint32_t line_index, float origin_x,
                                              float origin_y, float pixel_scale, GlyphMode mode,
                                              PreparedGlyphs &output) {
    const auto *layout = find_layout(*state_, id);
    if (!layout || line_index >= layout->result.lines.size() ||
        line_index >= layout->line_ranges.size())
        return false;
    const auto &line = layout->result.lines[line_index];
    const auto range = layout->line_ranges[line_index];
    return prepare_glyphs_internal(id, origin_x, origin_y, pixel_scale, mode, output, range.start,
                                   range.end, line.bounds.x, line.bounds.y);
}

bool SkribidiAdapter::prepare_glyphs_internal(TextLayoutId id, float origin_x, float origin_y,
                                              float pixel_scale, GlyphMode mode,
                                              PreparedGlyphs &output,
                                              int32_t line_start, int32_t line_end, float line_x,
                                              float line_y) {
    const auto *retained = find_layout(*state_, id);
    if (!retained || pixel_scale <= 0.0f)
        return false;
    output = {};
    output.origin_x = origin_x;
    output.origin_y = origin_y;
    output.pixel_scale = pixel_scale;
    output.mode = mode;
    output.layout_id = id;
    output.layout_generation = skb_layout_get_generation(retained->layout);
    if (!skb_layout_prepare_glyphs(retained->layout, state_->atlas, state_->temporary,
                                   state_->rasterizer, pixel_scale, raster_mode(mode)))
        return false;
    if (std::getenv("NKUI_DEBUG_GLYPHS") && retained->options.font_size == 18.0f) {
        const uint32_t *text = skb_layout_get_text(retained->layout);
        const skb_text_property_t *properties = skb_layout_get_text_properties(retained->layout);
        const int32_t count = skb_layout_get_text_count(retained->layout);
        std::fprintf(stderr, "text properties:");
        for (int32_t i = 0; i < count; ++i) {
            const uint32_t script = skb_script_to_iso15924_tag(properties[i].script);
            std::fprintf(stderr, " %x/%c%c%c%c", text[i], static_cast<char>(script >> 24),
                         static_cast<char>(script >> 16), static_cast<char>(script >> 8),
                         static_cast<char>(script));
        }
        std::fprintf(stderr, "\n");
    }
    RenderGlyphContext render{state_, retained->layout, origin_x - line_x, origin_y - line_y,
                              pixel_scale, mode, &output, line_start, line_end};
    if (!skb_layout_iterate_render_glyphs(retained->layout, append_render_glyph, &render))
        return false;
    state_->prepared_batch_count += output.batches.size();
    return true;
}

bool SkribidiAdapter::prepared_glyphs_current(const PreparedGlyphs &glyphs) const {
    const auto *layout = find_layout(*state_, glyphs.layout_id);
    if (!state_->atlas || !layout ||
        glyphs.layout_generation != skb_layout_get_generation(layout->layout))
        return false;
    const int count = skb_image_atlas_get_texture_count(state_->atlas);
    for (const auto &batch : glyphs.batches) {
        bool found = false;
        for (int index = 0; index < count; ++index) {
            if (skb_image_atlas_get_texture_user_data(state_->atlas, index) != batch.atlas.value)
                continue;
            found = true;
            if (skb_image_atlas_get_texture_generation(state_->atlas, index) !=
                batch.atlas_generation)
                return false;
            break;
        }
        if (!found)
            return false;
    }
    return true;
}

TextRect SkribidiAdapter::bounds() const {
    const auto *layout = active_layout(*state_);
    if (!layout)
        return {};
    const skb_rect2_t value = skb_layout_get_bounds(layout->layout);
    return {value.x, value.y, value.width, value.height};
}

TextPosition SkribidiAdapter::hit_test(float x, float y) const {
    const auto *layout = active_layout(*state_);
    if (!layout)
        return {};
    const skb_text_position_t value =
        skb_layout_hit_test(layout->layout, SKB_MOVEMENT_CARET, x, y);
    return {value.offset, static_cast<uint8_t>(value.affinity)};
}

TextCaret SkribidiAdapter::caret(TextPosition position) const {
    const auto *layout = active_layout(*state_);
    if (!layout)
        return {};
    const skb_text_position_t value = {position.offset,
                                       static_cast<skb_caret_affinity_t>(position.affinity)};
    const skb_caret_info_t result = skb_layout_get_caret_info_at(layout->layout, value);
    return {result.x, result.y, result.ascender, result.descender, result.slope, result.direction};
}

int32_t SkribidiAdapter::next_grapheme(int32_t offset) const {
    const auto *layout = active_layout(*state_);
    return layout ? skb_layout_get_next_grapheme_offset(layout->layout, offset) : 0;
}

int32_t SkribidiAdapter::previous_grapheme(int32_t offset) const {
    const auto *layout = active_layout(*state_);
    return layout ? skb_layout_get_prev_grapheme_offset(layout->layout, offset) : 0;
}

int32_t SkribidiAdapter::align_grapheme(int32_t offset) const {
    const auto *layout = active_layout(*state_);
    return layout ? skb_layout_align_grapheme_offset(layout->layout, offset) : 0;
}

std::vector<TextRect> SkribidiAdapter::selection_rects(TextPosition start, TextPosition end) const {
    std::vector<TextRect> rectangles;
    const auto *layout = active_layout(*state_);
    if (!layout)
        return rectangles;
    const skb_text_range_t range = {
        {start.offset, static_cast<skb_caret_affinity_t>(start.affinity)},
        {end.offset, static_cast<skb_caret_affinity_t>(end.affinity)}};
    const auto collect = [](skb_rect2_t rect, void *context) {
        static_cast<std::vector<TextRect> *>(context)->push_back(
            {rect.x, rect.y, rect.width, rect.height});
    };
    skb_layout_iterate_text_range_bounds(layout->layout, range, collect, &rectangles);
    return rectangles;
}

uint64_t SkribidiAdapter::font_collection_generation() const {
    return state_->fonts ? skb_font_collection_get_generation(state_->fonts) : 0;
}

uint64_t SkribidiAdapter::layout_generation() const {
    const auto *layout = active_layout(*state_);
    return layout ? skb_layout_get_generation(layout->layout) : 0;
}

uint32_t SkribidiAdapter::layout_build_count() const {
    return state_->layout_builds;
}

uint32_t SkribidiAdapter::atlas_texture_count() const {
    return state_->atlas ? static_cast<uint32_t>(skb_image_atlas_get_texture_count(state_->atlas))
                         : 0;
}

SkribidiAdapterStats SkribidiAdapter::stats() const {
    if (!state_->atlas)
        return {};
    const skb_image_atlas_stats_t atlas_stats = skb_image_atlas_get_stats(state_->atlas);
    return {atlas_stats.glyph_cache_misses, atlas_stats.glyphs_rasterized,
            state_->prepared_batch_count};
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
        const auto snapshot = skb_image_atlas_peek_texture_dirty(state_->atlas, index);
        const skb_rect2i_t dirty = snapshot.dirty;
        const bool is_dirty = !skb_rect2i_is_empty(dirty);
        if (!is_dirty && !include_clean)
            continue;
        const AtlasTextureId texture{
            static_cast<uint32_t>(skb_image_atlas_get_texture_user_data(state_->atlas, index))};
        if (!snapshot.pixels || !texture.value)
            continue;
        const uint8_t bytes_per_pixel = snapshot.format == SKB_IMAGE_ATLAS_FORMAT_RGBA8_PREMULTIPLIED
                                             ? 4
                                             : 1;
        uploads.push_back({texture, static_cast<uint8_t>(index), atlas_format(snapshot.format),
                           bytes_per_pixel, snapshot.width, snapshot.height, snapshot.stride_bytes,
                           is_dirty ? dirty.x : 0, is_dirty ? dirty.y : 0,
                           is_dirty ? dirty.width : snapshot.width,
                           is_dirty ? dirty.height : snapshot.height, snapshot.pixels, is_dirty,
                           snapshot.texture_generation, snapshot.epoch});
    }
    return uploads;
}

bool SkribidiAdapter::acknowledge_atlas_upload(AtlasTextureId texture, uint64_t dirty_epoch) {
    if (!state_->atlas || !texture.value || !dirty_epoch)
        return false;
    const int count = skb_image_atlas_get_texture_count(state_->atlas);
    for (int index = 0; index < count; ++index) {
        if (skb_image_atlas_get_texture_user_data(state_->atlas, index) != texture.value)
            continue;
        return skb_image_atlas_ack_texture_dirty(state_->atlas, index, dirty_epoch);
    }
    return false;
}

} // namespace nkui
