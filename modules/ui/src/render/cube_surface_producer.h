#ifndef NATIVEKIT_UI_CUBE_SURFACE_PRODUCER_H
#define NATIVEKIT_UI_CUBE_SURFACE_PRODUCER_H

#include "frame_resources.h"

#include <array>
#include <cstdint>

namespace nkui {

/** Animated indexed cube used to exercise the offscreen surface producer path. */
class CubeSurfaceProducer final : public SurfaceProducer {
  public:
    bool ready() const override { return true; }
    bool describe(int requested_width, int requested_height,
                  SurfaceDescriptor &description) const override;
    uint32_t generation() const override { return generation_; }
    SurfaceRenderResult render(RenderBackend &backend, ResourceId target,
                               const SurfaceDescriptor &description) override;

    /** Selects a deterministic rotation angle and invalidates the retained target. */
    void set_rotation(float radians);

  private:
    float rotation_ = 0.0f;
    uint32_t generation_ = 1;
};

} // namespace nkui

#endif
