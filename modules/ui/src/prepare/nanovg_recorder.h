#ifndef NATIVEKIT_UI_NANOVG_RECORDER_H
#define NATIVEKIT_UI_NANOVG_RECORDER_H

#include <cstdint>
#include <vector>

typedef struct NVGcontext NVGcontext;

namespace nkui {

enum class PreparedPathKind : uint8_t {
    Fill = 1,
    Stroke,
    Triangles,
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

struct NanoVGRecorderStats {
    uint32_t operations = 0;
    uint32_t paths = 0;
    uint32_t vertices = 0;
    uint32_t flushes = 0;
};

class NanoVGRecorder {
  public:
    struct State;

    NanoVGRecorder();
    ~NanoVGRecorder();
    NanoVGRecorder(const NanoVGRecorder &) = delete;
    NanoVGRecorder &operator=(const NanoVGRecorder &) = delete;

    bool valid() const;
    NVGcontext *context() const;
    void reset();

    const std::vector<PreparedPathOperation> &operations() const;
    const std::vector<PreparedPathRange> &paths() const;
    const std::vector<PreparedVertex> &vertices() const;
    const std::vector<PreparedTexture> &textures() const;
    NanoVGRecorderStats stats() const;

  private:
    State *state_ = nullptr;
    NVGcontext *context_ = nullptr;
};

} // namespace nkui

#endif
