#include "prepare/skribidi_adapter.h"

#ifndef NKUI_TEST_FONT_PATH
#error NKUI_TEST_FONT_PATH is required
#endif

using namespace nkui;

int main() {
    SkribidiAdapter adapter;
    if (!adapter.valid() || !adapter.add_font(NKUI_TEST_FONT_PATH) ||
        !adapter.layout_utf8("NativeKit مرحبا", 500.0f, 28.0f))
        return 1;
    PreparedGlyphs glyphs;
    if (!adapter.prepare_glyphs(10.0f, 40.0f, 1.0f, GlyphMode::Alpha, glyphs))
        return 2;
    if (glyphs.vertices.empty() || glyphs.indices.empty() || glyphs.batches.empty() ||
        adapter.atlas_texture_count() == 0)
        return 3;
    for (const auto &batch : glyphs.batches)
        if (batch.mode != GlyphMode::Alpha || !batch.vertex_count || !batch.index_count)
            return 4;
    const auto first_query = adapter.pending_atlas_uploads();
    const auto second_query = adapter.pending_atlas_uploads();
    if (first_query.empty() || first_query.size() != second_query.size())
        return 5;
    for (const auto &upload : first_query)
        if (!upload.texture.value || !upload.pixels ||
            (upload.bytes_per_pixel != 1 && upload.bytes_per_pixel != 4) || upload.width <= 0 ||
            upload.height <= 0 || upload.row_pitch <= 0 ||
            !adapter.acknowledge_atlas_upload(upload.texture))
            return 6;
    if (!adapter.pending_atlas_uploads().empty())
        return 7;
    return glyphs.vertices.size() % 4 == 0 && glyphs.indices.size() % 6 == 0 ? 0 : 8;
}
