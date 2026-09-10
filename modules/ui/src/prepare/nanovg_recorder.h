#ifndef NATIVEKIT_UI_NANOVG_RECORDER_H
#define NATIVEKIT_UI_NANOVG_RECORDER_H

#include "nanovg.h"

#include <cstdint>
#include <vector>

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

struct PreparedPathOperation {
    PreparedPathKind kind{};
    NVGpaint paint{};
    NVGcompositeOperationState composite{};
    NVGscissor scissor{};
    float fringe = 0.0f;
    float stroke_width = 0.0f;
    float bounds[4]{};
    uint32_t path_offset = 0;
    uint32_t path_count = 0;
    uint32_t vertex_offset = 0;
    uint32_t vertex_count = 0;
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
    const std::vector<NVGvertex> &vertices() const;
    NanoVGRecorderStats stats() const;

  private:
    State *state_ = nullptr;
    NVGcontext *context_ = nullptr;
};

} // namespace nkui

#endif
