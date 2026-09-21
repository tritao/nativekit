#include "render_internal.hpp"

#include <algorithm>
#include <atomic>
#include <unordered_set>

namespace nkscene {

namespace {

std::atomic_uint64_t next_gpu_identity{1};

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

bool isolation_only(const SceneView &view) noexcept {
    return !view.include_invisible && view.filter.source_visibility_overrides.empty() &&
           view.filter.source_material_overrides.empty() && view.visibility_overrides.empty() &&
           view.material_overrides.empty() && view.selection_material_overrides.empty() &&
           view.hover_material_overrides.empty() &&
           (!view.filter.isolated_sources.empty() || !view.filter.isolated_occurrences.empty());
}

std::unordered_set<OccurrenceId>
isolation_keep(const SceneSnapshot &snapshot, std::span<const EntityId> sources,
              std::span<const OccurrenceId> occurrences) {
    std::unordered_set<OccurrenceId> keep;
    for (const auto source : sources) {
        for (const auto occurrence_id : snapshot.occurrences_for_source(source)) {
            auto current = occurrence_id;
            while (current.valid()) {
                if (!keep.insert(current).second)
                    break;
                const auto *occurrence = snapshot.find(current);
                if (!occurrence || !occurrence->parent.valid())
                    break;
                current = occurrence->parent;
            }
        }
    }
    for (const auto occurrence_id : occurrences) {
        if (!snapshot.find(occurrence_id))
            continue;
        std::vector<OccurrenceId> pending{occurrence_id};
        while (!pending.empty()) {
            const auto current = pending.back();
            pending.pop_back();
            if (!keep.insert(current).second)
                continue;
            const auto *occurrence = snapshot.find(current);
            if (!occurrence)
                continue;
            if (occurrence->parent.valid())
                pending.push_back(occurrence->parent);
            for (const auto child : snapshot.children(current))
                pending.push_back(child);
        }
    }
    return keep;
}

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
    plan.view_source_policy_sources_.clear();
    plan.view_source_policy_sources_.reserve(view.filter.source_visibility_overrides.size() +
                                             view.filter.source_material_overrides.size());
    for (const auto &override : view.filter.source_visibility_overrides)
        plan.view_source_policy_sources_.push_back(override.source);
    for (const auto &override : view.filter.source_material_overrides)
        plan.view_source_policy_sources_.push_back(override.source);
    std::sort(plan.view_source_policy_sources_.begin(), plan.view_source_policy_sources_.end(),
              [](EntityId lhs, EntityId rhs) { return lhs.value < rhs.value; });
    plan.view_source_policy_sources_.erase(
        std::unique(plan.view_source_policy_sources_.begin(),
                    plan.view_source_policy_sources_.end()),
        plan.view_source_policy_sources_.end());
    plan.view_global_policy_ = view.include_invisible || !view.filter.isolated_sources.empty() ||
                               !view.filter.source_visibility_overrides.empty() ||
                               !view.filter.source_material_overrides.empty() ||
                               !view.filter.isolated_occurrences.empty();
    plan.view_isolation_sources_ = view.filter.isolated_sources;
    plan.view_isolation_occurrences_ = view.filter.isolated_occurrences;
    plan.view_source_rules_only_ =
        !view.include_invisible && view.filter.isolated_sources.empty() &&
        view.filter.isolated_occurrences.empty() && view.visibility_overrides.empty() &&
        view.material_overrides.empty() && view.selection_material_overrides.empty() &&
        view.hover_material_overrides.empty() &&
        (!view.filter.source_visibility_overrides.empty() ||
         !view.filter.source_material_overrides.empty());
    plan.view_isolation_only_ = isolation_only(view);
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
    plan.camera_enabled_ = view.camera.enabled;
    plan.geometry_revisions_.reserve(snapshot.geometries().size());
    for (const auto &resource : snapshot.geometries())
        plan.geometry_revisions_.emplace(resource.id, resource.revision);
    plan.material_revisions_.reserve(snapshot.materials().size());
    for (const auto &resource : snapshot.materials())
        plan.material_revisions_.emplace(resource.id, resource.revision);
    plan.geometry_resources_revision_ = snapshot.geometry_resources_revision();
    plan.material_resources_revision_ = snapshot.material_resources_revision();
    plan.culling_index_ = std::make_shared<SceneSpatialIndex>(snapshot);
    plan.culling_dirty_occurrences_.clear();
    plan.culling_unbounded_occurrences_.clear();
    plan.culled_occurrences_.clear();
    for (const auto &item : plan.items_) {
        const auto *occurrence = snapshot.find(item.occurrence);
        if (occurrence && !occurrence->bounds.valid)
            plan.culling_unbounded_occurrences_.insert(item.occurrence);
        if (has_render_flag(item.flags, RenderFlags::Culled))
            plan.culled_occurrences_.insert(item.occurrence);
    }
    plan.compile_count_ = 1;
    plan.gpu_identity_ = next_gpu_identity.fetch_add(1, std::memory_order_relaxed);
    plan.gpu_revision_ = 1;
    RenderPlan::GpuDelta delta;
    delta.revision = plan.gpu_revision_;
    delta.full_rebuild = true;
    delta.layout_changed = true;
    plan.gpu_delta_history_.push_back(std::move(delta));
    plan.gpu_delta_history_start_ = plan.gpu_delta_history_.front().revision;
    return plan;
}

RenderUpdate update(RenderPlan &plan, const SceneSnapshot &snapshot, const ChangeSet &changes,
                    const SceneView &view) {
    RenderUpdate result;
    std::vector<OccurrenceId> layout_occurrences;
    std::unordered_set<OccurrenceId> layout_occurrence_set;
    const auto mark_layout_occurrence = [&](OccurrenceId occurrence) {
        if (layout_occurrence_set.insert(occurrence).second)
            layout_occurrences.push_back(occurrence);
    };
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
    const bool hierarchy_changed = has_domain_in(changes, ChangeDomain::Hierarchy);
    const bool scene_hierarchy_or_source_change =
        hierarchy_changed ||
        has_domain_in(changes, ChangeDomain::Source);
    const bool current_global_policy =
        view.include_invisible || !view.filter.isolated_sources.empty() ||
        !view.filter.source_visibility_overrides.empty() ||
        !view.filter.source_material_overrides.empty() ||
        !view.filter.isolated_occurrences.empty();
    const bool current_source_rules_only =
        !view.include_invisible && view.filter.isolated_sources.empty() &&
        view.filter.isolated_occurrences.empty() && view.visibility_overrides.empty() &&
        view.material_overrides.empty() && view.selection_material_overrides.empty() &&
        view.hover_material_overrides.empty() &&
        (!view.filter.source_visibility_overrides.empty() ||
         !view.filter.source_material_overrides.empty());
    const bool source_rules_compatible =
        (current_source_rules_only &&
         (!plan.view_global_policy_ || plan.view_source_rules_only_)) ||
        (!current_global_policy && plan.view_source_rules_only_);
    const bool current_isolation_only = isolation_only(view);
    const bool local_policy = !plan.view_global_policy_ && !current_global_policy;
    const bool local_scene_effective_change =
        local_policy && !scene_hierarchy_or_source_change &&
        (has_domain_in(changes, ChangeDomain::Visibility) ||
         has_domain_in(changes, ChangeDomain::Material));
    const bool local_presentation_change =
        local_policy && presentation_changed && !scene_hierarchy_or_source_change;
    const bool local_effective_change = local_scene_effective_change || local_presentation_change;
    const bool source_rules_change =
        source_rules_compatible && !scene_hierarchy_or_source_change &&
        (presentation_changed || scene_effective_change);
    const bool isolation_change =
        current_isolation_only && plan.view_isolation_only_ && !scene_hierarchy_or_source_change &&
        (presentation_changed || scene_effective_change);
    const bool topology_changed = has_domain_in(changes, ChangeDomain::Created) ||
                                  has_domain_in(changes, ChangeDomain::Destroyed);
    const bool effective_state_dirty =
        !local_effective_change && !source_rules_change && !isolation_change &&
                                       (presentation_changed || scene_effective_change);
    const auto effective = effective_state_dirty ? render_internal::effective_state(snapshot, view)
                                                 : render_internal::EffectiveState{};
    const bool geometry_resources_changed =
        plan.geometry_resources_revision_ != snapshot.geometry_resources_revision();
    const bool material_resources_changed =
        plan.material_resources_revision_ != snapshot.material_resources_revision();
    ResourceChanges resource_changes;
    const bool resource_changes_complete = snapshot.resource_changes_since(
        plan.geometry_resources_revision_, plan.material_resources_revision_, resource_changes);
    std::unordered_set<GeometryId> changed_geometry_resources;
    std::unordered_set<MaterialId> changed_material_resources;
    std::size_t invalidated_items = 0;
    if (geometry_resources_changed) {
        if (resource_changes_complete) {
            for (const auto geometry : resource_changes.geometries) {
                const auto *resource = snapshot.find_geometry(geometry);
                if (resource) {
                    const auto found = plan.geometry_revisions_.find(geometry);
                    if (found != plan.geometry_revisions_.end() &&
                        found->second != resource->revision &&
                        plan.items_by_geometry_.contains(geometry))
                        changed_geometry_resources.insert(geometry);
                } else if (plan.items_by_geometry_.contains(geometry)) {
                    invalidated_items += plan.items_by_geometry_.at(geometry).size();
                }
            }
        } else {
            for (const auto &resource : snapshot.geometries()) {
                const auto found = plan.geometry_revisions_.find(resource.id);
                if (found != plan.geometry_revisions_.end() && found->second != resource.revision &&
                    plan.items_by_geometry_.contains(resource.id))
                    changed_geometry_resources.insert(resource.id);
            }
            for (const auto &[geometry, unused] : plan.geometry_revisions_)
                if (!snapshot.find_geometry(geometry) && plan.items_by_geometry_.contains(geometry))
                    invalidated_items += plan.items_by_geometry_.at(geometry).size();
        }
    }
    if (material_resources_changed) {
        if (resource_changes_complete) {
            for (const auto material : resource_changes.materials) {
                const auto *resource = snapshot.find_material(material);
                if (resource) {
                    const auto found = plan.material_revisions_.find(material);
                    if (found != plan.material_revisions_.end() &&
                        found->second != resource->revision &&
                        plan.items_by_material_.contains(material))
                        changed_material_resources.insert(material);
                } else if (plan.items_by_material_.contains(material)) {
                    invalidated_items += plan.items_by_material_.at(material).size();
                }
            }
        } else {
            for (const auto &resource : snapshot.materials()) {
                const auto found = plan.material_revisions_.find(resource.id);
                if (found != plan.material_revisions_.end() && found->second != resource.revision &&
                    plan.items_by_material_.contains(resource.id))
                    changed_material_resources.insert(resource.id);
            }
            for (const auto &[material, unused] : plan.material_revisions_)
                if (!snapshot.find_material(material) && plan.items_by_material_.contains(material))
                    invalidated_items += plan.items_by_material_.at(material).size();
        }
    }
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
        (view.root.valid() && hierarchy_changed)) {
        plan = compile(snapshot, view);
        result.plan_rebuilt = true;
        result.invalidated_items = invalidated_items;
        result.visible_items = plan.visible_items_;
        result.culled_items = plan.culled_items_;
        return result;
    }

    if (hierarchy_changed)
        render_internal::update_ancestor_index(plan, snapshot, changes);

    result.updated_geometry_resources = changed_geometry_resources.size();
    result.updated_material_resources = changed_material_resources.size();
    if (geometry_resources_changed) {
        if (resource_changes_complete) {
            for (const auto geometry : resource_changes.geometries) {
                if (const auto *resource = snapshot.find_geometry(geometry))
                    plan.geometry_revisions_[geometry] = resource->revision;
                else
                    plan.geometry_revisions_.erase(geometry);
            }
        } else {
            plan.geometry_revisions_.clear();
            plan.geometry_revisions_.reserve(snapshot.geometries().size());
            for (const auto &resource : snapshot.geometries())
                plan.geometry_revisions_.emplace(resource.id, resource.revision);
        }
        plan.geometry_resources_revision_ = snapshot.geometry_resources_revision();
    }
    if (material_resources_changed) {
        if (resource_changes_complete) {
            for (const auto material : resource_changes.materials) {
                if (const auto *resource = snapshot.find_material(material))
                    plan.material_revisions_[material] = resource->revision;
                else
                    plan.material_revisions_.erase(material);
            }
        } else {
            plan.material_revisions_.clear();
            plan.material_revisions_.reserve(snapshot.materials().size());
            for (const auto &resource : snapshot.materials())
                plan.material_revisions_.emplace(resource.id, resource.revision);
        }
        plan.material_resources_revision_ = snapshot.material_resources_revision();
    }

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
            mark_layout_occurrence(change.occurrence);
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
        const auto add_ancestor_items = [&](OccurrenceId occurrence) {
            const auto found = plan.items_by_ancestor_.find(occurrence);
            if (found == plan.items_by_ancestor_.end())
                return;
            for (const auto item_index : found->second)
                if (local_item_set.insert(item_index).second)
                    local_items.push_back(item_index);
        };
        for (const auto occurrence : local_targets)
            add_ancestor_items(occurrence);

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
                mark_layout_occurrence(item.occurrence);
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
                mark_layout_occurrence(item.occurrence);
                remove_index(plan.items_by_material_, item.material, item_index);
                item.material = next_material;
                plan.items_by_material_[item.material].push_back(item_index);
                ++result.patched_materials;
                result.rebuilt_batches += render_internal::move_item_batch(
                    plan, item_index, item.geometry, item.material);
            }
        }
    } else if (source_rules_change) {
        std::unordered_map<EntityId, bool> source_visibility;
        for (const auto &override : view.filter.source_visibility_overrides)
            source_visibility[override.source] = override.visible;
        std::unordered_map<EntityId, MaterialId> source_material;
        for (const auto &override : view.filter.source_material_overrides)
            source_material[override.source] = override.material;

        std::unordered_set<std::size_t> target_items;
        const auto add_item = [&target_items, &plan](OccurrenceId occurrence) {
            const auto item_index = plan.item_index(occurrence);
            if (item_index != invalid_item_index)
                target_items.insert(item_index);
        };
        const auto add_ancestor_items = [&](OccurrenceId occurrence) {
            const auto found = plan.items_by_ancestor_.find(occurrence);
            if (found == plan.items_by_ancestor_.end())
                return;
            for (const auto item_index : found->second)
                target_items.insert(item_index);
        };

        std::unordered_set<EntityId> policy_sources;
        for (const auto source : plan.view_source_policy_sources_)
            policy_sources.insert(source);
        for (const auto &override : view.filter.source_visibility_overrides)
            policy_sources.insert(override.source);
        for (const auto &override : view.filter.source_material_overrides)
            policy_sources.insert(override.source);
        for (const auto source : policy_sources)
            for (const auto occurrence : snapshot.occurrences_for_source(source))
                add_ancestor_items(occurrence);

        for (const auto occurrence : changes.effective_state_occurrences)
            add_ancestor_items(occurrence);
        for (const auto &change : changes.changes) {
            if (has_domain(change.domains, ChangeDomain::Visibility))
                add_ancestor_items(change.occurrence);
            else if (has_domain(change.domains, ChangeDomain::Material))
                add_item(change.occurrence);
        }

        std::unordered_map<OccurrenceId, bool> desired_visibility;
        const auto visible = [&](OccurrenceId occurrence, const auto &self) -> bool {
            const auto cached = desired_visibility.find(occurrence);
            if (cached != desired_visibility.end())
                return cached->second;
            const auto *value = snapshot.find(occurrence);
            if (!value)
                return false;
            bool result = value->visible;
            const auto override_found = source_visibility.find(value->source);
            if (override_found != source_visibility.end())
                result = override_found->second;
            if (value->parent.valid())
                result = result && self(value->parent, self);
            desired_visibility.emplace(occurrence, result);
            return result;
        };

        for (const auto item_index : target_items) {
            auto &item = plan.items_[item_index];
            const auto *occurrence = snapshot.find(item.occurrence);
            if (!occurrence)
                continue;
            const auto next_visible = visible(item.occurrence, visible);
            const auto was_visible = !has_render_flag(item.flags, RenderFlags::Hidden);
            if (next_visible != was_visible) {
                mark_layout_occurrence(item.occurrence);
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
            auto next_material = occurrence->material;
            const auto material_found = source_material.find(occurrence->source);
            if (material_found != source_material.end())
                next_material = material_found->second;
            if (!snapshot.find_material(next_material)) {
                ++invalidated_items;
                continue;
            }
            if (next_material != item.material) {
                mark_layout_occurrence(item.occurrence);
                remove_index(plan.items_by_material_, item.material, item_index);
                item.material = next_material;
                plan.items_by_material_[item.material].push_back(item_index);
                ++result.patched_materials;
                result.rebuilt_batches += render_internal::move_item_batch(
                    plan, item_index, item.geometry, item.material);
            }
        }
    } else if (isolation_change) {
        const auto previous_keep =
            isolation_keep(snapshot, plan.view_isolation_sources_, plan.view_isolation_occurrences_);
        const auto current_keep = isolation_keep(snapshot, view.filter.isolated_sources,
                                                 view.filter.isolated_occurrences);
        std::unordered_set<std::size_t> target_items;
        const auto add_item = [&target_items, &plan](OccurrenceId occurrence) {
            const auto item_index = plan.item_index(occurrence);
            if (item_index != invalid_item_index)
                target_items.insert(item_index);
        };
        const auto add_ancestor_items = [&](OccurrenceId occurrence) {
            const auto found = plan.items_by_ancestor_.find(occurrence);
            if (found == plan.items_by_ancestor_.end())
                return;
            for (const auto item_index : found->second)
                target_items.insert(item_index);
        };
        for (const auto occurrence : previous_keep)
            if (!current_keep.contains(occurrence))
                add_ancestor_items(occurrence);
        for (const auto occurrence : current_keep)
            if (!previous_keep.contains(occurrence))
                add_ancestor_items(occurrence);
        for (const auto occurrence : changes.effective_state_occurrences)
            add_ancestor_items(occurrence);
        for (const auto &change : changes.changes) {
            if (has_domain(change.domains, ChangeDomain::Visibility))
                add_ancestor_items(change.occurrence);
            else if (has_domain(change.domains, ChangeDomain::Material))
                add_item(change.occurrence);
        }

        std::unordered_map<OccurrenceId, bool> desired_visibility;
        const auto visible = [&](OccurrenceId occurrence, const auto &self) -> bool {
            const auto cached = desired_visibility.find(occurrence);
            if (cached != desired_visibility.end())
                return cached->second;
            const auto *value = snapshot.find(occurrence);
            if (!value)
                return false;
            auto result = value->visible && current_keep.contains(occurrence);
            if (value->parent.valid())
                result = result && self(value->parent, self);
            desired_visibility.emplace(occurrence, result);
            return result;
        };
        for (const auto item_index : target_items) {
            auto &item = plan.items_[item_index];
            const auto *occurrence = snapshot.find(item.occurrence);
            if (!occurrence)
                continue;
            const auto next_visible = visible(item.occurrence, visible);
            const auto was_visible = !has_render_flag(item.flags, RenderFlags::Hidden);
            if (next_visible != was_visible) {
                mark_layout_occurrence(item.occurrence);
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
            if (!snapshot.find_material(occurrence->material)) {
                ++invalidated_items;
                continue;
            }
            if (occurrence->material != item.material) {
                mark_layout_occurrence(item.occurrence);
                remove_index(plan.items_by_material_, item.material, item_index);
                item.material = occurrence->material;
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
                mark_layout_occurrence(item.occurrence);
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
                mark_layout_occurrence(item.occurrence);
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
    const auto mark_culling_dirty = [&plan, &snapshot](OccurrenceId occurrence) {
        plan.culling_dirty_occurrences_.insert(occurrence);
        const auto *value = snapshot.find(occurrence);
        if (value && !value->bounds.valid)
            plan.culling_unbounded_occurrences_.insert(occurrence);
        else
            plan.culling_unbounded_occurrences_.erase(occurrence);
    };
    for (const auto occurrence : changes.world_transform_occurrences)
        mark_culling_dirty(occurrence);
    for (const auto &change : changes.changes)
        if (has_domain(change.domains, ChangeDomain::Bounds) ||
            has_domain(change.domains, ChangeDomain::Geometry))
            mark_culling_dirty(change.occurrence);
    for (const auto geometry : changed_geometry_resources) {
        const auto found = plan.items_by_geometry_.find(geometry);
        if (found == plan.items_by_geometry_.end())
            continue;
        for (const auto item_index : found->second)
            mark_culling_dirty(plan.items_[item_index].occurrence);
    }
    std::vector<OccurrenceId> culling_targets;
    std::unordered_set<OccurrenceId> culling_target_set;
    const auto add_culling_target = [&](OccurrenceId occurrence) {
        if (culling_target_set.insert(occurrence).second)
            culling_targets.push_back(occurrence);
    };
    for (const auto occurrence : changes.world_transform_occurrences)
        add_culling_target(occurrence);
    if (culling_changed) {
        std::vector<std::array<float, 4>> next_planes;
        render_internal::append_culling_planes(view, next_planes);
        if (plan.culling_index_) {
            SceneView previous_view;
            previous_view.camera.enabled = plan.camera_enabled_;
            previous_view.camera.view_projection = plan.view_projection_;
            for (const auto &plane : plan.clip_planes())
                previous_view.clip_planes.push_back({{plane[0], plane[1], plane[2]}, plane[3], true});
            std::vector<std::array<float, 4>> previous_planes;
            render_internal::append_culling_planes(previous_view, previous_planes);
            for (const auto occurrence : plan.culling_index_->query_frustum(previous_planes))
                add_culling_target(occurrence);
            for (const auto occurrence : plan.culling_index_->query_frustum(next_planes))
                add_culling_target(occurrence);
        } else {
            for (const auto &item : plan.items_)
                add_culling_target(item.occurrence);
        }
        if (next_planes.empty())
            for (const auto occurrence : plan.culled_occurrences_)
                add_culling_target(occurrence);
        for (const auto occurrence : plan.culling_unbounded_occurrences_)
            add_culling_target(occurrence);
        for (const auto occurrence : plan.culling_dirty_occurrences_)
            add_culling_target(occurrence);
    }
    if (!culling_changed && has_domain_in(changes, ChangeDomain::Bounds))
        for (const auto &change : changes.changes)
            if (has_domain(change.domains, ChangeDomain::Bounds) ||
                has_domain(change.domains, ChangeDomain::Geometry))
                add_culling_target(change.occurrence);
    result.culling_candidates = culling_targets.size();
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
        mark_layout_occurrence(occurrence_id);
        const auto before = item.flags;
        if (item_culled)
            item.flags |= RenderFlags::Culled;
        else
            item.flags =
                static_cast<RenderFlags>(static_cast<std::uint32_t>(item.flags) &
                                         ~static_cast<std::uint32_t>(RenderFlags::Culled));
        adjust_counts(before, item.flags);
        ++result.patched_culling;
        if (item_culled)
            plan.culled_occurrences_.insert(occurrence_id);
        else
            plan.culled_occurrences_.erase(occurrence_id);
    }
    plan.source_revision_ = snapshot.revision();
    plan.view_signature_ = next_view_signature;
    plan.presentation_signature_ = next_presentation_signature;
    render_internal::capture_view_policy(plan, view);
    plan.view_root_ = view.root;
    plan.culling_signature_ = next_culling_signature;
    plan.camera_enabled_ = view.camera.enabled;
    plan.view_projection_ =
        view.camera.enabled ? view.camera.view_projection : SceneCamera{}.view_projection;
    plan.clip_plane_count_ = 0;
    for (const auto &plane : view.clip_planes) {
        if (!plane.enabled || plan.clip_plane_count_ == RenderPlan::max_clip_planes)
            continue;
        plan.clip_planes_[plan.clip_plane_count_++] = {plane.normal[0], plane.normal[1],
                                                       plane.normal[2], plane.distance};
    }
    result.visible_items = plan.visible_items_;
    result.culled_items = plan.culled_items_;

    RenderPlan::GpuDelta delta;
    delta.revision = ++plan.gpu_revision_;
    delta.layout_changed = result.geometry_rebuilt || result.rebuilt_batches != 0 ||
                           result.patched_visibility != 0 || result.patched_materials != 0 ||
                           result.patched_culling != 0;
    delta.resource_delta_complete = resource_changes_complete;
    delta.transforms = changes.world_transform_occurrences;
    delta.layout_occurrences = std::move(layout_occurrences);
    if (resource_changes_complete) {
        delta.geometries = resource_changes.geometries;
        delta.materials = resource_changes.materials;
    }
    plan.gpu_delta_history_.push_back(std::move(delta));
    if (plan.gpu_delta_history_.size() > RenderPlan::gpu_delta_history_limit)
        plan.gpu_delta_history_.erase(plan.gpu_delta_history_.begin());
    plan.gpu_delta_history_start_ = plan.gpu_delta_history_.front().revision;
    return result;
}

RenderUpdate refresh(RenderPlan &plan, const SceneSnapshot &snapshot, const SceneView &view) {
    RenderUpdate result;
    if (plan.source_revision() == snapshot.revision() &&
        plan.geometry_resources_revision_ == snapshot.geometry_resources_revision() &&
        plan.material_resources_revision_ == snapshot.material_resources_revision() &&
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
