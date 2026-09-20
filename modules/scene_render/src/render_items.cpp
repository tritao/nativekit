#include "render_internal.hpp"

namespace nkscene::render_internal {

void build_items(RenderPlan &plan, const SceneSnapshot &snapshot, const SceneView &view) {
    plan.items_.clear();
    plan.transforms_.clear();
    plan.items_.reserve(snapshot.occurrences().size());
    plan.transforms_.reserve(snapshot.occurrences().size());
    for (const auto &occurrence : snapshot.occurrences()) {
        if (!occurrence.geometry.valid())
            continue;
        const auto transform_index = static_cast<std::uint32_t>(plan.transforms_.size());
        plan.transforms_.push_back(occurrence.world_transform);
        RenderItem item;
        item.occurrence = occurrence.occurrence;
        item.geometry = occurrence.geometry;
        item.material = occurrence.material;
        item.pickId = static_cast<std::uint32_t>(plan.items_.size() + 1);
        item.transformIndex = transform_index;
        item.flags = RenderFlags::Opaque;
        if (!occurrence.visible)
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
