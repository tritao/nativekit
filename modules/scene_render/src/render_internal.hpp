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
std::size_t move_item_batch(RenderPlan &plan, std::size_t item_index, GeometryId geometry,
                            MaterialId material);
void build_items(RenderPlan &plan, const SceneSnapshot &snapshot, const SceneView &view);
void update_ancestor_index(RenderPlan &plan, const SceneSnapshot &snapshot,
                           const ChangeSet &changes);
bool culled_by_camera(const Bounds &bounds, const SceneCamera &camera) noexcept;
SceneCamera camera_for_snapshot(const SceneSnapshot &snapshot, const SceneView &view) noexcept;
bool culled_by_clip_planes(const Bounds &bounds, std::span<const ClipPlane> planes) noexcept;
void append_culling_planes(const SceneView &view, std::vector<std::array<float, 4>> &planes);
EffectiveState effective_state(const SceneSnapshot &snapshot, const SceneView &view);
std::uint64_t presentation_signature(const SceneView &view) noexcept;
std::uint64_t view_signature(const SceneView &view) noexcept;
std::uint64_t culling_signature(const SceneView &view) noexcept;

} // namespace nkscene::render_internal
