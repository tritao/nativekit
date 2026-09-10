#ifndef NATIVEKIT_UI_SKRIBIDI_ADAPTER_H
#define NATIVEKIT_UI_SKRIBIDI_ADAPTER_H

#include <cstdint>
#include <vector>

namespace nkui {

enum class GlyphMode : uint8_t {
    Alpha = 1,
    Sdf,
    Color,
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
};

struct AtlasUpload {
    AtlasTextureId texture;
    uint8_t texture_index = 0;
    uint8_t bytes_per_pixel = 0;
    int32_t texture_width = 0;
    int32_t texture_height = 0;
    int32_t row_pitch = 0;
    int32_t x = 0;
    int32_t y = 0;
    int32_t width = 0;
    int32_t height = 0;
    const uint8_t *pixels = nullptr;
};

class SkribidiAdapter {
  public:
    SkribidiAdapter();
    ~SkribidiAdapter();
    SkribidiAdapter(const SkribidiAdapter &) = delete;
    SkribidiAdapter &operator=(const SkribidiAdapter &) = delete;

    bool valid() const;
    bool add_font(const char *path, FontFamily family = FontFamily::Default);
    bool layout_utf8(const char *text, float width, float font_size);
    bool prepare_glyphs(float origin_x, float origin_y, float pixel_scale, GlyphMode mode,
                        PreparedGlyphs &output);
    uint32_t atlas_texture_count() const;
    std::vector<AtlasUpload> pending_atlas_uploads() const;
    bool acknowledge_atlas_upload(AtlasTextureId texture);

    struct State;

  private:
    State *state_ = nullptr;
};

} // namespace nkui

#endif
