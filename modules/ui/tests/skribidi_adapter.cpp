#include "prepare/skribidi_adapter.h"

#ifndef NKUI_TEST_FONT_PATH
#error NKUI_TEST_FONT_PATH is required
#endif

using namespace nkui;

int main() {
    SkribidiAdapter adapter;
    if (!adapter.valid() || !adapter.add_font(NKUI_TEST_FONT_PATH) ||
        !adapter.layout_utf8("áb NativeKit مرحبا", 500.0f, 28.0f))
        return 1;
    if (adapter.layout_build_count() != 1 ||
        !adapter.layout_utf8("áb NativeKit مرحبا", 500.0f, 28.0f) ||
        adapter.layout_build_count() != 1 || adapter.bounds().width <= 0.0f)
        return 2;
    const int32_t next = adapter.next_grapheme(0);
    if (next <= 1 || adapter.previous_grapheme(next) != 0 || adapter.align_grapheme(1) != 0)
        return 3;
    const TextPosition start{0, 1};
    const TextPosition end{next, 1};
    const auto caret = adapter.caret(start);
    const auto hit = adapter.hit_test(caret.x, caret.y);
    if (hit.offset < 0 || adapter.selection_rects(start, end).empty())
        return 4;
    PreparedGlyphs glyphs;
    if (!adapter.prepare_glyphs(10.0f, 40.0f, 1.0f, GlyphMode::Alpha, glyphs))
        return 5;
    if (glyphs.vertices.empty() || glyphs.indices.empty() || glyphs.batches.empty() ||
        adapter.atlas_texture_count() == 0)
        return 6;
    for (const auto &batch : glyphs.batches)
        if (!batch.atlas.value || batch.mode != GlyphMode::Alpha || !batch.vertex_count ||
            !batch.index_count)
            return 7;
    const auto first_query = adapter.pending_atlas_uploads();
    const auto second_query = adapter.pending_atlas_uploads();
    if (first_query.empty() || first_query.size() != second_query.size())
        return 8;
    for (const auto &upload : first_query)
        if (!upload.texture.value || !upload.pixels ||
            (upload.bytes_per_pixel != 1 && upload.bytes_per_pixel != 4) || upload.width <= 0 ||
            upload.height <= 0 || upload.row_pitch <= 0 ||
            !upload.generation || !adapter.acknowledge_atlas_upload(upload.texture))
            return 9;
    if (!adapter.pending_atlas_uploads().empty())
        return 10;
    const auto clean_uploads = adapter.atlas_uploads(true);
    if (clean_uploads.size() != first_query.size())
        return 11;
    for (const auto &upload : clean_uploads)
        if (upload.dirty || upload.x != 0 || upload.y != 0 ||
            upload.width != upload.texture_width || upload.height != upload.texture_height)
            return 12;
    if (!adapter.layout_utf8("áb NativeKit مرحبا", 400.0f, 28.0f) ||
        adapter.layout_build_count() != 2)
        return 13;
    return glyphs.vertices.size() % 4 == 0 && glyphs.indices.size() % 6 == 0 ? 0 : 14;
}
