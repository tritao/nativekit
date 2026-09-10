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

struct PreparedPathRange {
    uint32_t fill_offset = 0;
    uint32_t fill_count = 0;
    uint32_t stroke_offset = 0;
    uint32_t stroke_count = 0;
    bool closed = false;
    bool convex = false;
};

struct PreparedColor {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 0.0f;
};

struct PreparedPaint {
    float xform[6]{};
    float extent[2]{};
    float radius = 0.0f;
    float feather = 0.0f;
    PreparedColor innerColor{};
    PreparedColor outerColor{};
    int image = 0;
};

struct PreparedBlend {
    int srcRGB = 0;
    int dstRGB = 0;
    int srcAlpha = 0;
    int dstAlpha = 0;
};

struct PreparedScissor {
    float xform[6]{};
    float extent[2]{};
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
    PreparedBlend composite{};
    PreparedScissor scissor{};
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
    int id = 0;
    int type = 0;
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
