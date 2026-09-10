#ifndef NATIVEKIT_UI_FRAME_RESOURCES_H
#define NATIVEKIT_UI_FRAME_RESOURCES_H

#include "display_list/display_list.h"
#include "prepare/nanovg_recorder.h"
#include "prepare/skribidi_adapter.h"

#include <cstdint>
#include <unordered_map>

namespace nkui {

struct PreparedPathRef {
    const NanoVGRecorder *recorder = nullptr;
    uint32_t operation_index = 0;
};

class FrameResources {
  public:
    bool bind_path(ResourceId id, const NanoVGRecorder &recorder, uint32_t operation_index);
    bool bind_text(ResourceId id, const PreparedGlyphs &glyphs);
    const PreparedPathRef *path(ResourceId id) const;
    const PreparedGlyphs *text(ResourceId id) const;
    void reset();

  private:
    std::unordered_map<uint32_t, PreparedPathRef> paths_;
    std::unordered_map<uint32_t, const PreparedGlyphs *> texts_;
};

} // namespace nkui

#endif
