#include "render_internal.hpp"

#include <algorithm>
#include <unordered_set>

namespace nkscene {

RenderPlan compile(const SceneSnapshot &snapshot, const SceneView &view) {
    RenderPlan plan;
    render_internal::build_items(plan, snapshot, view);
    render_internal::rebuild_batches(plan);
    plan.source_revision_ = snapshot.revision();
    plan.view_signature_ = render_internal::view_signature(view);
    plan.geometry_revisions_.reserve(snapshot.geometries().size());
    for (const auto &resource : snapshot.geometries())
        plan.geometry_revisions_.emplace(resource.id, resource.revision);
    plan.material_revisions_.reserve(snapshot.materials().size());
    for (const auto &resource : snapshot.materials())
        plan.material_revisions_.emplace(resource.id, resource.revision);
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
    const bool effective_state_dirty = std::any_of(
        changes.changes.begin(), changes.changes.end(), [](const SceneChange &change) {
            return has_domain(change.domains, ChangeDomain::Hierarchy) ||
                has_domain(change.domains, ChangeDomain::Visibility) ||
                has_domain(change.domains, ChangeDomain::Material);
        });
    const auto effective = effective_state_dirty
        ? render_internal::effective_state(snapshot, view)
        : render_internal::EffectiveState{};
    std::unordered_set<GeometryId> changed_geometry_resources;
    std::unordered_set<MaterialId> changed_material_resources;
    std::size_t invalidated_items = 0;
    for (const auto &item : plan.items_) {
        const auto *occurrence = snapshot.find(item.occurrence);
        const auto *geometry = snapshot.find_geometry(item.geometry);
        const auto *material = snapshot.find_material(item.material);
        const auto desired_geometry = occurrence ? occurrence->geometry : invalid_geometry;
        const auto desired_material = occurrence && effective_state_dirty
            ? effective.material.at(item.occurrence)
            : item.material;
        if (!occurrence || !geometry || !material ||
            (desired_geometry != item.geometry &&
             !snapshot.find_geometry(desired_geometry)) ||
            !snapshot.find_material(desired_material)) {
            ++invalidated_items;
            continue;
        }
        const auto geometry_revision = plan.geometry_revisions_.find(item.geometry);
        if (geometry_revision == plan.geometry_revisions_.end() ||
            geometry_revision->second != geometry->revision)
            changed_geometry_resources.insert(item.geometry);
        const auto material_revision = plan.material_revisions_.find(item.material);
        if (material_revision == plan.material_revisions_.end() ||
            material_revision->second != material->revision)
            changed_material_resources.insert(item.material);
    }
    if (topology_changed || plan.source_revision() > snapshot.revision() ||
        plan.view_signature_ != render_internal::view_signature(view) ||
        invalidated_items != 0 ||
        (view.root.valid() && std::any_of(
             changes.changes.begin(), changes.changes.end(), [](const SceneChange &change) {
                 return has_domain(change.domains, ChangeDomain::Hierarchy);
             }))) {
        plan = compile(snapshot, view);
        result.plan_rebuilt = true;
        result.invalidated_items = invalidated_items;
        return result;
    }

    result.updated_geometry_resources = changed_geometry_resources.size();
    result.updated_material_resources = changed_material_resources.size();
    plan.geometry_revisions_.clear();
    plan.geometry_revisions_.reserve(snapshot.geometries().size());
    for (const auto &resource : snapshot.geometries())
        plan.geometry_revisions_.emplace(resource.id, resource.revision);
    plan.material_revisions_.clear();
    plan.material_revisions_.reserve(snapshot.materials().size());
    for (const auto &resource : snapshot.materials())
        plan.material_revisions_.emplace(resource.id, resource.revision);

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
            if (effective.visible.at(change.occurrence))
                item.flags = static_cast<RenderFlags>(
                    static_cast<std::uint32_t>(item.flags) &
                    ~static_cast<std::uint32_t>(RenderFlags::Hidden));
            else
                item.flags |= RenderFlags::Hidden;
            ++result.patched_visibility;
        }
        if (has_domain(change.domains, ChangeDomain::Material)) {
            item.material = effective.material.at(change.occurrence);
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
    if (effective_state_dirty) {
        for (auto &item : plan.items_) {
            const auto visible = effective.visible.at(item.occurrence);
            const auto was_visible = !has_render_flag(item.flags, RenderFlags::Hidden);
            if (visible == was_visible)
                continue;
            if (visible)
                item.flags = static_cast<RenderFlags>(
                    static_cast<std::uint32_t>(item.flags) &
                    ~static_cast<std::uint32_t>(RenderFlags::Hidden));
            else
                item.flags |= RenderFlags::Hidden;
            ++result.patched_visibility;
        }
    }
    if (batches_dirty) {
        render_internal::rebuild_batches(plan);
        result.rebuilt_batches = plan.batches_.size();
    }
    plan.source_revision_ = snapshot.revision();
    plan.view_signature_ = render_internal::view_signature(view);
    return result;
}

} // namespace nkscene
