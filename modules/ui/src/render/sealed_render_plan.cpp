#include "render/sealed_render_plan.h"

#include <unordered_set>
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
    std::unordered_set<uint32_t> internal_targets;
    internal_targets.reserve(plan.passes.size());
    for (const auto &pass : plan.passes)
        internal_targets.insert(pass.target.value);
    for (const auto &dependency : plan.dependencies) {
        if (internal_targets.count(dependency.producer.value))
            continue;
        if (resources.graphics_image(dependency.producer))
            continue;
        fail(error, "sealed render plans require retained graphics images for external surfaces");
        return nullptr;
    }
    if (error)
        error->message = nullptr;
    return std::shared_ptr<const SealedRenderPlan>(
        new SealedRenderPlan(std::move(plan), std::move(resources)));
}

} // namespace nkui
