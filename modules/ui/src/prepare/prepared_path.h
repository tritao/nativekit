#ifndef NATIVEKIT_UI_PREPARED_PATH_H
#define NATIVEKIT_UI_PREPARED_PATH_H

#include <cstdint>
#include <memory>
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

enum class PreparedImageFlags : uint32_t {
    None = 0,
    GenerateMipmaps = 1u << 0,
    RepeatX = 1u << 1,
    RepeatY = 1u << 2,
    FlipY = 1u << 3,
    Premultiplied = 1u << 4,
    Nearest = 1u << 5,
};

constexpr PreparedImageFlags operator|(PreparedImageFlags lhs, PreparedImageFlags rhs) {
    return static_cast<PreparedImageFlags>(static_cast<uint32_t>(lhs) |
                                           static_cast<uint32_t>(rhs));
}

constexpr PreparedImageFlags &operator|=(PreparedImageFlags &lhs, PreparedImageFlags rhs) {
    lhs = lhs | rhs;
    return lhs;
}

constexpr bool has_flag(PreparedImageFlags flags, PreparedImageFlags flag) {
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(flag)) != 0;
}

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
    PreparedImageFlags flags = PreparedImageFlags::None;
    uint32_t generation = 0;
    bool dirty = false;
    std::vector<uint8_t> pixels;
};

struct PreparedPathData {
    std::vector<PreparedPathOperation> operation_data;
    std::vector<PreparedPathRange> path_data;
    std::vector<PreparedVertex> vertex_data;
    std::vector<PreparedTexture> texture_data;
    const std::vector<PreparedPathRange> *path_view = nullptr;
    const std::vector<PreparedVertex> *vertex_view = nullptr;

    const std::vector<PreparedPathOperation> &operations() const { return operation_data; }
    const std::vector<PreparedPathRange> &paths() const {
        return path_view ? *path_view : path_data;
    }
    const std::vector<PreparedVertex> &vertices() const {
        return vertex_view ? *vertex_view : vertex_data;
    }
    const std::vector<PreparedTexture> &textures() const { return texture_data; }
};

} // namespace nkui

#endif
