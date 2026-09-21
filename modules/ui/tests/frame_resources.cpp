#include "render/frame_resources.h"
#include "render/sealed_render_plan.h"

#include "prepare/nanovg_recorder.h"
#include "nanovg.h"

using namespace nkui;

int main() {
    NanoVGRecorder recorder;
    NVGcontext *vg = recorder.context();
    nvgBeginFrame(vg, 100.0f, 100.0f, 1.0f);
    nvgBeginPath(vg);
    nvgRect(vg, 0.0f, 0.0f, 10.0f, 10.0f);
    nvgFill(vg);
    nvgEndFrame(vg);
    const auto path = make_resource_id(ResourceKind::Path, 2, 3);
    const auto text = make_resource_id(ResourceKind::TextLayout, 2, 4);
    const auto graphics_image = make_resource_id(ResourceKind::RenderTarget, 2, 6);
    PreparedGlyphs glyphs;
    FrameResources resources;
    if (!resources.bind_path(path, recorder.data(), 0, 42) ||
        resources.bind_path(text, recorder.data(), 0) ||
        resources.bind_path(path, recorder.data(), 1) || !resources.bind_text(text, glyphs) ||
        resources.bind_text(path, glyphs) ||
        !resources.bind_graphics_image(graphics_image, nk_graphics_image{123}, 9))
        return 1;
    if (!resources.path(path) || resources.path(path)->operation_index != 0 ||
        resources.text(text) != &glyphs || !resources.graphics_image(graphics_image) ||
        resources.content_generation(path) != 42 ||
        resources.content_generation(graphics_image) != 9)
        return 2;
    resources.reset();
    if (resources.path(path) || resources.text(text) || resources.graphics_image(graphics_image))
        return 3;

    RenderPlan plan;
    plan.passes.push_back({make_resource_id(ResourceKind::RenderTarget, 2, 7), {}, false, {}});
    OwnedFrameResources owned;
    RenderPlanSealError error{};
    if (!SealedRenderPlan::seal(std::move(plan), std::move(owned), &error) || error.message)
        return 4;
    return 0;
}
