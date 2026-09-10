#ifndef NATIVEKIT_UI_NANOVG_RECORDER_H
#define NATIVEKIT_UI_NANOVG_RECORDER_H

#include "prepared_path.h"

typedef struct NVGcontext NVGcontext;

namespace nkui {

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
    const PreparedPathData &data() const;
    NanoVGRecorderStats stats() const;

  private:
    State *state_ = nullptr;
    NVGcontext *context_ = nullptr;
};

} // namespace nkui

#endif
