#include "render_internal.hpp"

#include <algorithm>
#include <unordered_set>

namespace nkscene {

RenderPlan compile(const SceneSnapshot &snapshot, const SceneView &view) {
    RenderPlan plan;
    render_internal::build_items(plan, snapshot, view);
    render_internal::rebuild_batches(plan);
    plan.source_revision_ = snapshot.revision();
    plan.compile_count_ = 1;
    return plan;
}

RenderUpdate update(RenderPlan &plan, const SceneSnapshot &snapshot,
                    const ChangeSet &changes, const SceneView &view) {
    RenderUpdate result;
    const bool topology_changed = std::any_of(
        changes.changes.begin(), changes.changes.end(), [](const SceneChange &change) {
            return has_domain(change.domains, ChangeDomain::Created) ||
                has_domain(change.domains, ChangeDomain::Destroyed);
        });
    if (topology_changed || plan.source_revision() > snapshot.revision()) {
        plan = compile(snapshot, view);
        result.plan_rebuilt = true;
        return result;
    }

    const auto indices = render_internal::item_indices(plan);
    bool batches_dirty = false;
    for (const auto &change : changes.changes) {
        const auto item_found = indices.find(change.occurrence);
        const auto *snapshot_occurrence = snapshot.find(change.occurrence);
        if (item_found == indices.end() || !snapshot_occurrence)
            continue;
        auto &item = plan.items_[item_found->second];
        if (has_domain(change.domains, ChangeDomain::Transform) ||
            has_domain(change.domains, ChangeDomain::Hierarchy)) {
            plan.transforms_[item.transformIndex] = snapshot_occurrence->world_transform;
            ++result.patched_instances;
        }
        if (has_domain(change.domains, ChangeDomain::Visibility)) {
            if (snapshot_occurrence->visible)
                item.flags = static_cast<RenderFlags>(
                    static_cast<std::uint32_t>(item.flags) &
                    ~static_cast<std::uint32_t>(RenderFlags::Hidden));
            else
                item.flags |= RenderFlags::Hidden;
            ++result.patched_visibility;
        }
        if (has_domain(change.domains, ChangeDomain::Material)) {
            item.material = snapshot_occurrence->material;
            ++result.patched_materials;
            batches_dirty = true;
        }
        if (has_domain(change.domains, ChangeDomain::Geometry)) {
            item.geometry = snapshot_occurrence->geometry;
            batches_dirty = true;
            result.geometry_rebuilt = true;
        }
    }
    const bool hierarchy_changed = std::any_of(
        changes.changes.begin(), changes.changes.end(), [](const SceneChange &change) {
            return has_domain(change.domains, ChangeDomain::Hierarchy);
        });
    if (hierarchy_changed) {
        for (auto &item : plan.items_) {
            if (const auto *occurrence = snapshot.find(item.occurrence)) {
                if (occurrence->world_transform.revision !=
                    plan.transforms_[item.transformIndex].revision) {
                    plan.transforms_[item.transformIndex] = occurrence->world_transform;
                    ++result.patched_instances;
                }
            }
        }
    }
    if (batches_dirty) {
        render_internal::rebuild_batches(plan);
        result.rebuilt_batches = plan.batches_.size();
    }
    plan.source_revision_ = snapshot.revision();
    return result;
}

} // namespace nkscene
