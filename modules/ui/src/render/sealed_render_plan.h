#ifndef NATIVEKIT_UI_SEALED_RENDER_PLAN_H
#define NATIVEKIT_UI_SEALED_RENDER_PLAN_H

#include "compositor/render_plan.h"
#include "render/frame_resources.h"

#include <cstdint>
#include <memory>
#include <utility>

namespace nkui {

/** Describes why a render plan could not be sealed. */
struct RenderPlanSealError {
    const char *message = nullptr;
};

/**
 * Immutable render plan with everything it needs kept alive.
 *
 * Sealing is the boundary between building a frame and rendering it: the plan
 * and its resource set are owned by this object, no layout frame, session, or
 * display list has to stay alive, and nothing in it points at a language
 * binding or a callback. A sealed plan is reference counted, so frame N can be
 * rendered while frame N+1 is built.
 */
class SealedRenderPlan {
  public:
    /**
     * Seals one compiled plan and its resource set.
     *
     * Fails when the resources reference a live surface producer, which is a
     * callback by nature, or when any path, image, text, or graphics image is
     * only borrowed from its builder.
     */
    static std::shared_ptr<const SealedRenderPlan> seal(RenderPlan plan, FrameResources resources,
                                                        RenderPlanSealError *error = nullptr);

    const RenderPlan &plan() const { return plan_; }
    const FrameResources &resources() const { return resources_; }
    uint32_t pass_count() const { return static_cast<uint32_t>(plan_.passes.size()); }

  private:
    SealedRenderPlan(RenderPlan plan, FrameResources resources)
        : plan_(std::move(plan)), resources_(std::move(resources)) {}

    RenderPlan plan_;
    FrameResources resources_;
};

} // namespace nkui

#endif
