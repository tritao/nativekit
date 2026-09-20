#include "prepare/text_engine.h"

#include <cstring>
#include <vector>

#ifndef NKUI_TEST_FONT_PATH
#error NKUI_TEST_FONT_PATH is required
#endif
#ifndef NKUI_TEST_COLOR_FONT_PATH
#error NKUI_TEST_COLOR_FONT_PATH is required
#endif

using namespace nkui;

int main() {
    auto shared_fonts = std::make_shared<FontCollection>();
    if (!shared_fonts->valid() || !shared_fonts->add_font(NKUI_TEST_FONT_PATH) ||
        shared_fonts->font_load_count() != 1)
        return 48;
    TextEngine shared_first(shared_fonts);
    TextEngine shared_second(shared_fonts);
    if (!shared_first.layout_utf8("first", 200.0f, 16.0f) ||
        !shared_second.layout_utf8("second", 200.0f, 16.0f) ||
        shared_fonts->font_load_count() != 1 ||
        shared_first.font_collection_generation() != shared_second.font_collection_generation())
        return 49;
    PreparedGlyphs shared_glyphs;
    if (!shared_first.prepare_glyphs(0.0f, 0.0f, 1.0f, GlyphMode::Alpha, shared_glyphs) ||
        shared_glyphs.vertices.empty())
        return 50;

    TextEngine engine;
    if (!engine.valid() || !engine.add_font(NKUI_TEST_FONT_PATH) ||
        !engine.add_font(NKUI_TEST_COLOR_FONT_PATH, FontFamily::Emoji) ||
        !engine.layout_utf8("áb NativeKit مرحبا", 500.0f, 28.0f))
        return 1;
    if (!engine.font_collection_generation() || !engine.layout_generation())
        return 2;
    if (engine.layout_build_count() != 1 ||
        !engine.layout_utf8("áb NativeKit مرحبا", 500.0f, 28.0f) ||
        engine.layout_build_count() != 1 || engine.bounds().width <= 0.0f)
        return 3;
    const int32_t next = engine.next_grapheme(0);
    if (next <= 1 || engine.previous_grapheme(next) != 0 || engine.align_grapheme(1) != 0)
        return 4;
    const TextPosition start{0, 1};
    const TextPosition end{next, 1};
    const auto caret = engine.caret(start);
    const auto hit = engine.hit_test(caret.x, caret.y);
    if (hit.offset < 0 || engine.selection_rects(start, end).empty())
        return 5;
    PreparedGlyphs glyphs;
    if (!engine.prepare_glyphs(10.0f, 40.0f, 1.0f, GlyphMode::Alpha, glyphs))
        return 6;
    if (glyphs.vertices.empty() || glyphs.indices.empty() || glyphs.batches.empty() ||
        engine.atlas_texture_count() == 0)
        return 7;
    const auto initial_stats = engine.stats();
    if (!initial_stats.glyph_cache_misses || !initial_stats.glyphs_rasterized ||
        initial_stats.prepared_batch_count < glyphs.batches.size())
        return 8;
    for (const auto &batch : glyphs.batches)
        if (!batch.atlas.value || batch.mode != GlyphMode::Alpha || !batch.vertex_count ||
            !batch.index_count)
            return 9;
    const auto first_query = engine.pending_atlas_uploads();
    const auto second_query = engine.pending_atlas_uploads();
    if (first_query.empty() || first_query.size() != second_query.size())
        return 10;
    for (const auto &batch : glyphs.batches) {
        bool matched_generation = false;
        for (const auto &upload : first_query)
            matched_generation =
                matched_generation || (upload.texture.value == batch.atlas.value &&
                                       upload.generation == batch.atlas_generation);
        if (!matched_generation)
            return 11;
    }
    if (first_query.front().format != AtlasTextureFormat::R8Mask ||
        engine.acknowledge_atlas_upload(first_query.front().texture,
                                         first_query.front().dirty_epoch + 1) ||
        engine.pending_atlas_uploads().empty())
        return 12;
    for (const auto &upload : first_query)
        if (!upload.texture.value || !upload.pixels ||
            (upload.bytes_per_pixel != 1 && upload.bytes_per_pixel != 4) || upload.width <= 0 ||
            upload.height <= 0 || upload.row_pitch <= 0 || !upload.generation ||
            !upload.dirty_epoch ||
            !engine.acknowledge_atlas_upload(upload.texture, upload.dirty_epoch))
            return 13;
    if (!engine.pending_atlas_uploads().empty())
        return 14;
    const auto clean_uploads = engine.atlas_uploads(true);
    if (clean_uploads.size() != first_query.size())
        return 14;
    for (const auto &upload : clean_uploads)
        if (upload.dirty || upload.x != 0 || upload.y != 0 ||
            upload.width != upload.texture_width || upload.height != upload.texture_height)
            return 15;
    const uint64_t stable_layout_generation = engine.layout_generation();
    PreparedGlyphs fractional_glyphs;
    if (!engine.prepare_glyphs(10.0f, 40.0f, 1.25f, GlyphMode::Alpha, fractional_glyphs) ||
        engine.layout_generation() != stable_layout_generation)
        return 15;
    for (const auto &upload : engine.pending_atlas_uploads())
        if (!engine.acknowledge_atlas_upload(upload.texture, upload.dirty_epoch))
            return 16;
    const auto fractional_stats = engine.stats();
    PreparedGlyphs repeated_fractional_glyphs;
    if (!engine.prepare_glyphs(10.0f, 40.0f, 1.25f, GlyphMode::Alpha,
                                repeated_fractional_glyphs) ||
        !engine.pending_atlas_uploads().empty())
        return 17;
    const auto repeated_stats = engine.stats();
    if (repeated_stats.glyph_cache_misses != fractional_stats.glyph_cache_misses ||
        repeated_stats.glyphs_rasterized != fractional_stats.glyphs_rasterized ||
        repeated_stats.prepared_batch_count <= fractional_stats.prepared_batch_count)
        return 18;
    PreparedGlyphs native_scale_glyphs;
    if (!engine.prepare_glyphs(10.0f, 40.0f, 1.0f, GlyphMode::Alpha, native_scale_glyphs) ||
        !engine.pending_atlas_uploads().empty())
        return 19;
    PreparedGlyphs sdf_glyphs;
    if (!engine.prepare_glyphs(10.0f, 40.0f, 1.0f, GlyphMode::Sdf, sdf_glyphs) ||
        sdf_glyphs.batches.empty())
        return 20;
    const auto sdf_uploads = engine.pending_atlas_uploads();
    bool saw_sdf = false;
    for (const auto &upload : sdf_uploads) {
        saw_sdf = saw_sdf || upload.format == AtlasTextureFormat::R8Sdf;
        if (upload.format != AtlasTextureFormat::R8Sdf || upload.bytes_per_pixel != 1 ||
            !engine.acknowledge_atlas_upload(upload.texture, upload.dirty_epoch))
            return 21;
    }
    if (!saw_sdf || !engine.pending_atlas_uploads().empty())
        return 22;
    if (!engine.prepared_glyphs_current(glyphs))
        return 23;
    PreparedGlyphs stale_glyphs;
    if (!engine.prepare_glyphs(10.0f, 40.0f, 1.125f, GlyphMode::Alpha, stale_glyphs))
        return 24;
    const auto stale_uploads = engine.pending_atlas_uploads();
    if (stale_uploads.empty())
        return 25;
    PreparedGlyphs updated_glyphs;
    if (!engine.prepare_glyphs(10.0f, 40.0f, 1.375f, GlyphMode::Alpha, updated_glyphs))
        return 26;
    bool replaced_first_epoch = false;
    for (const auto &upload : engine.pending_atlas_uploads()) {
        for (const auto &stale : stale_uploads) {
            if (upload.texture.value == stale.texture.value &&
                upload.dirty_epoch != stale.dirty_epoch) {
                replaced_first_epoch = true;
                if (engine.acknowledge_atlas_upload(stale.texture, stale.dirty_epoch))
                    return 27;
            }
        }
        if (!engine.acknowledge_atlas_upload(upload.texture, upload.dirty_epoch))
            return 28;
    }
    if (!replaced_first_epoch || !engine.pending_atlas_uploads().empty())
        return 29;
    if (engine.layout_build_count() != 1 ||
        engine.layout_generation() != stable_layout_generation)
        return 30;

    if (!engine.layout_utf8("😀", 80.0f, 32.0f))
        return 31;
    PreparedGlyphs color_glyphs;
    if (!engine.prepare_glyphs(0.0f, 0.0f, 1.0f, GlyphMode::Color, color_glyphs) ||
        color_glyphs.batches.empty())
        return 31;
    bool saw_color = false;
    for (const auto &batch : color_glyphs.batches)
        saw_color = saw_color || batch.mode == GlyphMode::Color;
    for (const auto &upload : engine.pending_atlas_uploads()) {
        if (upload.format != AtlasTextureFormat::Rgba8Premultiplied || upload.bytes_per_pixel != 4)
            return 32;
        if (!engine.acknowledge_atlas_upload(upload.texture, upload.dirty_epoch))
            return 33;
    }
    if (!saw_color || !engine.pending_atlas_uploads().empty())
        return 34;

    const char *unicode_samples[] = {
        "שלום עולם", "नमस्ते दुनिया", "你好世界", "👩‍🚀", "क्‍ष", "office ﬁ á",
    };
    int sample_index = 0;
    for (const char *sample : unicode_samples) {
        if (!engine.layout_utf8(sample, 90.0f, 24.0f) || engine.bounds().width <= 0.0f)
            return 35;
        const int32_t end = engine.next_grapheme(0);
        if (end <= 0 || (sample_index != 4 && engine.selection_rects({0, 0}, {end, 0}).empty()))
            return 36;
        ++sample_index;
    }
    const char *mixed_selection = "こんにちは — שלום — NativeKit مرحبا";
    if (!engine.layout_utf8(mixed_selection, 500.0f, 24.0f))
        return 51;
    const auto mixed_rects = engine.selection_rects({0, 0}, {32, 0});
    if (mixed_rects.empty())
        return 51;
    for (std::size_t left = 0; left < mixed_rects.size(); ++left) {
        for (std::size_t right = left + 1; right < mixed_rects.size(); ++right) {
            const auto &a = mixed_rects[left];
            const auto &b = mixed_rects[right];
            const float overlap_width = std::min(a.x + a.width, b.x + b.width) - std::max(a.x, b.x);
            const float overlap_height =
                std::min(a.y + a.height, b.y + b.height) - std::max(a.y, b.y);
            if (overlap_width > 0.01f && overlap_height > 0.01f)
                return 52;
        }
    }
    TextLayoutOptions options;
    options.font_size = 24.0f;
    options.letter_spacing = 1.0f;
    options.line_height = 40.0f;
    options.wrap = TextWrapMode::None;
    if (!engine.layout_utf8("NativeKit text options", 500.0f, options) ||
        engine.bounds().height < 39.0f)
        return 37;
    const uint32_t options_builds = engine.layout_build_count();
    if (!engine.layout_utf8("NativeKit text options", 500.0f, options) ||
        engine.layout_build_count() != options_builds)
        return 38;
    TextLayoutOptions wrapped_options;
    wrapped_options.font_size = 24.0f;
    wrapped_options.wrap = TextWrapMode::WordCharacter;
    TextLayoutResult wrapped;
    if (!engine.layout_utf8("Skribidi owns paragraph wrapping in NativeKit", 90.0f,
                             wrapped_options, &wrapped) ||
        !wrapped.id || wrapped.lines.size() < 2)
        return 39;
    PreparedGlyphs first_line;
    PreparedGlyphs second_line;
    if (!engine.prepare_glyphs_for_line(0, 0.0f, 0.0f, 1.0f, GlyphMode::Alpha, first_line) ||
        !engine.prepare_glyphs_for_line(1, 0.0f, 0.0f, 1.0f, GlyphMode::Alpha, second_line) ||
        first_line.vertices.empty() || second_line.vertices.empty())
        return 40;

    TextLayoutOptions direction_options;
    direction_options.font_size = 24.0f;
    direction_options.wrap = TextWrapMode::None;
    TextLayoutResult automatic_direction;
    TextLayoutResult right_to_left_direction;
    TextLayoutResult automatic_direction_again;
    const uint32_t direction_builds = engine.layout_build_count();
    if (!engine.layout_utf8("NativeKit", 300.0f, direction_options, &automatic_direction))
        return 44;
    direction_options.direction = TextDirection::Rtl;
    if (!engine.layout_utf8("NativeKit", 300.0f, direction_options, &right_to_left_direction) ||
        automatic_direction.id == right_to_left_direction.id ||
        automatic_direction.lines.size() != 1 || right_to_left_direction.lines.size() != 1 ||
        right_to_left_direction.lines.front().bounds.x <=
            automatic_direction.lines.front().bounds.x + 1.0f ||
        engine.layout_build_count() != direction_builds + 2)
        return 45;
    direction_options.direction = TextDirection::Auto;
    if (!engine.layout_utf8("NativeKit", 300.0f, direction_options, &automatic_direction_again) ||
        automatic_direction_again.id != automatic_direction.id ||
        engine.layout_build_count() != direction_builds + 2)
        return 46;

    TextLayoutResult retained_first;
    TextLayoutResult retained_second;
    TextLayoutOptions retained_options;
    retained_options.font_size = 22.0f;
    retained_options.wrap = TextWrapMode::WordCharacter;
    if (!engine.layout_utf8("first retained paragraph", 120.0f, retained_options,
                             &retained_first) ||
        !engine.layout_utf8("second retained paragraph", 120.0f, retained_options,
                             &retained_second) ||
        !retained_first.id || !retained_second.id || retained_first.id == retained_second.id ||
        !engine.has_layout(retained_first.id) || !engine.has_layout(retained_second.id))
        return 42;
    const uint32_t retained_builds = engine.layout_build_count();
    PreparedGlyphs retained_first_glyphs;
    PreparedGlyphs retained_second_glyphs;
    if (!engine.prepare_glyphs_for_line(retained_first.id, 0, 0.0f, 0.0f, 1.0f, GlyphMode::Alpha,
                                         retained_first_glyphs) ||
        !engine.prepare_glyphs_for_line(retained_second.id, 0, 0.0f, 0.0f, 1.0f, GlyphMode::Alpha,
                                         retained_second_glyphs) ||
        retained_first_glyphs.layout_id != retained_first.id ||
        retained_second_glyphs.layout_id != retained_second.id ||
        engine.layout_build_count() != retained_builds)
        return 43;
    const std::vector<TextLayoutId> kept_layouts{retained_second.id};
    engine.prune_layout_cache(kept_layouts, 1);
    if (engine.has_layout(retained_first.id) || !engine.has_layout(retained_second.id))
        return 47;

    /*
     * Published snapshots are shared, immutable, and usable after later
     * preparation, which is what lets an owned resource set keep them without
     * copying glyph buffers.
     */
    TextLayoutResult published_layout{};
    TextLayoutOptions published_options;
    published_options.font_size = 16.0f;
    if (!engine.layout_utf8("published glyphs", 200.0f, published_options, &published_layout) ||
        !published_layout.id)
        return 51;
    const auto snapshot =
        engine.published_glyphs(published_layout.id, 0.0f, 0.0f, 1.0f, GlyphMode::Alpha);
    if (!snapshot || snapshot->vertices.empty() || snapshot->indices.empty() ||
        snapshot->layout_id != published_layout.id || snapshot->mode != GlyphMode::Alpha ||
        snapshot->pixel_scale != 1.0f || !engine.prepared_glyphs_current(*snapshot))
        return 52;
    if (engine.published_glyphs(published_layout.id, 0.0f, 0.0f, 1.0f, GlyphMode::Alpha) !=
        snapshot)
        return 53;
    const auto vertices_before = snapshot->vertices;
    const auto indices_before = snapshot->indices;
    PreparedGlyphs mutable_glyphs;
    if (!engine.prepare_glyphs(0.0f, 0.0f, 2.0f, GlyphMode::Alpha, mutable_glyphs))
        return 54;
    if (snapshot->vertices.size() != vertices_before.size() ||
        std::memcmp(snapshot->vertices.data(), vertices_before.data(),
                    vertices_before.size() * sizeof(GlyphVertex)) != 0 ||
        snapshot->indices != indices_before || snapshot->pixel_scale != 1.0f)
        return 55;
    if (engine.published_glyphs(published_layout.id, 0.0f, 0.0f, 2.0f, GlyphMode::Alpha) ==
        snapshot)
        return 56;
    const auto line_snapshot = engine.published_glyphs_for_line(published_layout.id, 0, 0.0f, 0.0f,
                                                                 1.0f, GlyphMode::Alpha);
    if (!line_snapshot || line_snapshot->layout_id != published_layout.id ||
        line_snapshot == snapshot ||
        engine.published_glyphs_for_line(published_layout.id, 0, 0.0f, 0.0f, 1.0f,
                                          GlyphMode::Alpha) != line_snapshot)
        return 57;
    if (engine.published_glyphs(0, 0.0f, 0.0f, 1.0f, GlyphMode::Alpha) ||
        engine.published_glyphs(published_layout.id, 0.0f, 0.0f, 0.0f, GlyphMode::Alpha) ||
        engine.published_glyphs_for_line(published_layout.id, 99, 0.0f, 0.0f, 1.0f,
                                          GlyphMode::Alpha))
        return 58;

    return glyphs.vertices.size() % 4 == 0 && glyphs.indices.size() % 6 == 0 ? 0 : 41;
}
