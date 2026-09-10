#ifndef NATIVEKIT_UI_RENDER_PLAN_EXECUTOR_H
#define NATIVEKIT_UI_RENDER_PLAN_EXECUTOR_H

#include "compositor/render_plan.h"
#include "render/frame_resources.h"
#include "render/sokol_backend.h"

namespace nkui {

struct WindowTarget {
    ResourceId id{};
    int width = 0;
    int height = 0;
    uint32_t framebuffer = 0;
};

struct RenderExecutionError {
    uint32_t pass_index = 0;
    uint32_t command_index = 0;
    const char *message = nullptr;
};

bool execute_render_plan(SokolBackend &backend, const RenderPlan &plan,
                         const FrameResources &resources, const WindowTarget &window,
                         RenderExecutionError *error = nullptr);

} // namespace nkui

#endif
