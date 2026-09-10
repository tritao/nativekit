#ifndef NATIVEKIT_UI_FRAME_RESOURCES_H
#define NATIVEKIT_UI_FRAME_RESOURCES_H

#include "display_list/display_list.h"
#include "prepare/nanovg_recorder.h"
#include "prepare/skribidi_adapter.h"

#include <cstdint>
#include <unordered_map>

namespace nkui {

class SokolBackend;

class SurfaceProducer {
  public:
    virtual ~SurfaceProducer() = default;
    virtual bool ready() const = 0;
    virtual uint32_t generation() const = 0;
    virtual bool render(SokolBackend &backend, ResourceId target, int width, int height) = 0;
};

struct PreparedPathRef {
    const NanoVGRecorder *recorder = nullptr;
    uint32_t operation_index = 0;
};

class FrameResources {
  public:
    bool bind_path(ResourceId id, const NanoVGRecorder &recorder, uint32_t operation_index);
    bool bind_text(ResourceId id, const PreparedGlyphs &glyphs);
    bool bind_surface(ResourceId id, SurfaceProducer &producer);
    const PreparedPathRef *path(ResourceId id) const;
    const PreparedGlyphs *text(ResourceId id) const;
    SurfaceProducer *surface(ResourceId id) const;
    void reset();

  private:
    std::unordered_map<uint32_t, PreparedPathRef> paths_;
    std::unordered_map<uint32_t, const PreparedGlyphs *> texts_;
    std::unordered_map<uint32_t, SurfaceProducer *> surfaces_;
};

} // namespace nkui

#endif
