#include "prepare/skribidi_adapter.h"

#ifndef NKUI_TEST_FONT_PATH
#error NKUI_TEST_FONT_PATH is required
#endif
#ifndef NKUI_TEST_COLOR_FONT_PATH
#error NKUI_TEST_COLOR_FONT_PATH is required
#endif

using namespace nkui;

int main() {
    SkribidiAdapter adapter;
    if (!adapter.valid() || !adapter.add_font(NKUI_TEST_FONT_PATH) ||
        !adapter.add_font(NKUI_TEST_COLOR_FONT_PATH, FontFamily::Emoji) ||
        !adapter.layout_utf8("áb NativeKit مرحبا", 500.0f, 28.0f))
        return 1;
    if (!adapter.font_collection_generation() || !adapter.layout_generation())
        return 2;
    if (adapter.layout_build_count() != 1 ||
        !adapter.layout_utf8("áb NativeKit مرحبا", 500.0f, 28.0f) ||
        adapter.layout_build_count() != 1 || adapter.bounds().width <= 0.0f)
        return 3;
    const int32_t next = adapter.next_grapheme(0);
    if (next <= 1 || adapter.previous_grapheme(next) != 0 || adapter.align_grapheme(1) != 0)
        return 4;
    const TextPosition start{0, 1};
    const TextPosition end{next, 1};
    const auto caret = adapter.caret(start);
    const auto hit = adapter.hit_test(caret.x, caret.y);
    if (hit.offset < 0 || adapter.selection_rects(start, end).empty())
        return 5;
    PreparedGlyphs glyphs;
    if (!adapter.prepare_glyphs(10.0f, 40.0f, 1.0f, GlyphMode::Alpha, glyphs))
        return 6;
    if (glyphs.vertices.empty() || glyphs.indices.empty() || glyphs.batches.empty() ||
        adapter.atlas_texture_count() == 0)
        return 7;
    const auto initial_stats = adapter.stats();
    if (!initial_stats.glyph_cache_misses || !initial_stats.glyphs_rasterized ||
        initial_stats.prepared_batch_count < glyphs.batches.size())
        return 8;
    for (const auto &batch : glyphs.batches)
        if (!batch.atlas.value || batch.mode != GlyphMode::Alpha || !batch.vertex_count ||
            !batch.index_count)
            return 9;
    const auto first_query = adapter.pending_atlas_uploads();
    const auto second_query = adapter.pending_atlas_uploads();
    if (first_query.empty() || first_query.size() != second_query.size())
        return 10;
    for (const auto &batch : glyphs.batches) {
        bool matched_generation = false;
        for (const auto &upload : first_query)
            matched_generation = matched_generation ||
                                 (upload.texture.value == batch.atlas.value &&
                                  upload.generation == batch.atlas_generation);
        if (!matched_generation)
            return 11;
    }
    if (first_query.front().format != AtlasTextureFormat::R8Mask ||
        adapter.acknowledge_atlas_upload(first_query.front().texture,
                                         first_query.front().dirty_epoch + 1) ||
        adapter.pending_atlas_uploads().empty())
        return 12;
    for (const auto &upload : first_query)
        if (!upload.texture.value || !upload.pixels ||
            (upload.bytes_per_pixel != 1 && upload.bytes_per_pixel != 4) || upload.width <= 0 ||
            upload.height <= 0 || upload.row_pitch <= 0 ||
            !upload.generation || !upload.dirty_epoch ||
            !adapter.acknowledge_atlas_upload(upload.texture, upload.dirty_epoch))
            return 13;
    if (!adapter.pending_atlas_uploads().empty())
        return 14;
    const auto clean_uploads = adapter.atlas_uploads(true);
    if (clean_uploads.size() != first_query.size())
        return 14;
    for (const auto &upload : clean_uploads)
        if (upload.dirty || upload.x != 0 || upload.y != 0 ||
            upload.width != upload.texture_width || upload.height != upload.texture_height)
            return 15;
    const uint64_t stable_layout_generation = adapter.layout_generation();
    PreparedGlyphs fractional_glyphs;
    if (!adapter.prepare_glyphs(10.0f, 40.0f, 1.25f, GlyphMode::Alpha, fractional_glyphs) ||
        adapter.layout_generation() != stable_layout_generation)
        return 15;
    for (const auto &upload : adapter.pending_atlas_uploads())
        if (!adapter.acknowledge_atlas_upload(upload.texture, upload.dirty_epoch))
            return 16;
    const auto fractional_stats = adapter.stats();
    PreparedGlyphs repeated_fractional_glyphs;
    if (!adapter.prepare_glyphs(10.0f, 40.0f, 1.25f, GlyphMode::Alpha,
                                repeated_fractional_glyphs) ||
        !adapter.pending_atlas_uploads().empty())
        return 17;
    const auto repeated_stats = adapter.stats();
    if (repeated_stats.glyph_cache_misses != fractional_stats.glyph_cache_misses ||
        repeated_stats.glyphs_rasterized != fractional_stats.glyphs_rasterized ||
        repeated_stats.prepared_batch_count <= fractional_stats.prepared_batch_count)
        return 18;
    PreparedGlyphs native_scale_glyphs;
    if (!adapter.prepare_glyphs(10.0f, 40.0f, 1.0f, GlyphMode::Alpha, native_scale_glyphs) ||
        !adapter.pending_atlas_uploads().empty())
        return 19;
    PreparedGlyphs sdf_glyphs;
    if (!adapter.prepare_glyphs(10.0f, 40.0f, 1.0f, GlyphMode::Sdf, sdf_glyphs) ||
        sdf_glyphs.batches.empty())
        return 20;
    const auto sdf_uploads = adapter.pending_atlas_uploads();
    bool saw_sdf = false;
    for (const auto &upload : sdf_uploads) {
        saw_sdf = saw_sdf || upload.format == AtlasTextureFormat::R8Sdf;
        if (upload.format != AtlasTextureFormat::R8Sdf || upload.bytes_per_pixel != 1 ||
            !adapter.acknowledge_atlas_upload(upload.texture, upload.dirty_epoch))
            return 21;
    }
    if (!saw_sdf || !adapter.pending_atlas_uploads().empty())
        return 22;
    if (!adapter.prepared_glyphs_current(glyphs))
        return 23;
    PreparedGlyphs stale_glyphs;
    if (!adapter.prepare_glyphs(10.0f, 40.0f, 1.125f, GlyphMode::Alpha, stale_glyphs))
        return 24;
    const auto stale_uploads = adapter.pending_atlas_uploads();
    if (stale_uploads.empty())
        return 25;
    PreparedGlyphs updated_glyphs;
    if (!adapter.prepare_glyphs(10.0f, 40.0f, 1.375f, GlyphMode::Alpha, updated_glyphs))
        return 26;
    bool replaced_first_epoch = false;
    for (const auto &upload : adapter.pending_atlas_uploads()) {
        for (const auto &stale : stale_uploads) {
            if (upload.texture.value == stale.texture.value &&
                upload.dirty_epoch != stale.dirty_epoch) {
                replaced_first_epoch = true;
                if (adapter.acknowledge_atlas_upload(stale.texture, stale.dirty_epoch))
                    return 27;
            }
        }
        if (!adapter.acknowledge_atlas_upload(upload.texture, upload.dirty_epoch))
            return 28;
    }
    if (!replaced_first_epoch || !adapter.pending_atlas_uploads().empty())
        return 29;
    if (adapter.layout_build_count() != 1 || adapter.layout_generation() != stable_layout_generation)
        return 30;

    if (!adapter.layout_utf8("😀", 80.0f, 32.0f))
        return 31;
    PreparedGlyphs color_glyphs;
    if (!adapter.prepare_glyphs(0.0f, 0.0f, 1.0f, GlyphMode::Color, color_glyphs) ||
        color_glyphs.batches.empty())
        return 31;
    bool saw_color = false;
    for (const auto &batch : color_glyphs.batches)
        saw_color = saw_color || batch.mode == GlyphMode::Color;
    for (const auto &upload : adapter.pending_atlas_uploads()) {
        if (upload.format != AtlasTextureFormat::Rgba8Premultiplied || upload.bytes_per_pixel != 4)
            return 32;
        if (!adapter.acknowledge_atlas_upload(upload.texture, upload.dirty_epoch))
            return 33;
    }
    if (!saw_color || !adapter.pending_atlas_uploads().empty())
        return 34;

    const char *unicode_samples[] = {
        "שלום עולם", "नमस्ते दुनिया", "你好世界", "👩‍🚀", "क्‍ष", "office ﬁ á",
    };
    int sample_index = 0;
    for (const char *sample : unicode_samples) {
        if (!adapter.layout_utf8(sample, 90.0f, 24.0f) || adapter.bounds().width <= 0.0f)
            return 35;
        const int32_t end = adapter.next_grapheme(0);
        if (end <= 0 || (sample_index != 4 && adapter.selection_rects({0, 0}, {end, 0}).empty()))
            return 36;
        ++sample_index;
    }
    TextLayoutOptions options;
    options.font_size = 24.0f;
    options.letter_spacing = 1.0f;
    options.line_height = 40.0f;
    options.wrap = TextWrapMode::None;
    if (!adapter.layout_utf8("NativeKit text options", 500.0f, options) ||
        adapter.bounds().height < 39.0f)
        return 37;
    const uint32_t options_builds = adapter.layout_build_count();
    if (!adapter.layout_utf8("NativeKit text options", 500.0f, options) ||
        adapter.layout_build_count() != options_builds)
        return 38;
    TextLayoutOptions wrapped_options;
    wrapped_options.font_size = 24.0f;
    wrapped_options.wrap = TextWrapMode::WordCharacter;
    TextLayoutResult wrapped;
    if (!adapter.layout_utf8("Skribidi owns paragraph wrapping in NativeKit", 90.0f,
                            wrapped_options, &wrapped) ||
        !wrapped.id || wrapped.lines.size() < 2)
        return 39;
    PreparedGlyphs first_line;
    PreparedGlyphs second_line;
    if (!adapter.prepare_glyphs_for_line(0, 0.0f, 0.0f, 1.0f, GlyphMode::Alpha, first_line) ||
        !adapter.prepare_glyphs_for_line(1, 0.0f, 0.0f, 1.0f, GlyphMode::Alpha, second_line) ||
        first_line.vertices.empty() || second_line.vertices.empty())
        return 40;
    return glyphs.vertices.size() % 4 == 0 && glyphs.indices.size() % 6 == 0 ? 0 : 41;
}
