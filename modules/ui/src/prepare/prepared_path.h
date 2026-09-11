#ifndef NATIVEKIT_UI_PREPARED_PATH_H
#define NATIVEKIT_UI_PREPARED_PATH_H

#include <cstdint>
#include <vector>

namespace nkui {

enum class PreparedPathKind : uint8_t {
    Fill = 1,
    Stroke,
    Triangles,
};

enum class PathFillRule : uint8_t {
    NonZero = 1,
    EvenOdd = 2,
};

enum PreparedTextureType : int {
    PreparedTextureAlpha = 0x01,
    PreparedTextureRgba = 0x02,
};

enum PreparedImageFlags : int {
    PreparedImageGenerateMipmaps = 1 << 0,
    PreparedImageRepeatX = 1 << 1,
    PreparedImageRepeatY = 1 << 2,
    PreparedImageFlipY = 1 << 3,
    PreparedImagePremultiplied = 1 << 4,
    PreparedImageNearest = 1 << 5,
};

/* NativeKit-local identity for an image used by a prepared paint.  NanoVG
 * image handles are translated to this token at the compatibility boundary;
 * they never become part of the retained path representation. */
using PreparedImageToken = uint32_t;

struct PreparedPathRange {
    int32_t fill_offset = 0;
    int32_t fill_count = 0;
    int32_t stroke_offset = 0;
    int32_t stroke_count = 0;
    uint8_t closed = 0;
    uint8_t convex = 0;
    int32_t winding = 0;
};

struct PreparedColor {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 0.0f;
};

struct PreparedPaint {
    float transform[6]{};
    float extent[2]{};
    float radius = 0.0f;
    float feather = 0.0f;
    PreparedColor inner_color{};
    PreparedColor outer_color{};
    PreparedImageToken image_token = 0;
};

struct PreparedVertex {
    float x = 0.0f;
    float y = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
};

struct PreparedPathOperation {
    PreparedPathKind kind{};
    PreparedPaint paint{};
    float fringe = 0.0f;
    float stroke_width = 0.0f;
    PathFillRule fill_rule = PathFillRule::NonZero;
    float bounds[4]{};
    uint32_t path_offset = 0;
    uint32_t path_count = 0;
    uint32_t vertex_offset = 0;
    uint32_t vertex_count = 0;
};

struct PreparedTexture {
    PreparedImageToken token = 0;
    PreparedTextureType type = PreparedTextureRgba;
    int width = 0;
    int height = 0;
    int flags = 0;
    uint32_t generation = 0;
    bool dirty = false;
    std::vector<uint8_t> pixels;
};

struct PreparedPathData {
    std::vector<PreparedPathOperation> operation_data;
    std::vector<PreparedPathRange> path_data;
    std::vector<PreparedVertex> vertex_data;
    std::vector<PreparedTexture> texture_data;

    const std::vector<PreparedPathOperation> &operations() const { return operation_data; }
    const std::vector<PreparedPathRange> &paths() const { return path_data; }
    const std::vector<PreparedVertex> &vertices() const { return vertex_data; }
    const std::vector<PreparedTexture> &textures() const { return texture_data; }
};

} // namespace nkui

#endif
