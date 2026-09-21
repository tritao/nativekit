#include "render_internal.hpp"

#include <algorithm>
#include <unordered_set>

namespace nkscene {

namespace {

bool culled(const SnapshotOccurrence &occurrence, const SceneCamera &camera,
            const SceneView &view) noexcept {
    return render_internal::culled_by_camera(occurrence.bounds, camera) ||
           render_internal::culled_by_clip_planes(occurrence.bounds, view.clip_planes);
}

bool has_domain_in(const ChangeSet &changes, ChangeDomain domain) noexcept {
    return std::any_of(
        changes.changes.begin(), changes.changes.end(),
        [domain](const SceneChange &change) { return has_domain(change.domains, domain); });
}

constexpr std::size_t invalid_item_index = static_cast<std::size_t>(-1);

} // namespace

void render_internal::capture_view_policy(RenderPlan &plan, const SceneView &view) {
    plan.view_override_occurrences_.clear();
    plan.view_override_occurrences_.reserve(view.visibility_overrides.size() +
                                            view.material_overrides.size() +
                                            view.selection_material_overrides.size() +
                                            view.hover_material_overrides.size());
    for (const auto &override : view.visibility_overrides)
        plan.view_override_occurrences_.push_back(override.occurrence);
    for (const auto &override : view.material_overrides)
        plan.view_override_occurrences_.push_back(override.occurrence);
    for (const auto &override : view.selection_material_overrides)
        plan.view_override_occurrences_.push_back(override.occurrence);
    for (const auto &override : view.hover_material_overrides)
        plan.view_override_occurrences_.push_back(override.occurrence);
    plan.view_global_policy_ = view.include_invisible || !view.filter.isolated_sources.empty() ||
                               !view.filter.source_visibility_overrides.empty() ||
                               !view.filter.source_material_overrides.empty() ||
                               !view.filter.isolated_occurrences.empty();
}

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
    plan.presentation_signature_ = render_internal::presentation_signature(view);
    render_internal::capture_view_policy(plan, view);
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

RenderUpdate update(RenderPlan &plan, const SceneSnapshot &snapshot, const ChangeSet &changes,
                    const SceneView &view) {
    RenderUpdate result;
    const auto next_view_signature = render_internal::view_signature(view);
    const auto next_presentation_signature = render_internal::presentation_signature(view);
    const auto next_culling_signature = render_internal::culling_signature(view);
    const bool presentation_changed = plan.presentation_signature_ != next_presentation_signature;
    const bool culling_changed = plan.culling_signature_ != next_culling_signature;
    const bool scene_effective_change =
        has_domain_in(changes, ChangeDomain::Hierarchy) ||
        has_domain_in(changes, ChangeDomain::Visibility) ||
        has_domain_in(changes, ChangeDomain::Material) ||
        has_domain_in(changes, ChangeDomain::Source);
    const bool scene_hierarchy_or_source_change =
        has_domain_in(changes, ChangeDomain::Hierarchy) ||
        has_domain_in(changes, ChangeDomain::Source);
    const bool current_global_policy =
        view.include_invisible || !view.filter.isolated_sources.empty() ||
        !view.filter.source_visibility_overrides.empty() ||
        !view.filter.source_material_overrides.empty() ||
        !view.filter.isolated_occurrences.empty();
    const bool local_policy = !plan.view_global_policy_ && !current_global_policy;
    const bool local_scene_effective_change =
        local_policy && !scene_hierarchy_or_source_change &&
        (has_domain_in(changes, ChangeDomain::Visibility) ||
         has_domain_in(changes, ChangeDomain::Material));
    const bool local_presentation_change =
        local_policy && presentation_changed && !scene_hierarchy_or_source_change;
    const bool local_effective_change = local_scene_effective_change || local_presentation_change;
    const bool topology_changed = has_domain_in(changes, ChangeDomain::Created) ||
                                  has_domain_in(changes, ChangeDomain::Destroyed);
    const bool effective_state_dirty = !local_effective_change &&
                                       (presentation_changed || scene_effective_change);
    const auto effective = effective_state_dirty ? render_internal::effective_state(snapshot, view)
                                                 : render_internal::EffectiveState{};
    std::unordered_set<GeometryId> changed_geometry_resources;
    std::unordered_set<MaterialId> changed_material_resources;
    std::size_t invalidated_items = 0;
    for (const auto &resource : snapshot.geometries()) {
        const auto found = plan.geometry_revisions_.find(resource.id);
        if (found != plan.geometry_revisions_.end() && found->second != resource.revision &&
            plan.items_by_geometry_.contains(resource.id))
            changed_geometry_resources.insert(resource.id);
    }
    for (const auto &resource : snapshot.materials()) {
        const auto found = plan.material_revisions_.find(resource.id);
        if (found != plan.material_revisions_.end() && found->second != resource.revision &&
            plan.items_by_material_.contains(resource.id))
            changed_material_resources.insert(resource.id);
    }
    for (const auto &[geometry, unused] : plan.geometry_revisions_)
        if (!snapshot.find_geometry(geometry) && plan.items_by_geometry_.contains(geometry))
            invalidated_items += plan.items_by_geometry_.at(geometry).size();
    for (const auto &[material, unused] : plan.material_revisions_)
        if (!snapshot.find_material(material) && plan.items_by_material_.contains(material))
            invalidated_items += plan.items_by_material_.at(material).size();
    for (const auto &change : changes.changes) {
        const auto item_index = plan.item_index(change.occurrence);
        const auto *occurrence = snapshot.find(change.occurrence);
        if (item_index == invalid_item_index || !occurrence)
            continue;
        if (has_domain(change.domains, ChangeDomain::Geometry) &&
            !snapshot.find_geometry(occurrence->geometry))
            ++invalidated_items;
        if (has_domain(change.domains, ChangeDomain::Material) &&
            !snapshot.find_material(occurrence->material))
            ++invalidated_items;
    }
    if (topology_changed || plan.source_revision() > snapshot.revision() ||
        plan.view_root_ != view.root || invalidated_items != 0 ||
        (view.root.valid() &&
         std::any_of(changes.changes.begin(), changes.changes.end(), [](const SceneChange &change) {
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

    const auto remove_index = [](auto &index, const auto key, std::size_t item_index) {
        const auto found = index.find(key);
        if (found == index.end())
            return;
        auto &items = found->second;
        items.erase(std::remove(items.begin(), items.end(), item_index), items.end());
        if (items.empty())
            index.erase(found);
    };
    const auto adjust_counts = [&plan](RenderFlags before, RenderFlags after) {
        const auto before_culled = has_render_flag(before, RenderFlags::Culled);
        const auto after_culled = has_render_flag(after, RenderFlags::Culled);
        const auto before_visible = !before_culled && !has_render_flag(before, RenderFlags::Hidden);
        const auto after_visible = !after_culled && !has_render_flag(after, RenderFlags::Hidden);
        if (before_culled != after_culled) {
            if (after_culled)
                ++plan.culled_items_;
            else
                --plan.culled_items_;
        }
        if (before_visible != after_visible) {
            if (after_visible)
                ++plan.visible_items_;
            else
                --plan.visible_items_;
        }
    };

    for (const auto &change : changes.changes) {
        const auto item_index = plan.item_index(change.occurrence);
        const auto *snapshot_occurrence = snapshot.find(change.occurrence);
        if (item_index == invalid_item_index || !snapshot_occurrence)
            continue;
        auto &item = plan.items_[item_index];
        if (has_domain(change.domains, ChangeDomain::Geometry) &&
            item.geometry != snapshot_occurrence->geometry) {
            remove_index(plan.items_by_geometry_, item.geometry, item_index);
            item.geometry = snapshot_occurrence->geometry;
            plan.items_by_geometry_[item.geometry].push_back(item_index);
            result.rebuilt_batches +=
                render_internal::move_item_batch(plan, item_index, item.geometry, item.material);
            result.geometry_rebuilt = true;
        }
        if (has_domain(change.domains, ChangeDomain::Source) &&
            plan.item_sources_[item_index] != snapshot_occurrence->source) {
            remove_index(plan.items_by_source_, plan.item_sources_[item_index], item_index);
            plan.item_sources_[item_index] = snapshot_occurrence->source;
            plan.items_by_source_[snapshot_occurrence->source].push_back(item_index);
        }
    }

    for (const auto occurrence_id : changes.world_transform_occurrences) {
        const auto item_index = plan.item_index(occurrence_id);
        const auto *occurrence = snapshot.find(occurrence_id);
        if (item_index == invalid_item_index || !occurrence)
            continue;
        auto &item = plan.items_[item_index];
        if (plan.transforms_[item.transformIndex].revision != occurrence->world_transform.revision) {
            plan.transforms_[item.transformIndex] = occurrence->world_transform;
            ++result.patched_instances;
        }
    }

    if (local_effective_change) {
        std::unordered_set<OccurrenceId> local_targets;
        for (const auto occurrence : plan.view_override_occurrences_)
            local_targets.insert(occurrence);
        if (local_scene_effective_change) {
            for (const auto occurrence : changes.effective_state_occurrences)
                local_targets.insert(occurrence);
            for (const auto &change : changes.changes)
                local_targets.insert(change.occurrence);
        }
        const auto add_current_target = [&local_targets](OccurrenceId occurrence) {
            local_targets.insert(occurrence);
        };
        for (const auto &override : view.visibility_overrides)
            add_current_target(override.occurrence);
        for (const auto &override : view.material_overrides)
            add_current_target(override.occurrence);
        for (const auto &override : view.selection_material_overrides)
            add_current_target(override.occurrence);
        for (const auto &override : view.hover_material_overrides)
            add_current_target(override.occurrence);

        std::unordered_map<OccurrenceId, bool> visibility_overrides;
        visibility_overrides.reserve(view.visibility_overrides.size());
        for (const auto &override : view.visibility_overrides)
            visibility_overrides[override.occurrence] = override.visible;
        std::unordered_map<OccurrenceId, MaterialId> material_overrides;
        material_overrides.reserve(view.material_overrides.size() +
                                   view.selection_material_overrides.size() +
                                   view.hover_material_overrides.size());
        for (const auto &override : view.material_overrides)
            material_overrides[override.occurrence] = override.material;
        for (const auto &override : view.selection_material_overrides)
            material_overrides[override.occurrence] = override.material;
        for (const auto &override : view.hover_material_overrides)
            material_overrides[override.occurrence] = override.material;

        std::vector<std::size_t> local_items;
        std::unordered_set<std::size_t> local_item_set;
        const auto add_subtree = [&](OccurrenceId occurrence, const auto &self) -> void {
            const auto item_index = plan.item_index(occurrence);
            if (item_index != invalid_item_index && local_item_set.insert(item_index).second)
                local_items.push_back(item_index);
            const auto found = plan.items_by_parent_.find(occurrence);
            if (found == plan.items_by_parent_.end())
                return;
            for (const auto child_index : found->second)
                self(plan.items_[child_index].occurrence, self);
        };
        for (const auto occurrence : local_targets)
            add_subtree(occurrence, add_subtree);

        std::unordered_map<OccurrenceId, bool> desired_visibility;
        const auto visible = [&](OccurrenceId occurrence, const auto &self) -> bool {
            const auto cached = desired_visibility.find(occurrence);
            if (cached != desired_visibility.end())
                return cached->second;
            const auto *value = snapshot.find(occurrence);
            if (!value)
                return false;
            bool result = value->visible;
            const auto override_found = visibility_overrides.find(occurrence);
            if (override_found != visibility_overrides.end())
                result = override_found->second;
            if (value->parent.valid())
                result = result && self(value->parent, self);
            desired_visibility.emplace(occurrence, result);
            return result;
        };

        for (const auto item_index : local_items) {
            auto &item = plan.items_[item_index];
            const auto *occurrence = snapshot.find(item.occurrence);
            if (!occurrence)
                continue;
            const auto next_visible = visible(item.occurrence, visible);
            const auto was_visible = !has_render_flag(item.flags, RenderFlags::Hidden);
            if (next_visible != was_visible) {
                const auto before = item.flags;
                if (next_visible)
                    item.flags = static_cast<RenderFlags>(
                        static_cast<std::uint32_t>(item.flags) &
                        ~static_cast<std::uint32_t>(RenderFlags::Hidden));
                else
                    item.flags |= RenderFlags::Hidden;
                adjust_counts(before, item.flags);
                ++result.patched_visibility;
            }
            const auto material_found = material_overrides.find(item.occurrence);
            const auto next_material = material_found == material_overrides.end()
                                           ? occurrence->material
                                           : material_found->second;
            if (!snapshot.find_material(next_material)) {
                ++invalidated_items;
                continue;
            }
            if (next_material != item.material) {
                remove_index(plan.items_by_material_, item.material, item_index);
                item.material = next_material;
                plan.items_by_material_[item.material].push_back(item_index);
                ++result.patched_materials;
                result.rebuilt_batches += render_internal::move_item_batch(
                    plan, item_index, item.geometry, item.material);
            }
        }
    } else if (effective_state_dirty) {
        std::vector<OccurrenceId> effective_targets;
        std::unordered_set<OccurrenceId> effective_target_set;
        const auto add_effective_target = [&](OccurrenceId occurrence) {
            if (effective_target_set.insert(occurrence).second)
                effective_targets.push_back(occurrence);
        };
        for (const auto occurrence : changes.effective_state_occurrences)
            add_effective_target(occurrence);
        if (presentation_changed)
            for (const auto &item : plan.items_)
                add_effective_target(item.occurrence);
        for (const auto occurrence_id : effective_targets) {
            const auto item_index = plan.item_index(occurrence_id);
            if (item_index == invalid_item_index || !effective.visible.contains(occurrence_id))
                continue;
            auto &item = plan.items_[item_index];
            const auto visible = effective.visible.at(occurrence_id);
            const auto was_visible = !has_render_flag(item.flags, RenderFlags::Hidden);
            if (visible != was_visible) {
                const auto before = item.flags;
                if (visible)
                    item.flags =
                        static_cast<RenderFlags>(static_cast<std::uint32_t>(item.flags) &
                                                 ~static_cast<std::uint32_t>(RenderFlags::Hidden));
                else
                    item.flags |= RenderFlags::Hidden;
                adjust_counts(before, item.flags);
                ++result.patched_visibility;
            }
            const auto material = effective.material.at(occurrence_id);
            if (!snapshot.find_material(material)) {
                ++invalidated_items;
                continue;
            }
            if (material != item.material) {
                remove_index(plan.items_by_material_, item.material, item_index);
                item.material = material;
                plan.items_by_material_[item.material].push_back(item_index);
                ++result.patched_materials;
                result.rebuilt_batches +=
                    render_internal::move_item_batch(plan, item_index, item.geometry, item.material);
            }
        }
    }

    const auto camera = render_internal::camera_for_snapshot(snapshot, view);
    std::vector<OccurrenceId> culling_targets;
    std::unordered_set<OccurrenceId> culling_target_set;
    const auto add_culling_target = [&](OccurrenceId occurrence) {
        if (culling_target_set.insert(occurrence).second)
            culling_targets.push_back(occurrence);
    };
    for (const auto occurrence : changes.world_transform_occurrences)
        add_culling_target(occurrence);
    if (culling_changed || has_domain_in(changes, ChangeDomain::Camera))
        for (const auto &item : plan.items_)
            add_culling_target(item.occurrence);
    if (!culling_changed)
        for (const auto &change : changes.changes)
            if (has_domain(change.domains, ChangeDomain::Bounds) ||
                has_domain(change.domains, ChangeDomain::Geometry))
                add_culling_target(change.occurrence);
    for (const auto occurrence_id : culling_targets) {
        const auto item_index = plan.item_index(occurrence_id);
        const auto *occurrence = snapshot.find(occurrence_id);
        if (item_index == invalid_item_index || !occurrence)
            continue;
        auto &item = plan.items_[item_index];
        const auto item_culled = culled(*occurrence, camera, view);
        const auto was_culled = has_render_flag(item.flags, RenderFlags::Culled);
        if (item_culled == was_culled)
            continue;
        const auto before = item.flags;
        if (item_culled)
            item.flags |= RenderFlags::Culled;
        else
            item.flags =
                static_cast<RenderFlags>(static_cast<std::uint32_t>(item.flags) &
                                         ~static_cast<std::uint32_t>(RenderFlags::Culled));
        adjust_counts(before, item.flags);
        ++result.patched_culling;
    }
    plan.source_revision_ = snapshot.revision();
    plan.view_signature_ = next_view_signature;
    plan.presentation_signature_ = next_presentation_signature;
    render_internal::capture_view_policy(plan, view);
    plan.view_root_ = view.root;
    plan.culling_signature_ = next_culling_signature;
    plan.view_projection_ = camera.view_projection;
    plan.clip_plane_count_ = 0;
    for (const auto &plane : view.clip_planes) {
        if (!plane.enabled || plan.clip_plane_count_ == RenderPlan::max_clip_planes)
            continue;
        plan.clip_planes_[plan.clip_plane_count_++] = {plane.normal[0], plane.normal[1],
                                                       plane.normal[2], plane.distance};
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
