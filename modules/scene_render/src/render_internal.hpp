#pragma once

#include "nativekit_scene_render.h"

#include <cstddef>
#include <unordered_map>

namespace nkscene::render_internal {

void rebuild_batches(RenderPlan &plan);
void build_items(RenderPlan &plan, const SceneSnapshot &snapshot, const SceneView &view);
std::unordered_map<OccurrenceId, std::size_t> item_indices(const RenderPlan &plan);

} // namespace nkscene::render_internal
