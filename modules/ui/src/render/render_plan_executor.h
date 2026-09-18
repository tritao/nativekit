#ifndef NATIVEKIT_UI_RENDER_PLAN_EXECUTOR_H
#define NATIVEKIT_UI_RENDER_PLAN_EXECUTOR_H

#include "nativekit_graphics.h"
#include "compositor/render_plan.h"
#include "render/frame_resources.h"
#include "render/sealed_render_plan.h"
#include "render/ui_renderer.h"

namespace nkui {

struct WindowTarget {
    ResourceId id{};
    nk_surface_frame_target frame_target{};
};

struct RenderExecutionError {
    uint32_t pass_index = 0;
    uint32_t command_index = 0;
    const char *message = nullptr;
};

bool execute_render_plan(UiRenderer &renderer, const RenderPlan &plan,
                         const FrameResources &resources, const WindowTarget &window,
                         RenderExecutionError *error = nullptr);

/** Executes a sealed plan; the plan and its resources stay alive for the call. */
bool execute_render_plan(UiRenderer &renderer, const SealedRenderPlan &sealed,
                         const WindowTarget &window, RenderExecutionError *error = nullptr);

} // namespace nkui

#endif
