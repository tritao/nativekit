#include "render/sealed_render_plan.h"

#include <utility>

namespace nkui {
namespace {

void fail(RenderPlanSealError *error, const char *message) {
    if (error)
        error->message = message;
}

} // namespace

std::shared_ptr<const SealedRenderPlan>
SealedRenderPlan::seal(RenderPlan plan, OwnedFrameResources resources, RenderPlanSealError *error) {
    (void)error;
    return std::shared_ptr<const SealedRenderPlan>(
        new SealedRenderPlan(std::move(plan), std::move(resources)));
}

} // namespace nkui
