#include "render_internal.hpp"

#include <array>
#include <functional>
#include <unordered_map>

namespace nkscene::render_internal {

namespace {

bool outside_plane(const Bounds &bounds, const std::array<float, 4> &plane) noexcept {
    const auto x = plane[0] >= 0.0f ? bounds.maximum[0] : bounds.minimum[0];
    const auto y = plane[1] >= 0.0f ? bounds.maximum[1] : bounds.minimum[1];
    const auto z = plane[2] >= 0.0f ? bounds.maximum[2] : bounds.minimum[2];
    return plane[0] * x + plane[1] * y + plane[2] * z + plane[3] < 0.0f;
}

} // namespace

bool culled_by_camera(const Bounds &bounds, const SceneCamera &camera) noexcept {
    if (!camera.enabled || !bounds.valid)
        return false;
    const auto &m = camera.view_projection;
    const std::array<std::array<float, 4>, 6> planes = {{
        {m[0] + m[3], m[4] + m[7], m[8] + m[11], m[12] + m[15]},
        {m[3] - m[0], m[7] - m[4], m[11] - m[8], m[15] - m[12]},
        {m[1] + m[3], m[5] + m[7], m[9] + m[11], m[13] + m[15]},
        {m[3] - m[1], m[7] - m[5], m[11] - m[9], m[15] - m[13]},
        {m[2] + m[3], m[6] + m[7], m[10] + m[11], m[14] + m[15]},
        {m[3] - m[2], m[7] - m[6], m[11] - m[10], m[15] - m[14]}}};
    for (const auto &plane : planes)
        if (outside_plane(bounds, plane))
            return true;
    return false;
}

bool culled_by_clip_planes(const Bounds &bounds,
                           std::span<const ClipPlane> planes) noexcept {
    if (!bounds.valid)
        return false;
    for (const auto &plane : planes) {
        if (!plane.enabled)
            continue;
        const auto x = plane.normal[0] >= 0.0f ? bounds.maximum[0] : bounds.minimum[0];
        const auto y = plane.normal[1] >= 0.0f ? bounds.maximum[1] : bounds.minimum[1];
        const auto z = plane.normal[2] >= 0.0f ? bounds.maximum[2] : bounds.minimum[2];
        if (plane.normal[0] * x + plane.normal[1] * y + plane.normal[2] * z +
                plane.distance < 0.0f)
            return true;
    }
    return false;
}

EffectiveState effective_state(const SceneSnapshot &snapshot, const SceneView &view) {
    EffectiveState result;
    const auto occurrences = snapshot.occurrences();
    result.in_view.reserve(occurrences.size());
    result.visible.reserve(occurrences.size());
    result.material.reserve(occurrences.size());

    std::unordered_map<OccurrenceId, std::vector<OccurrenceId>> children;
    children.reserve(occurrences.size());
    for (const auto &occurrence : occurrences) {
        result.in_view.emplace(occurrence.occurrence, false);
        result.material.emplace(occurrence.occurrence, occurrence.material);
        if (occurrence.parent.valid())
            children[occurrence.parent].push_back(occurrence.occurrence);
    }

    std::unordered_map<OccurrenceId, bool> visibility_overrides;
    visibility_overrides.reserve(view.visibility_overrides.size());
    for (const auto &override : view.visibility_overrides)
        visibility_overrides[override.occurrence] = override.visible;

    std::unordered_map<OccurrenceId, MaterialId> material_overrides;
    material_overrides.reserve(view.material_overrides.size() +
                               view.selection_material_overrides.size() +
                               view.hover_material_overrides.size());
    const auto apply_material_layer = [&material_overrides](
                                          const auto &overrides) {
        for (const auto &override : overrides)
            material_overrides[override.occurrence] = override.material;
    };
    apply_material_layer(view.material_overrides);
    apply_material_layer(view.selection_material_overrides);
    apply_material_layer(view.hover_material_overrides);
    for (const auto &[occurrence, material] : material_overrides)
        if (result.material.contains(occurrence))
            result.material[occurrence] = material;

    std::unordered_map<OccurrenceId, bool> visited;
    visited.reserve(occurrences.size());
    const auto walk = [&](OccurrenceId start, bool parent_visible, bool selected,
                          const auto &self) -> void {
        const auto *occurrence = snapshot.find(start);
        if (!occurrence || visited.contains(start))
            return;
        visited.emplace(start, true);
        const auto override_found = visibility_overrides.find(start);
        const bool local_visible = override_found != visibility_overrides.end()
                                       ? override_found->second
                                       : (view.include_invisible || occurrence->visible);
        const bool visible = parent_visible && local_visible;
        if (selected) {
            result.in_view[start] = true;
            result.visible[start] = visible;
        }
        const auto child_found = children.find(start);
        if (child_found == children.end())
            return;
        for (const auto child : child_found->second)
            self(child, visible, selected, self);
    };

    if (view.root.valid()) {
        if (snapshot.find(view.root))
            walk(view.root, true, true, walk);
    } else {
        for (const auto &occurrence : occurrences)
            if (!occurrence.parent.valid() || !snapshot.find(occurrence.parent))
                walk(occurrence.occurrence, true, true, walk);
        for (const auto &occurrence : occurrences)
            if (!visited.contains(occurrence.occurrence))
                walk(occurrence.occurrence, true, true, walk);
    }
    return result;
}

std::uint64_t view_signature(const SceneView &view) noexcept {
    std::uint64_t hash = 1469598103934665603ull;
    const auto add = [&hash](std::uint64_t value) {
        hash ^= value;
        hash *= 1099511628211ull;
    };
    add(view.root.value);
    add(view.include_invisible ? 1 : 0);
    add(view.visibility_overrides.size());
    for (const auto &override : view.visibility_overrides) {
        add(override.occurrence.value);
        add(override.visible ? 1 : 0);
    }
    add(view.material_overrides.size());
    for (const auto &override : view.material_overrides) {
        add(override.occurrence.value);
        add(override.material.value);
    }
    add(view.selection_material_overrides.size());
    for (const auto &override : view.selection_material_overrides) {
        add(override.occurrence.value);
        add(override.material.value);
    }
    add(view.hover_material_overrides.size());
    for (const auto &override : view.hover_material_overrides) {
        add(override.occurrence.value);
        add(override.material.value);
    }
    add(view.camera.enabled ? 1 : 0);
    for (const auto value : view.camera.view_projection)
        add(std::hash<float>{}(value));
    add(view.clip_planes.size());
    for (const auto &plane : view.clip_planes) {
        for (const auto value : plane.normal)
            add(std::hash<float>{}(value));
        add(std::hash<float>{}(plane.distance));
        add(plane.enabled ? 1 : 0);
    }
    return hash;
}

std::uint64_t culling_signature(const SceneView &view) noexcept {
    std::uint64_t hash = 1469598103934665603ull;
    const auto add = [&hash](std::uint64_t value) {
        hash ^= value;
        hash *= 1099511628211ull;
    };
    add(view.camera.enabled ? 1 : 0);
    for (const auto value : view.camera.view_projection)
        add(std::hash<float>{}(value));
    add(view.clip_planes.size());
    for (const auto &plane : view.clip_planes) {
        for (const auto value : plane.normal)
            add(std::hash<float>{}(value));
        add(std::hash<float>{}(plane.distance));
        add(plane.enabled ? 1 : 0);
    }
    return hash;
}

void build_items(RenderPlan &plan, const SceneSnapshot &snapshot, const SceneView &view) {
    const auto state = effective_state(snapshot, view);
    plan.items_.clear();
    plan.transforms_.clear();
    plan.visible_items_ = 0;
    plan.culled_items_ = 0;
    plan.view_projection_ = view.camera.enabled
        ? view.camera.view_projection
        : SceneCamera{}.view_projection;
    plan.items_.reserve(snapshot.occurrences().size());
    plan.transforms_.reserve(snapshot.occurrences().size());
    for (const auto &occurrence : snapshot.occurrences()) {
        if (!occurrence.geometry.valid() || !occurrence.material.valid() ||
            !snapshot.find_geometry(occurrence.geometry) ||
            !snapshot.find_material(occurrence.material) ||
            !state.in_view.at(occurrence.occurrence))
            continue;
        const auto transform_index = static_cast<std::uint32_t>(plan.transforms_.size());
        plan.transforms_.push_back(occurrence.world_transform);
        RenderItem item;
        item.occurrence = occurrence.occurrence;
        item.geometry = occurrence.geometry;
        item.material = state.material.at(occurrence.occurrence);
        item.pickId = static_cast<std::uint32_t>(plan.items_.size() + 1);
        item.transformIndex = transform_index;
        item.flags = RenderFlags::Opaque;
        if (!state.visible.at(occurrence.occurrence))
            item.flags |= RenderFlags::Hidden;
        if (culled_by_camera(occurrence.bounds, view.camera) ||
            culled_by_clip_planes(occurrence.bounds, view.clip_planes)) {
            item.flags |= RenderFlags::Culled;
            ++plan.culled_items_;
        } else if (state.visible.at(occurrence.occurrence)) {
            ++plan.visible_items_;
        }
        plan.items_.push_back(item);
    }
}

std::unordered_map<OccurrenceId, std::size_t> item_indices(const RenderPlan &plan) {
    std::unordered_map<OccurrenceId, std::size_t> result;
    result.reserve(plan.items_.size());
    for (std::size_t index = 0; index < plan.items_.size(); ++index)
        result.emplace(plan.items_[index].occurrence, index);
    return result;
}

} // namespace nkscene::render_internal
