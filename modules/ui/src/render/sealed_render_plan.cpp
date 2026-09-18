#include "render/sealed_render_plan.h"

#include <new>
#include <utility>

namespace nkui {
namespace {

void fail(RenderPlanSealError *error, const char *message) {
    if (error)
        error->message = message;
}

} // namespace

std::shared_ptr<const SealedRenderPlan>
SealedRenderPlan::seal(RenderPlan plan, FrameResources resources, RenderPlanSealError *error) {
    if (resources.has_surface_producers()) {
        fail(error, "a sealed render plan cannot reference a live surface producer");
        return {};
    }
    if (resources.has_borrowed_resources()) {
        fail(error, "a sealed render plan must own every path, image, text, and graphics image it "
                    "references");
        return {};
    }
    try {
        return std::shared_ptr<const SealedRenderPlan>(
            new SealedRenderPlan(std::move(plan), std::move(resources)));
    } catch (const std::bad_alloc &) {
        fail(error, "out of memory while sealing a render plan");
        return {};
    } catch (...) {
        fail(error, "unexpected failure while sealing a render plan");
        return {};
    }
}

} // namespace nkui
