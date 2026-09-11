#ifndef NATIVEKIT_UI_SKRIBIDI_ADAPTER_H
#define NATIVEKIT_UI_SKRIBIDI_ADAPTER_H

#include <cstdint>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace nkui {

enum class GlyphMode : uint8_t {
    Alpha = 1,
    Sdf,
    Color,
};

enum class AtlasTextureFormat : uint8_t {
    R8Mask = 0,
    R8Sdf,
    Rgba8Premultiplied,
};

enum class FontFamily : uint8_t {
    Default = 0,
    Emoji = 1,
};

struct GlyphVertex {
    float x;
    float y;
    float u;
    float v;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t alpha;
};

struct AtlasTextureId {
    uint32_t value = 0;
};

struct GlyphBatch {
    AtlasTextureId atlas;
    uint32_t atlas_generation = 0;
    GlyphMode mode = GlyphMode::Alpha;
    uint32_t first_vertex = 0;
    uint32_t vertex_count = 0;
    uint32_t first_index = 0;
    uint32_t index_count = 0;
};

struct PreparedGlyphs {
    std::vector<GlyphVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<GlyphBatch> batches;
    float origin_x = 0.0f;
    float origin_y = 0.0f;
    float pixel_scale = 1.0f;
    GlyphMode mode = GlyphMode::Alpha;
    uint64_t layout_generation = 0;
};

struct TextPosition {
    int32_t offset = 0;
    uint8_t affinity = 0;
};

struct TextCaret {
    float x = 0.0f;
    float y = 0.0f;
    float ascender = 0.0f;
    float descender = 0.0f;
    float slope = 0.0f;
    uint8_t direction = 0;
};

struct TextRect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

struct AtlasUpload {
    AtlasTextureId texture;
    uint8_t texture_index = 0;
    AtlasTextureFormat format = AtlasTextureFormat::R8Mask;
    uint8_t bytes_per_pixel = 0;
    int32_t texture_width = 0;
    int32_t texture_height = 0;
    int32_t row_pitch = 0;
    int32_t x = 0;
    int32_t y = 0;
    int32_t width = 0;
    int32_t height = 0;
    const uint8_t *pixels = nullptr;
    bool dirty = false;
    uint32_t generation = 0;
    uint64_t dirty_epoch = 0;
};

struct SkribidiAdapterStats {
    uint64_t glyph_cache_misses = 0;
    uint64_t glyphs_rasterized = 0;
    uint64_t prepared_batch_count = 0;
};

class SkribidiAdapter {
  public:
    SkribidiAdapter();
    ~SkribidiAdapter();
    SkribidiAdapter(const SkribidiAdapter &) = delete;
    SkribidiAdapter &operator=(const SkribidiAdapter &) = delete;

    bool valid() const;
    bool set_atlas_namespace(uint16_t value);
    bool add_font(const char *path, FontFamily family = FontFamily::Default);
    bool add_font_from_data(const char *name, const void *data, std::size_t bytes,
                            FontFamily family = FontFamily::Default);
    bool add_font_from_shared_data(const char *name,
                                   const std::shared_ptr<std::vector<uint8_t>> &data,
                                   FontFamily family = FontFamily::Default);
    bool add_system_fallbacks();
    bool layout_utf8(const char *text, float width, float font_size);
    bool prepare_glyphs(float origin_x, float origin_y, float pixel_scale, GlyphMode mode,
                        PreparedGlyphs &output);
    bool prepared_glyphs_current(const PreparedGlyphs &glyphs) const;
    TextRect bounds() const;
    TextPosition hit_test(float x, float y) const;
    TextCaret caret(TextPosition position) const;
    int32_t next_grapheme(int32_t offset) const;
    int32_t previous_grapheme(int32_t offset) const;
    int32_t align_grapheme(int32_t offset) const;
    std::vector<TextRect> selection_rects(TextPosition start, TextPosition end) const;
    uint64_t font_collection_generation() const;
    uint64_t layout_generation() const;
    uint32_t layout_build_count() const;
    uint32_t atlas_texture_count() const;
    SkribidiAdapterStats stats() const;
    std::vector<AtlasUpload> pending_atlas_uploads() const;
    std::vector<AtlasUpload> atlas_uploads(bool include_clean) const;
    bool acknowledge_atlas_upload(AtlasTextureId texture, uint64_t dirty_epoch);

    struct State;

  private:
    State *state_ = nullptr;
};

} // namespace nkui

#endif
