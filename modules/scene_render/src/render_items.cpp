#include "render_internal.hpp"

#include <unordered_map>

namespace nkscene::render_internal {

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
    material_overrides.reserve(view.material_overrides.size());
    for (const auto &override : view.material_overrides)
        material_overrides[override.occurrence] = override.material;
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
    return hash;
}

void build_items(RenderPlan &plan, const SceneSnapshot &snapshot, const SceneView &view) {
    const auto state = effective_state(snapshot, view);
    plan.items_.clear();
    plan.transforms_.clear();
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
