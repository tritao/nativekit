#include "render_internal.hpp"

#include <array>
#include <cmath>
#include <functional>
#include <unordered_map>
#include <unordered_set>

namespace nkscene::render_internal {

namespace {

bool outside_plane(const Bounds &bounds, const std::array<float, 4> &plane) noexcept {
    const auto x = plane[0] >= 0.0f ? bounds.maximum[0] : bounds.minimum[0];
    const auto y = plane[1] >= 0.0f ? bounds.maximum[1] : bounds.minimum[1];
    const auto z = plane[2] >= 0.0f ? bounds.maximum[2] : bounds.minimum[2];
    return plane[0] * x + plane[1] * y + plane[2] * z + plane[3] < 0.0f;
}

std::array<float, 3> column(const LocalTransform &transform, std::size_t index) noexcept {
    return {transform.matrix[index * 4], transform.matrix[index * 4 + 1],
            transform.matrix[index * 4 + 2]};
}

float dot(const std::array<float, 3> &lhs, const std::array<float, 3> &rhs) noexcept {
    return lhs[0] * rhs[0] + lhs[1] * rhs[1] + lhs[2] * rhs[2];
}

std::array<float, 3> cross(const std::array<float, 3> &lhs,
                           const std::array<float, 3> &rhs) noexcept {
    return {lhs[1] * rhs[2] - lhs[2] * rhs[1], lhs[2] * rhs[0] - lhs[0] * rhs[2],
            lhs[0] * rhs[1] - lhs[1] * rhs[0]};
}

std::array<float, 3> normalized(std::array<float, 3> value) noexcept {
    const auto length = std::sqrt(dot(value, value));
    if (length > 1.0e-6f)
        for (auto &component : value)
            component /= length;
    return value;
}

std::array<float, 16> multiply(const std::array<float, 16> &lhs,
                               const std::array<float, 16> &rhs) noexcept {
    std::array<float, 16> result{};
    for (std::size_t column_index = 0; column_index < 4; ++column_index)
        for (std::size_t row = 0; row < 4; ++row)
            for (std::size_t index = 0; index < 4; ++index)
                result[column_index * 4 + row] +=
                    lhs[index * 4 + row] * rhs[column_index * 4 + index];
    return result;
}

std::array<float, 16> scene_camera_projection(const CameraResource &camera) noexcept {
    const auto near_plane = camera.near_plane;
    const auto far_plane = camera.far_plane;
    const auto aspect = camera.aspect_ratio > 0.0f ? camera.aspect_ratio : 1.0f;
    std::array<float, 16> result{};
    if (camera.projection == CameraProjection::Orthographic) {
        const auto half_height = camera.orthographic_height * 0.5f;
        const auto half_width = half_height * aspect;
        result[0] = 1.0f / half_width;
        result[5] = 1.0f / half_height;
        result[10] = 2.0f / (far_plane - near_plane);
        result[14] = -(far_plane + near_plane) / (far_plane - near_plane);
        result[15] = 1.0f;
    } else {
        const auto focal = 1.0f / std::tan(camera.fov_y * 0.5f);
        result[0] = focal / aspect;
        result[5] = focal;
        result[10] = (far_plane + near_plane) / (far_plane - near_plane);
        result[11] = 1.0f;
        result[14] = -(2.0f * far_plane * near_plane) / (far_plane - near_plane);
    }
    return result;
}

std::array<float, 16> scene_camera_view(const LocalTransform &transform) noexcept {
    const auto position = column(transform, 3);
    const auto forward = normalized(column(transform, 0));
    const auto up = normalized(column(transform, 2));
    const auto right = normalized(cross(forward, up));
    return {right[0], up[0], forward[0], 0.0f,
            right[1], up[1], forward[1], 0.0f,
            right[2], up[2], forward[2], 0.0f,
            -dot(right, position), -dot(up, position), -dot(forward, position), 1.0f};
}

SceneCamera camera_from_occurrence(const SnapshotOccurrence &occurrence,
                                   const CameraResource &resource) noexcept {
    SceneCamera result;
    result.enabled = true;
    result.view_projection = multiply(
        scene_camera_projection(resource), scene_camera_view(occurrence.world_transform.transform));
    return result;
}

} // namespace

SceneCamera camera_for_snapshot(const SceneSnapshot &snapshot, const SceneView &view) noexcept {
    if (view.camera.enabled)
        return view.camera;
    if (view.camera_occurrence.valid()) {
        const auto *occurrence = snapshot.find(view.camera_occurrence);
        if (occurrence && occurrence->camera.valid()) {
            if (const auto *resource = snapshot.find_camera(occurrence->camera))
                return camera_from_occurrence(*occurrence, *resource);
        }
    }
    return {};
}

bool culled_by_camera(const Bounds &bounds, const SceneCamera &camera) noexcept {
    if (!camera.enabled || !bounds.valid)
        return false;
    const auto &m = camera.view_projection;
    const std::array<std::array<float, 4>, 6> planes = {
        {{m[0] + m[3], m[4] + m[7], m[8] + m[11], m[12] + m[15]},
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

bool culled_by_clip_planes(const Bounds &bounds, std::span<const ClipPlane> planes) noexcept {
    if (!bounds.valid)
        return false;
    for (const auto &plane : planes) {
        if (!plane.enabled)
            continue;
        const auto x = plane.normal[0] >= 0.0f ? bounds.maximum[0] : bounds.minimum[0];
        const auto y = plane.normal[1] >= 0.0f ? bounds.maximum[1] : bounds.minimum[1];
        const auto z = plane.normal[2] >= 0.0f ? bounds.maximum[2] : bounds.minimum[2];
        if (plane.normal[0] * x + plane.normal[1] * y + plane.normal[2] * z + plane.distance < 0.0f)
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

    std::unordered_map<EntityId, bool> source_visibility_overrides;
    source_visibility_overrides.reserve(view.filter.source_visibility_overrides.size());
    for (const auto &override : view.filter.source_visibility_overrides)
        source_visibility_overrides[override.source] = override.visible;

    std::unordered_set<OccurrenceId> isolated_keep;
    if (!view.filter.isolated_sources.empty()) {
        for (const auto source : view.filter.isolated_sources) {
            for (const auto occurrence_id : snapshot.occurrences_for_source(source)) {
                auto current = occurrence_id;
                while (current.valid()) {
                    if (!isolated_keep.insert(current).second)
                        break;
                    const auto *occurrence = snapshot.find(current);
                    if (!occurrence || !occurrence->parent.valid())
                        break;
                    current = occurrence->parent;
                }
            }
        }
    }
    for (const auto occurrence_id : view.filter.isolated_occurrences) {
        if (!snapshot.find(occurrence_id))
            continue;
        std::vector<OccurrenceId> pending{occurrence_id};
        while (!pending.empty()) {
            const auto current = pending.back();
            pending.pop_back();
            if (!isolated_keep.insert(current).second)
                continue;
            const auto parent = snapshot.find(current)->parent;
            if (parent.valid())
                pending.push_back(parent);
            const auto child_found = children.find(current);
            if (child_found != children.end())
                for (const auto child : child_found->second)
                    pending.push_back(child);
        }
    }
    const bool isolation_active =
        !view.filter.isolated_sources.empty() || !view.filter.isolated_occurrences.empty();

    std::unordered_map<OccurrenceId, MaterialId> material_overrides;
    material_overrides.reserve(view.material_overrides.size() +
                               view.selection_material_overrides.size() +
                               view.hover_material_overrides.size());
    for (const auto &occurrence : occurrences) {
        const auto found = std::find_if(
            view.filter.source_material_overrides.begin(),
            view.filter.source_material_overrides.end(),
            [&occurrence](const auto &override) { return override.source == occurrence.source; });
        if (found != view.filter.source_material_overrides.end())
            result.material[occurrence.occurrence] = found->material;
    }
    const auto apply_material_layer = [&material_overrides](const auto &overrides) {
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
        const auto source_override_found = source_visibility_overrides.find(occurrence->source);
        bool local_visible = view.include_invisible || occurrence->visible;
        if (source_override_found != source_visibility_overrides.end())
            local_visible = source_override_found->second;
        if (override_found != visibility_overrides.end())
            local_visible = override_found->second;
        if (isolation_active && !isolated_keep.contains(start))
            local_visible = false;
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
    add(view.filter.isolated_sources.size());
    for (const auto source : view.filter.isolated_sources)
        add(source.value);
    add(view.filter.source_visibility_overrides.size());
    for (const auto &override : view.filter.source_visibility_overrides) {
        add(override.source.value);
        add(override.visible ? 1 : 0);
    }
    add(view.filter.source_material_overrides.size());
    for (const auto &override : view.filter.source_material_overrides) {
        add(override.source.value);
        add(override.material.value);
    }
    add(view.filter.isolated_occurrences.size());
    for (const auto occurrence : view.filter.isolated_occurrences)
        add(occurrence.value);
    add(view.camera.enabled ? 1 : 0);
    add(view.camera_occurrence.value);
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
    add(view.camera_occurrence.value);
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
    const auto camera = camera_for_snapshot(snapshot, view);
    plan.items_.clear();
    plan.transforms_.clear();
    plan.visible_items_ = 0;
    plan.culled_items_ = 0;
    plan.view_projection_ = camera.view_projection;
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
        if (culled_by_camera(occurrence.bounds, camera) ||
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
