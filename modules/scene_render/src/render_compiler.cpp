#include "render_internal.hpp"

#include <algorithm>
#include <unordered_set>

namespace nkscene {

namespace {

bool culled(const SnapshotOccurrence &occurrence, const SceneView &view) noexcept {
    return render_internal::culled_by_camera(occurrence.bounds, view.camera) ||
        render_internal::culled_by_clip_planes(occurrence.bounds, view.clip_planes);
}

bool has_domain_in(const ChangeSet &changes, ChangeDomain domain) noexcept {
    return std::any_of(changes.changes.begin(), changes.changes.end(),
                       [domain](const SceneChange &change) {
                           return has_domain(change.domains, domain);
                       });
}

} // namespace

RenderPlan compile(const SceneSnapshot &snapshot, const SceneView &view) {
    RenderPlan plan;
    render_internal::build_items(plan, snapshot, view);
    render_internal::rebuild_batches(plan);
    plan.clip_plane_count_ = 0;
    for (const auto &plane : view.clip_planes) {
        if (!plane.enabled || plan.clip_plane_count_ == RenderPlan::max_clip_planes)
            continue;
        plan.clip_planes_[plan.clip_plane_count_++] = {plane.normal[0], plane.normal[1],
                                                       plane.normal[2], plane.distance};
    }
    plan.source_revision_ = snapshot.revision();
    plan.view_signature_ = render_internal::view_signature(view);
    plan.view_root_ = view.root;
    plan.culling_signature_ = render_internal::culling_signature(view);
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
    const auto next_view_signature = render_internal::view_signature(view);
    const auto next_culling_signature = render_internal::culling_signature(view);
    const bool view_changed = plan.view_signature_ != next_view_signature;
    const bool culling_changed = plan.culling_signature_ != next_culling_signature;
    const bool topology_changed = has_domain_in(changes, ChangeDomain::Created) ||
        has_domain_in(changes, ChangeDomain::Destroyed);
    const bool effective_state_dirty = view_changed ||
        has_domain_in(changes, ChangeDomain::Hierarchy) ||
        has_domain_in(changes, ChangeDomain::Visibility) ||
        has_domain_in(changes, ChangeDomain::Material);
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
        plan.view_root_ != view.root ||
        invalidated_items != 0 ||
        (view.root.valid() && std::any_of(
             changes.changes.begin(), changes.changes.end(), [](const SceneChange &change) {
                 return has_domain(change.domains, ChangeDomain::Hierarchy);
             }))) {
        plan = compile(snapshot, view);
        result.plan_rebuilt = true;
        result.invalidated_items = invalidated_items;
        result.visible_items = plan.visible_items_;
        result.culled_items = plan.culled_items_;
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
        if (has_domain(change.domains, ChangeDomain::Geometry)) {
            item.geometry = snapshot_occurrence->geometry;
            batches_dirty = true;
            result.geometry_rebuilt = true;
        }
    }
    const bool world_transforms_changed = std::any_of(
        changes.changes.begin(), changes.changes.end(), [](const SceneChange &change) {
            return has_domain(change.domains, ChangeDomain::Transform) ||
                has_domain(change.domains, ChangeDomain::Hierarchy);
        });
    if (world_transforms_changed) {
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
            if (visible != was_visible) {
                if (visible)
                    item.flags = static_cast<RenderFlags>(
                        static_cast<std::uint32_t>(item.flags) &
                        ~static_cast<std::uint32_t>(RenderFlags::Hidden));
                else
                    item.flags |= RenderFlags::Hidden;
                ++result.patched_visibility;
            }
            const auto material = effective.material.at(item.occurrence);
            if (material != item.material) {
                item.material = material;
                ++result.patched_materials;
                batches_dirty = true;
            }
        }
    }

    const bool scene_culling_dirty = has_domain_in(changes, ChangeDomain::Transform) ||
        has_domain_in(changes, ChangeDomain::Hierarchy) ||
        has_domain_in(changes, ChangeDomain::Geometry) ||
        has_domain_in(changes, ChangeDomain::Bounds);
    if (culling_changed || scene_culling_dirty) {
        for (auto &item : plan.items_) {
            const auto *occurrence = snapshot.find(item.occurrence);
            if (!occurrence)
                continue;
            const auto item_culled = culled(*occurrence, view);
            const auto was_culled = has_render_flag(item.flags, RenderFlags::Culled);
            if (item_culled == was_culled)
                continue;
            if (item_culled)
                item.flags |= RenderFlags::Culled;
            else
                item.flags = static_cast<RenderFlags>(
                    static_cast<std::uint32_t>(item.flags) &
                    ~static_cast<std::uint32_t>(RenderFlags::Culled));
            ++result.patched_culling;
        }
    }
    if (batches_dirty) {
        render_internal::rebuild_batches(plan);
        result.rebuilt_batches = plan.batches_.size();
    }
    plan.source_revision_ = snapshot.revision();
    plan.view_signature_ = next_view_signature;
    plan.view_root_ = view.root;
    plan.culling_signature_ = next_culling_signature;
    plan.view_projection_ = view.camera.enabled
        ? view.camera.view_projection
        : SceneCamera{}.view_projection;
    plan.clip_plane_count_ = 0;
    for (const auto &plane : view.clip_planes) {
        if (!plane.enabled || plan.clip_plane_count_ == RenderPlan::max_clip_planes)
            continue;
        plan.clip_planes_[plan.clip_plane_count_++] = {plane.normal[0], plane.normal[1],
                                                       plane.normal[2], plane.distance};
    }
    plan.visible_items_ = 0;
    plan.culled_items_ = 0;
    for (const auto &item : plan.items_) {
        if (has_render_flag(item.flags, RenderFlags::Culled))
            ++plan.culled_items_;
        else if (!has_render_flag(item.flags, RenderFlags::Hidden))
            ++plan.visible_items_;
    }
    result.visible_items = plan.visible_items_;
    result.culled_items = plan.culled_items_;
    return result;
}

RenderUpdate refresh(RenderPlan &plan, const SceneSnapshot &snapshot, const SceneView &view) {
    RenderUpdate result;
    if (plan.source_revision() == snapshot.revision() &&
        plan.view_signature_ == render_internal::view_signature(view)) {
        result.visible_items = plan.visible_items_;
        result.culled_items = plan.culled_items_;
        return result;
    }

    if (plan.source_revision() != snapshot.revision()) {
        plan = compile(snapshot, view);
        result.plan_rebuilt = true;
        result.visible_items = plan.visible_items_;
        result.culled_items = plan.culled_items_;
        return result;
    }

    return update(plan, snapshot, ChangeSet{}, view);
}

} // namespace nkscene
