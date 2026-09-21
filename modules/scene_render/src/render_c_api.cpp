#include "nativekit_scene_render.h"

#include "handles.hpp"
#include "scene_internal.hpp"

#include <cstddef>
#include <memory>
#include <mutex>

namespace {

struct RenderRegistry {
    std::mutex mutex;
    nkscene::HandleTable<nkscene::RenderPlan> plans;
    nkscene::HandleTable<nkscene::NativeKitGpuExecutor> executors;
    nkscene::HandleTable<nkscene::SceneSpatialIndex> spatial_indices;
    nkscene::HandleTable<nkscene::GpuPickRequest> pick_requests;
};

RenderRegistry &registry() {
    static RenderRegistry value;
    return value;
}

nkscene_result copy_view(const nkscene_render_view *input, nkscene::SceneView &output) {
    if (!input)
        return NKS_OK;
    constexpr auto legacy_size = offsetof(nkscene_render_view, selection_overrides);
    if (input->struct_size < legacy_size)
        return NKS_ERROR_INVALID_ARGUMENT;
    const bool has_selection_overrides =
        input->struct_size >= offsetof(nkscene_render_view, selection_override_count) +
                                  sizeof(input->selection_override_count);
    const bool has_hover_overrides =
        input->struct_size >=
        offsetof(nkscene_render_view, hover_override_count) + sizeof(input->hover_override_count);
    const bool has_isolated_sources =
        input->struct_size >=
        offsetof(nkscene_render_view, isolated_source_count) + sizeof(input->isolated_source_count);
    const bool has_source_visibility_overrides =
        input->struct_size >= offsetof(nkscene_render_view, source_visibility_override_count) +
                                  sizeof(input->source_visibility_override_count);
    const bool has_source_material_overrides =
        input->struct_size >= offsetof(nkscene_render_view, source_material_override_count) +
                                  sizeof(input->source_material_override_count);
    const bool has_isolated_occurrences =
        input->struct_size >= offsetof(nkscene_render_view, isolated_occurrence_count) +
                                  sizeof(input->isolated_occurrence_count);
    if ((input->visibility_override_count != 0 && !input->visibility_overrides) ||
        (input->material_override_count != 0 && !input->material_overrides) ||
        (input->clip_plane_count != 0 && !input->clip_planes) ||
        (has_selection_overrides && input->selection_override_count != 0 &&
         !input->selection_overrides) ||
        (has_hover_overrides && input->hover_override_count != 0 && !input->hover_overrides) ||
        (has_isolated_sources && input->isolated_source_count != 0 && !input->isolated_sources) ||
        (has_source_visibility_overrides && input->source_visibility_override_count != 0 &&
         !input->source_visibility_overrides) ||
        (has_source_material_overrides && input->source_material_override_count != 0 &&
         !input->source_material_overrides) ||
        (has_isolated_occurrences && input->isolated_occurrence_count != 0 &&
         !input->isolated_occurrences))
        return NKS_ERROR_INVALID_ARGUMENT;

    output.root = {input->root.value};
    output.include_invisible = input->include_invisible != 0;
    output.visibility_overrides.reserve(input->visibility_override_count);
    for (uint32_t index = 0; index < input->visibility_override_count; ++index) {
        const auto &value = input->visibility_overrides[index];
        output.visibility_overrides.push_back({{value.occurrence.value}, value.visible != 0});
    }
    output.material_overrides.reserve(input->material_override_count);
    for (uint32_t index = 0; index < input->material_override_count; ++index) {
        const auto &value = input->material_overrides[index];
        output.material_overrides.push_back({{value.occurrence.value}, {value.material.value}});
    }
    if (has_selection_overrides) {
        output.selection_material_overrides.reserve(input->selection_override_count);
        for (uint32_t index = 0; index < input->selection_override_count; ++index) {
            const auto &value = input->selection_overrides[index];
            output.selection_material_overrides.push_back(
                {{value.occurrence.value}, {value.material.value}});
        }
    }
    if (has_hover_overrides) {
        output.hover_material_overrides.reserve(input->hover_override_count);
        for (uint32_t index = 0; index < input->hover_override_count; ++index) {
            const auto &value = input->hover_overrides[index];
            output.hover_material_overrides.push_back(
                {{value.occurrence.value}, {value.material.value}});
        }
    }
    if (has_isolated_sources) {
        output.filter.isolated_sources.reserve(input->isolated_source_count);
        for (uint32_t index = 0; index < input->isolated_source_count; ++index)
            output.filter.isolated_sources.push_back({input->isolated_sources[index].value});
    }
    if (has_source_visibility_overrides) {
        output.filter.source_visibility_overrides.reserve(input->source_visibility_override_count);
        for (uint32_t index = 0; index < input->source_visibility_override_count; ++index) {
            const auto &value = input->source_visibility_overrides[index];
            output.filter.source_visibility_overrides.push_back(
                {{value.source.value}, value.visible != 0});
        }
    }
    if (has_source_material_overrides) {
        output.filter.source_material_overrides.reserve(input->source_material_override_count);
        for (uint32_t index = 0; index < input->source_material_override_count; ++index) {
            const auto &value = input->source_material_overrides[index];
            output.filter.source_material_overrides.push_back(
                {{value.source.value}, {value.material.value}});
        }
    }
    if (has_isolated_occurrences) {
        output.filter.isolated_occurrences.reserve(input->isolated_occurrence_count);
        for (uint32_t index = 0; index < input->isolated_occurrence_count; ++index)
            output.filter.isolated_occurrences.push_back({input->isolated_occurrences[index].value});
    }
    output.clip_planes.reserve(input->clip_plane_count);
    for (uint32_t index = 0; index < input->clip_plane_count; ++index) {
        const auto &value = input->clip_planes[index];
        nkscene::ClipPlane plane;
        for (uint32_t axis = 0; axis < 3; ++axis)
            plane.normal[axis] = value.normal[axis];
        plane.distance = value.distance;
        plane.enabled = value.enabled != 0;
        output.clip_planes.push_back(plane);
    }
    output.camera.enabled = input->camera.enabled != 0;
    for (uint32_t index = 0; index < 16; ++index)
        output.camera.view_projection[index] = input->camera.view_projection.matrix[index];
    return NKS_OK;
}

} // namespace

extern "C" {

nkscene_result NKS_CALL nkscene_render_plan_compile(nkscene_snapshot snapshot_handle,
                                                    const nkscene_render_view *view_input,
                                                    nkscene_render_plan *out_plan) {
    if (!out_plan)
        return NKS_ERROR_INVALID_ARGUMENT;
    *out_plan = 0;
    nkscene::SceneView view;
    const auto view_result = copy_view(view_input, view);
    if (view_result != NKS_OK)
        return view_result;
    const auto snapshot = nkscene::resolve_snapshot_handle(snapshot_handle);
    if (!snapshot)
        return NKS_ERROR_INVALID_HANDLE;

    auto plan = std::make_shared<nkscene::RenderPlan>(nkscene::compile(*snapshot, view));
    auto &state = registry();
    std::lock_guard lock(state.mutex);
    const auto handle = state.plans.create(std::move(plan));
    *out_plan = nkscene::pack_handle(handle);
    return NKS_OK;
}

void NKS_CALL nkscene_render_plan_destroy(nkscene_render_plan plan) {
    auto &state = registry();
    std::lock_guard lock(state.mutex);
    state.plans.remove(nkscene::unpack_handle(plan));
}

nkscene_result NKS_CALL nkscene_render_plan_get_item_count(nkscene_render_plan plan_handle,
                                                           uint64_t *out_count) {
    if (!out_count)
        return NKS_ERROR_INVALID_ARGUMENT;
    auto &state = registry();
    std::lock_guard lock(state.mutex);
    const auto plan = state.plans.get(nkscene::unpack_handle(plan_handle));
    if (!plan)
        return NKS_ERROR_INVALID_HANDLE;
    *out_count = plan->items().size();
    return NKS_OK;
}

nkscene_result NKS_CALL nkscene_render_plan_update(nkscene_render_plan plan_handle,
                                                   nkscene_snapshot snapshot_handle,
                                                   nkscene_change_set changes_handle,
                                                   const nkscene_render_view *view_input,
                                                   nkscene_render_update *out_update) {
    if (!out_update)
        return NKS_ERROR_INVALID_ARGUMENT;
    if (out_update->struct_size < sizeof(nkscene_render_update))
        return NKS_ERROR_INVALID_ARGUMENT;
    nkscene::SceneView view;
    const auto view_result = copy_view(view_input, view);
    if (view_result != NKS_OK)
        return view_result;
    const auto snapshot = nkscene::resolve_snapshot_handle(snapshot_handle);
    const auto changes = nkscene::resolve_change_set_handle(changes_handle);
    if (!snapshot || !changes)
        return NKS_ERROR_INVALID_HANDLE;

    auto &state = registry();
    std::lock_guard lock(state.mutex);
    const auto plan = state.plans.get(nkscene::unpack_handle(plan_handle));
    if (!plan)
        return NKS_ERROR_INVALID_HANDLE;
    const auto update = nkscene::update(*plan, *snapshot, *changes, view);
    *out_update = {};
    out_update->struct_size = sizeof(nkscene_render_update);
    out_update->plan_rebuilt = update.plan_rebuilt;
    out_update->geometry_rebuilt = update.geometry_rebuilt;
    out_update->patched_instances = update.patched_instances;
    out_update->patched_visibility = update.patched_visibility;
    out_update->patched_materials = update.patched_materials;
    out_update->rebuilt_batches = update.rebuilt_batches;
    out_update->updated_geometry_resources = update.updated_geometry_resources;
    out_update->updated_material_resources = update.updated_material_resources;
    out_update->invalidated_items = update.invalidated_items;
    out_update->patched_culling = update.patched_culling;
    out_update->visible_items = update.visible_items;
    out_update->culled_items = update.culled_items;
    return NKS_OK;
}

nkscene_result NKS_CALL nkscene_render_plan_refresh(nkscene_render_plan plan_handle,
                                                    nkscene_snapshot snapshot_handle,
                                                    const nkscene_render_view *view_input,
                                                    nkscene_render_update *out_update) {
    if (!out_update)
        return NKS_ERROR_INVALID_ARGUMENT;
    if (out_update->struct_size < sizeof(nkscene_render_update))
        return NKS_ERROR_INVALID_ARGUMENT;
    nkscene::SceneView view;
    const auto view_result = copy_view(view_input, view);
    if (view_result != NKS_OK)
        return view_result;
    const auto snapshot = nkscene::resolve_snapshot_handle(snapshot_handle);
    if (!snapshot)
        return NKS_ERROR_INVALID_HANDLE;

    auto &state = registry();
    std::lock_guard lock(state.mutex);
    const auto plan = state.plans.get(nkscene::unpack_handle(plan_handle));
    if (!plan)
        return NKS_ERROR_INVALID_HANDLE;
    const auto update = nkscene::refresh(*plan, *snapshot, view);
    *out_update = {};
    out_update->struct_size = sizeof(nkscene_render_update);
    out_update->plan_rebuilt = update.plan_rebuilt;
    out_update->geometry_rebuilt = update.geometry_rebuilt;
    out_update->patched_instances = update.patched_instances;
    out_update->patched_visibility = update.patched_visibility;
    out_update->patched_materials = update.patched_materials;
    out_update->rebuilt_batches = update.rebuilt_batches;
    out_update->updated_geometry_resources = update.updated_geometry_resources;
    out_update->updated_material_resources = update.updated_material_resources;
    out_update->invalidated_items = update.invalidated_items;
    out_update->patched_culling = update.patched_culling;
    out_update->visible_items = update.visible_items;
    out_update->culled_items = update.culled_items;
    return NKS_OK;
}

nkscene_result NKS_CALL nkscene_render_plan_pick(nkscene_render_plan plan_handle,
                                                 nkscene_snapshot snapshot_handle,
                                                 uint32_t primitive, const float world_position[3],
                                                 float depth,
                                                 nkscene_render_pick_result *out_result) {
    if (!world_position || !out_result)
        return NKS_ERROR_INVALID_ARGUMENT;
    const auto snapshot = nkscene::resolve_snapshot_handle(snapshot_handle);
    if (!snapshot)
        return NKS_ERROR_INVALID_HANDLE;
    std::shared_ptr<nkscene::RenderPlan> plan;
    {
        auto &state = registry();
        std::lock_guard lock(state.mutex);
        plan = state.plans.get(nkscene::unpack_handle(plan_handle));
        if (!plan)
            return NKS_ERROR_INVALID_HANDLE;
    }
    const auto result =
        nkscene::pick(*plan, *snapshot, primitive,
                      {world_position[0], world_position[1], world_position[2]}, depth);
    *out_result = {};
    out_result->occurrence.value = result.occurrence.value;
    out_result->source.value = result.source.value;
    out_result->subelement = result.subelement.value;
    out_result->world_position[0] = result.worldPosition.x;
    out_result->world_position[1] = result.worldPosition.y;
    out_result->world_position[2] = result.worldPosition.z;
    out_result->depth = result.depth;
    return NKS_OK;
}

nkscene_result NKS_CALL nkscene_render_spatial_index_create(
    nkscene_snapshot snapshot_handle, nkscene_render_spatial_index *out_index) {
    if (!out_index)
        return NKS_ERROR_INVALID_ARGUMENT;
    *out_index = 0;
    const auto snapshot = nkscene::resolve_snapshot_handle(snapshot_handle);
    if (!snapshot)
        return NKS_ERROR_INVALID_HANDLE;
    auto index = std::make_shared<nkscene::SceneSpatialIndex>(*snapshot);
    auto &state = registry();
    std::lock_guard lock(state.mutex);
    const auto handle = state.spatial_indices.create(std::move(index));
    if (!handle.valid())
        return NKS_ERROR_OUT_OF_MEMORY;
    *out_index = nkscene::pack_handle(handle);
    return NKS_OK;
}

void NKS_CALL nkscene_render_spatial_index_destroy(nkscene_render_spatial_index index) {
    auto &state = registry();
    std::lock_guard lock(state.mutex);
    state.spatial_indices.remove(nkscene::unpack_handle(index));
}

nkscene_result NKS_CALL nkscene_render_spatial_index_get_revision(
    nkscene_render_spatial_index index_handle, uint64_t *out_revision) {
    if (!out_revision)
        return NKS_ERROR_INVALID_ARGUMENT;
    auto &state = registry();
    std::lock_guard lock(state.mutex);
    const auto index = state.spatial_indices.get(nkscene::unpack_handle(index_handle));
    if (!index)
        return NKS_ERROR_INVALID_HANDLE;
    *out_revision = index->source_revision();
    return NKS_OK;
}

nkscene_result NKS_CALL nkscene_render_spatial_index_query_bounds(
    nkscene_render_spatial_index index_handle, const nkscene_bounds *bounds, uint64_t *out_count) {
    if (!bounds || !out_count)
        return NKS_ERROR_INVALID_ARGUMENT;
    auto &state = registry();
    std::lock_guard lock(state.mutex);
    const auto index = state.spatial_indices.get(nkscene::unpack_handle(index_handle));
    if (!index)
        return NKS_ERROR_INVALID_HANDLE;
    nkscene::Bounds query;
    for (uint32_t axis = 0; axis < 3; ++axis) {
        query.minimum[axis] = bounds->minimum[axis];
        query.maximum[axis] = bounds->maximum[axis];
    }
    query.valid = bounds->valid != 0;
    *out_count = index->query_bounds(query).size();
    return NKS_OK;
}

nkscene_result NKS_CALL nkscene_render_spatial_index_query_ray(
    nkscene_render_spatial_index index_handle, const nkscene_render_ray *ray, uint64_t *out_count) {
    if (!ray || !out_count)
        return NKS_ERROR_INVALID_ARGUMENT;
    auto &state = registry();
    std::lock_guard lock(state.mutex);
    const auto index = state.spatial_indices.get(nkscene::unpack_handle(index_handle));
    if (!index)
        return NKS_ERROR_INVALID_HANDLE;
    const nkscene::Ray query{{ray->origin[0], ray->origin[1], ray->origin[2]},
                             {ray->direction[0], ray->direction[1], ray->direction[2]}};
    *out_count = index->query_ray(query).size();
    return NKS_OK;
}

nkscene_result NKS_CALL nkscene_render_spatial_index_get_occurrence(
    nkscene_render_spatial_index index_handle, uint64_t result_index,
    nkscene_render_spatial_occurrence *out_result) {
    if (!out_result)
        return NKS_ERROR_INVALID_ARGUMENT;
    *out_result = {};
    auto &state = registry();
    std::lock_guard lock(state.mutex);
    const auto index = state.spatial_indices.get(nkscene::unpack_handle(index_handle));
    if (!index)
        return NKS_ERROR_INVALID_HANDLE;
    const auto occurrence = index->query_result(result_index);
    if (!occurrence.valid())
        return NKS_ERROR_INVALID_ARGUMENT;
    out_result->occurrence.value = occurrence.value;
    return NKS_OK;
}

nkscene_result NKS_CALL nkscene_render_spatial_index_pick_ray(
    nkscene_render_spatial_index index_handle, const nkscene_render_ray *ray,
    nkscene_render_pick_result *out_result) {
    if (!ray || !out_result)
        return NKS_ERROR_INVALID_ARGUMENT;
    auto &state = registry();
    std::lock_guard lock(state.mutex);
    const auto index = state.spatial_indices.get(nkscene::unpack_handle(index_handle));
    if (!index)
        return NKS_ERROR_INVALID_HANDLE;
    const nkscene::Ray query{{ray->origin[0], ray->origin[1], ray->origin[2]},
                             {ray->direction[0], ray->direction[1], ray->direction[2]}};
    const auto result = index->pick_ray(query);
    *out_result = {};
    out_result->occurrence.value = result.occurrence.value;
    out_result->source.value = result.source.value;
    out_result->subelement = result.subelement.value;
    out_result->world_position[0] = result.worldPosition.x;
    out_result->world_position[1] = result.worldPosition.y;
    out_result->world_position[2] = result.worldPosition.z;
    out_result->depth = result.depth;
    return NKS_OK;
}

nkscene_result NKS_CALL nkscene_render_executor_create(nkgpu_renderer renderer,
                                                       nkscene_render_executor *out_executor) {
    if (!out_executor)
        return NKS_ERROR_INVALID_ARGUMENT;
    *out_executor = 0;
    auto executor = std::make_shared<nkscene::NativeKitGpuExecutor>(renderer);
    auto &state = registry();
    std::lock_guard lock(state.mutex);
    const auto handle = state.executors.create(std::move(executor));
    if (!handle.valid())
        return NKS_ERROR_OUT_OF_MEMORY;
    *out_executor = nkscene::pack_handle(handle);
    return NKS_OK;
}

void NKS_CALL nkscene_render_executor_destroy(nkscene_render_executor executor) {
    auto &state = registry();
    std::lock_guard lock(state.mutex);
    state.executors.remove(nkscene::unpack_handle(executor));
}

nkscene_result NKS_CALL nkscene_render_executor_execute(nkscene_render_executor executor_handle,
                                                        nkscene_render_plan plan_handle,
                                                        nkscene_snapshot snapshot_handle,
                                                        nkscene_render_execution_stats *out_stats) {
    if (!out_stats)
        return NKS_ERROR_INVALID_ARGUMENT;
    if (out_stats->struct_size < sizeof(nkscene_render_execution_stats))
        return NKS_ERROR_INVALID_ARGUMENT;
    const auto snapshot = nkscene::resolve_snapshot_handle(snapshot_handle);
    if (!snapshot)
        return NKS_ERROR_INVALID_HANDLE;

    auto &state = registry();
    std::lock_guard lock(state.mutex);
    const auto executor = state.executors.get(nkscene::unpack_handle(executor_handle));
    const auto plan = state.plans.get(nkscene::unpack_handle(plan_handle));
    if (!executor || !plan)
        return NKS_ERROR_INVALID_HANDLE;

    const auto stats = executor->execute(*plan, *snapshot);
    *out_stats = {};
    out_stats->struct_size = sizeof(nkscene_render_execution_stats);
    out_stats->result = stats.result;
    out_stats->geometry_resources_created = stats.geometry_resources_created;
    out_stats->geometry_resources_updated = stats.geometry_resources_updated;
    out_stats->material_resources_created = stats.material_resources_created;
    out_stats->material_resources_updated = stats.material_resources_updated;
    out_stats->instance_buffers_created = stats.instance_buffers_created;
    out_stats->instance_records_updated = stats.instance_records_updated;
    out_stats->commands = stats.commands;
    out_stats->draw_calls = stats.draw_calls;
    return NKS_OK;
}

nkscene_result NKS_CALL nkscene_render_executor_get_last_result(
    nkscene_render_executor executor_handle, nkgpu_result *out_result) {
    if (!out_result)
        return NKS_ERROR_INVALID_ARGUMENT;
    auto &state = registry();
    std::lock_guard lock(state.mutex);
    const auto executor = state.executors.get(nkscene::unpack_handle(executor_handle));
    if (!executor)
        return NKS_ERROR_INVALID_HANDLE;
    *out_result = executor->last_result();
    return NKS_OK;
}

nkscene_result NKS_CALL nkscene_render_executor_pick_pixel(nkscene_render_executor executor_handle,
                                                           nkscene_render_plan plan_handle,
                                                           nkscene_snapshot snapshot_handle,
                                                           uint32_t width, uint32_t height,
                                                           uint32_t x, uint32_t y,
                                                           nkscene_render_pick_result *out_result) {
    if (!out_result)
        return NKS_ERROR_INVALID_ARGUMENT;
    const auto snapshot = nkscene::resolve_snapshot_handle(snapshot_handle);
    if (!snapshot)
        return NKS_ERROR_INVALID_HANDLE;

    auto &state = registry();
    std::lock_guard lock(state.mutex);
    const auto executor = state.executors.get(nkscene::unpack_handle(executor_handle));
    const auto plan = state.plans.get(nkscene::unpack_handle(plan_handle));
    if (!executor || !plan)
        return NKS_ERROR_INVALID_HANDLE;

    nkscene::PickResult result;
    const auto gpu_result = executor->pick_pixel(*plan, *snapshot, width, height, x, y, &result);
    *out_result = {};
    out_result->occurrence.value = result.occurrence.value;
    out_result->source.value = result.source.value;
    out_result->subelement = result.subelement.value;
    out_result->world_position[0] = result.worldPosition.x;
    out_result->world_position[1] = result.worldPosition.y;
    out_result->world_position[2] = result.worldPosition.z;
    out_result->depth = result.depth;
    return gpu_result == NKGPU_OK ? NKS_OK : NKS_ERROR_INVALID_STATE;
}

nkscene_result NKS_CALL nkscene_render_executor_pick_pixel_begin(
    nkscene_render_executor executor_handle, nkscene_render_plan plan_handle,
    nkscene_snapshot snapshot_handle, uint32_t width, uint32_t height, uint32_t x, uint32_t y,
    nkscene_render_pick_request *out_request) {
    if (!out_request)
        return NKS_ERROR_INVALID_ARGUMENT;
    *out_request = 0;
    const auto snapshot = nkscene::resolve_snapshot_handle(snapshot_handle);
    if (!snapshot)
        return NKS_ERROR_INVALID_HANDLE;

    auto &state = registry();
    std::lock_guard lock(state.mutex);
    const auto executor = state.executors.get(nkscene::unpack_handle(executor_handle));
    const auto plan = state.plans.get(nkscene::unpack_handle(plan_handle));
    if (!executor || !plan)
        return NKS_ERROR_INVALID_HANDLE;

    std::shared_ptr<nkscene::GpuPickRequest> request;
    const auto gpu_result =
        executor->begin_pick_pixel(*plan, *snapshot, width, height, x, y, request);
    if (gpu_result != NKGPU_OK)
        return NKS_ERROR_INVALID_STATE;
    const auto handle = state.pick_requests.create(std::move(request));
    if (!handle.valid())
        return NKS_ERROR_OUT_OF_MEMORY;
    *out_request = nkscene::pack_handle(handle);
    return NKS_OK;
}

void NKS_CALL nkscene_render_pick_request_destroy(nkscene_render_pick_request request) {
    auto &state = registry();
    std::lock_guard lock(state.mutex);
    state.pick_requests.remove(nkscene::unpack_handle(request));
}

nkscene_result NKS_CALL nkscene_render_executor_pick_pixel_poll(
    nkscene_render_executor executor_handle, nkscene_render_pick_request request_handle,
    nkscene_render_plan plan_handle, nkscene_snapshot snapshot_handle, uint32_t *out_state,
    nkgpu_result *out_error, nkscene_render_pick_result *out_result) {
    if (!out_state || !out_error || !out_result)
        return NKS_ERROR_INVALID_ARGUMENT;
    *out_state = NKS_RENDER_PICK_FAILED;
    *out_error = NKGPU_ERROR_INVALID_HANDLE;
    *out_result = {};
    const auto snapshot = nkscene::resolve_snapshot_handle(snapshot_handle);
    if (!snapshot)
        return NKS_ERROR_INVALID_HANDLE;

    auto &state = registry();
    std::lock_guard lock(state.mutex);
    const auto executor = state.executors.get(nkscene::unpack_handle(executor_handle));
    const auto request = state.pick_requests.get(nkscene::unpack_handle(request_handle));
    const auto plan = state.plans.get(nkscene::unpack_handle(plan_handle));
    if (!executor || !request || !plan)
        return NKS_ERROR_INVALID_HANDLE;

    nkscene::PickResult result;
    *out_state = executor->poll_pick_pixel(*request, *plan, *snapshot, &result, out_error);
    out_result->occurrence.value = result.occurrence.value;
    out_result->source.value = result.source.value;
    out_result->subelement = result.subelement.value;
    out_result->world_position[0] = result.worldPosition.x;
    out_result->world_position[1] = result.worldPosition.y;
    out_result->world_position[2] = result.worldPosition.z;
    out_result->depth = result.depth;
    return NKS_OK;
}

} // extern "C"
