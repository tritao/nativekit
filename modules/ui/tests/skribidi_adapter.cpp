#include "prepare/skribidi_adapter.h"
#include "prepare/skribidi_document_engine.h"

#include <cmath>
#include <initializer_list>
#include <vector>

#ifndef NKUI_TEST_FONT_PATH
#error NKUI_TEST_FONT_PATH is required
#endif
#ifndef NKUI_TEST_COLOR_FONT_PATH
#error NKUI_TEST_COLOR_FONT_PATH is required
#endif

using namespace nkui;

namespace {

bool check_grapheme_boundaries(SkribidiAdapter &adapter, const char *text,
                               std::initializer_list<int32_t> expected) {
    TextLayoutOptions options;
    options.font_size = 24.0f;
    options.wrap = TextWrapMode::None;
    if (!adapter.layout_utf8(text, 800.0f, options))
        return false;

    const std::vector<int32_t> boundaries(expected);
    if (boundaries.size() < 2 || boundaries.front() != 0 || boundaries.back() < boundaries.front())
        return false;
    for (std::size_t index = 0; index + 1 < boundaries.size(); ++index) {
        const int32_t current = boundaries[index];
        const int32_t next = boundaries[index + 1];
        if (next <= current || adapter.align_grapheme(current) != current ||
            adapter.next_grapheme(current) != next || adapter.previous_grapheme(next) != current)
            return false;
        for (int32_t offset = current + 1; offset < next; ++offset) {
            const int32_t aligned = adapter.align_grapheme(offset);
            if (aligned != current && aligned != next)
                return false;
            if (adapter.next_grapheme(offset) != next)
                return false;
        }
    }
    return adapter.align_grapheme(boundaries.back()) == boundaries.back() &&
           adapter.next_grapheme(boundaries.back()) == boundaries.back() &&
           adapter.previous_grapheme(boundaries.front()) == boundaries.front();
}

bool check_text_geometry(SkribidiAdapter &adapter, const char *text, float width,
                         int32_t text_length, std::size_t minimum_lines) {
    TextLayoutOptions options;
    options.font_size = 24.0f;
    options.wrap = TextWrapMode::WordCharacter;
    TextLayoutResult result;
    if (!adapter.layout_utf8(text, width, options, &result) ||
        result.lines.size() < minimum_lines || text_length <= 0)
        return false;

    const auto finite_rect = [](const TextRect &rect) {
        return std::isfinite(rect.x) && std::isfinite(rect.y) && std::isfinite(rect.width) &&
               std::isfinite(rect.height) && rect.width >= 0.0f && rect.height >= 0.0f;
    };
    const auto selection = adapter.selection_rects({0, 0}, {text_length, 0});
    if (selection.size() < minimum_lines)
        return false;
    for (const auto &rect : selection)
        if (!finite_rect(rect) || rect.width <= 0.0f || rect.height <= 0.0f)
            return false;

    bool saw_multiple_lines = false;
    for (std::size_t index = 1; index < selection.size(); ++index)
        saw_multiple_lines =
            saw_multiple_lines || std::abs(selection[index].y - selection[index - 1].y) > 0.01f;
    if (minimum_lines > 1 && !saw_multiple_lines)
        return false;

    for (int32_t offset = 0; offset <= text_length; ++offset) {
        const TextCaret caret = adapter.caret({offset, 0});
        if (!std::isfinite(caret.x) || !std::isfinite(caret.y) || !std::isfinite(caret.ascender) ||
            !std::isfinite(caret.descender))
            return false;
        const TextPosition hit = adapter.hit_test(caret.x, caret.y);
        if (hit.offset < 0 || hit.offset > text_length)
            return false;
    }
    return true;
}

bool check_document_offset_mappings(const std::shared_ptr<SkribidiFontCollection> &fonts) {
    TextLayoutOptions options;
    options.font_size = 18.0f;
    const char *text = "A\xC3\xA9"
                       "e\xCC\x81"
                       "\xF0\x9F\x99\x82"
                       "\n"
                       "日本";
    SkribidiDocumentEngine document(fonts, text, 400.0f, options);
    if (!document.valid() || document.document_length() != 8)
        return false;

    constexpr int32_t utf8_bytes = 17;
    constexpr int32_t utf16_units = 9;
    for (int32_t codepoint = 0; codepoint <= document.document_length(); ++codepoint) {
        int32_t utf8 = -1;
        int32_t utf16 = -1;
        int32_t roundtrip = -1;
        if (!document.codepoint_to_utf8_byte_offset(codepoint, &utf8) ||
            !document.codepoint_to_utf16_unit_offset(codepoint, &utf16) ||
            !document.utf8_byte_offset_to_codepoint(utf8, &roundtrip) || roundtrip != codepoint ||
            !document.utf16_unit_offset_to_codepoint(utf16, &roundtrip) || roundtrip != codepoint)
            return false;
    }

    int32_t ignored = 0;
    return document.codepoint_to_utf8_byte_offset(document.document_length(), &ignored) &&
           ignored == utf8_bytes &&
           document.codepoint_to_utf16_unit_offset(document.document_length(), &ignored) &&
           ignored == utf16_units && !document.utf8_byte_offset_to_codepoint(2, &ignored) &&
           !document.utf16_unit_offset_to_codepoint(5, &ignored);
}

} // namespace

int main() {
    auto shared_fonts = std::make_shared<SkribidiFontCollection>();
    if (!shared_fonts->valid() || !shared_fonts->add_font(NKUI_TEST_FONT_PATH) ||
        shared_fonts->font_load_count() != 1)
        return 48;
    SkribidiAdapter shared_first(shared_fonts);
    SkribidiAdapter shared_second(shared_fonts);
    if (!shared_first.layout_utf8("first", 200.0f, 16.0f) ||
        !shared_second.layout_utf8("second", 200.0f, 16.0f) ||
        shared_fonts->font_load_count() != 1 ||
        shared_first.font_collection_generation() != shared_second.font_collection_generation())
        return 49;
    if (!check_document_offset_mappings(shared_fonts))
        return 51;
    PreparedGlyphs shared_glyphs;
    if (!shared_first.prepare_glyphs(0.0f, 0.0f, 1.0f, GlyphMode::Alpha, shared_glyphs) ||
        shared_glyphs.vertices.empty())
        return 50;

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
            matched_generation =
                matched_generation || (upload.texture.value == batch.atlas.value &&
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
            upload.height <= 0 || upload.row_pitch <= 0 || !upload.generation ||
            !upload.dirty_epoch ||
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
    if (adapter.layout_build_count() != 1 ||
        adapter.layout_generation() != stable_layout_generation)
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
    const char *mixed_selection = "こんにちは — שלום — NativeKit مرحبا";
    if (!adapter.layout_utf8(mixed_selection, 500.0f, 24.0f))
        return 51;
    const auto mixed_rects = adapter.selection_rects({0, 0}, {32, 0});
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
    if (!adapter.layout_utf8("NativeKit text options", 500.0f, options) ||
        adapter.bounds().height < 39.0f)
        return 37;

    TextLayoutOptions intrinsic_options;
    intrinsic_options.font_size = 24.0f;
    intrinsic_options.wrap = TextWrapMode::Word;
    TextIntrinsicMetrics intrinsic_metrics;
    if (!adapter.measure_intrinsic_utf8("short supercalifragilistic", intrinsic_options,
                                        &intrinsic_metrics) ||
        intrinsic_metrics.min_content_width <= 0.0f ||
        intrinsic_metrics.max_content_width < intrinsic_metrics.min_content_width ||
        intrinsic_metrics.natural_height <= 0.0f || !intrinsic_metrics.has_baseline ||
        !std::isfinite(intrinsic_metrics.first_baseline))
        return 53;
    TextIntrinsicMetrics newline_metrics;
    intrinsic_options.wrap = TextWrapMode::None;
    if (!adapter.measure_intrinsic_utf8("short\nlonger line", intrinsic_options,
                                        &newline_metrics) ||
        newline_metrics.min_content_width <= 0.0f ||
        newline_metrics.max_content_width < newline_metrics.min_content_width)
        return 54;

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

    TextLayoutOptions direction_options;
    direction_options.font_size = 24.0f;
    direction_options.wrap = TextWrapMode::None;
    TextLayoutResult automatic_direction;
    TextLayoutResult right_to_left_direction;
    TextLayoutResult automatic_direction_again;
    const uint32_t direction_builds = adapter.layout_build_count();
    if (!adapter.layout_utf8("NativeKit", 300.0f, direction_options, &automatic_direction))
        return 44;
    direction_options.direction = TextDirection::Rtl;
    if (!adapter.layout_utf8("NativeKit", 300.0f, direction_options, &right_to_left_direction) ||
        automatic_direction.id == right_to_left_direction.id ||
        automatic_direction.lines.size() != 1 || right_to_left_direction.lines.size() != 1 ||
        right_to_left_direction.lines.front().bounds.x <=
            automatic_direction.lines.front().bounds.x + 1.0f ||
        adapter.layout_build_count() != direction_builds + 2)
        return 45;
    direction_options.direction = TextDirection::Auto;
    if (!adapter.layout_utf8("NativeKit", 300.0f, direction_options, &automatic_direction_again) ||
        automatic_direction_again.id != automatic_direction.id ||
        adapter.layout_build_count() != direction_builds + 2)
        return 46;

    TextLayoutResult retained_first;
    TextLayoutResult retained_second;
    TextLayoutOptions retained_options;
    retained_options.font_size = 22.0f;
    retained_options.wrap = TextWrapMode::WordCharacter;
    if (!adapter.layout_utf8("first retained paragraph", 120.0f, retained_options,
                             &retained_first) ||
        !adapter.layout_utf8("second retained paragraph", 120.0f, retained_options,
                             &retained_second) ||
        !retained_first.id || !retained_second.id || retained_first.id == retained_second.id ||
        !adapter.has_layout(retained_first.id) || !adapter.has_layout(retained_second.id))
        return 42;
    const uint32_t retained_builds = adapter.layout_build_count();
    PreparedGlyphs retained_first_glyphs;
    PreparedGlyphs retained_second_glyphs;
    if (!adapter.prepare_glyphs_for_line(retained_first.id, 0, 0.0f, 0.0f, 1.0f, GlyphMode::Alpha,
                                         retained_first_glyphs) ||
        !adapter.prepare_glyphs_for_line(retained_second.id, 0, 0.0f, 0.0f, 1.0f, GlyphMode::Alpha,
                                         retained_second_glyphs) ||
        retained_first_glyphs.layout_id != retained_first.id ||
        retained_second_glyphs.layout_id != retained_second.id ||
        adapter.layout_build_count() != retained_builds)
        return 43;
    const std::vector<TextLayoutId> kept_layouts{retained_second.id};
    adapter.prune_layout_cache(kept_layouts, 1);
    if (adapter.has_layout(retained_first.id) || !adapter.has_layout(retained_second.id))
        return 47;

    if (!check_grapheme_boundaries(adapter, "é", {0, 2}))
        return 55;
    if (!check_grapheme_boundaries(adapter, "👩‍🚀", {0, 3}))
        return 57;
    if (!check_grapheme_boundaries(adapter, "👨‍👩‍👧‍👦", {0, 7}))
        return 58;
    if (!check_grapheme_boundaries(adapter, "🇺🇸🇯🇵", {0, 2, 4}))
        return 59;
    if (!check_grapheme_boundaries(adapter, "👍🏽", {0, 2}))
        return 60;
    if (!check_grapheme_boundaries(adapter, "क्‍ष", {0, 4}))
        return 61;
    if (!check_grapheme_boundaries(adapter, "\r\nb", {0, 2, 3}))
        return 62;
    if (!check_grapheme_boundaries(adapter, "각", {0, 3}))
        return 63;
    if (!check_text_geometry(adapter, "مرحبا NativeKit — שלום — こんにちは", 110.0f, 30, 2))
        return 64;
    if (!check_text_geometry(adapter, "one\ntwo\nthree", 180.0f, 13, 3))
        return 65;
    return glyphs.vertices.size() % 4 == 0 && glyphs.indices.size() % 6 == 0 ? 0 : 41;
}
