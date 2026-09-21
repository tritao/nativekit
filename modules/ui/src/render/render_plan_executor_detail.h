#ifndef NATIVEKIT_UI_RENDER_PLAN_EXECUTOR_DETAIL_H
#define NATIVEKIT_UI_RENDER_PLAN_EXECUTOR_DETAIL_H

#include "render_plan_executor.h"

namespace nkui::detail {

/**
 * Executes borrowed builder data for compiler/unit-test coverage only.
 *
 * Production submission must use the sealed overload from
 * render_plan_executor.h. This escape hatch remains private to the UI source
 * tree while the low-level renderer tests exercise individual plan features.
 */
bool execute_unsealed_render_plan(UiRenderer &renderer, const RenderPlan &plan,
                                  const FrameResources &resources, const WindowTarget &window,
                                  RenderExecutionError *error = nullptr);

} // namespace nkui::detail

#endif
