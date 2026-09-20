#pragma once

#include "nativekit_scene_render.h"

#include <cstddef>
#include <unordered_map>

namespace nkscene::render_internal {

struct EffectiveState {
    std::unordered_map<OccurrenceId, bool> in_view;
    std::unordered_map<OccurrenceId, bool> visible;
    std::unordered_map<OccurrenceId, MaterialId> material;
};

void rebuild_batches(RenderPlan &plan);
void build_items(RenderPlan &plan, const SceneSnapshot &snapshot, const SceneView &view);
EffectiveState effective_state(const SceneSnapshot &snapshot, const SceneView &view);
std::uint64_t view_signature(const SceneView &view) noexcept;
std::unordered_map<OccurrenceId, std::size_t> item_indices(const RenderPlan &plan);

} // namespace nkscene::render_internal
