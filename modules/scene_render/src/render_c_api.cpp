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
};

RenderRegistry &registry() {
    static RenderRegistry value;
    return value;
}

nkscene_result copy_view(const nkscene_render_view *input, nkscene::SceneView &output) {
    if (!input)
        return NKS_OK;
    if (input->struct_size < sizeof(nkscene_render_view))
        return NKS_ERROR_INVALID_ARGUMENT;
    if ((input->visibility_override_count != 0 && !input->visibility_overrides) ||
        (input->material_override_count != 0 && !input->material_overrides))
        return NKS_ERROR_INVALID_ARGUMENT;

    output.root = {input->root.value};
    output.include_invisible = input->include_invisible != 0;
    output.visibility_overrides.reserve(input->visibility_override_count);
    for (uint32_t index = 0; index < input->visibility_override_count; ++index) {
        const auto &value = input->visibility_overrides[index];
        output.visibility_overrides.push_back(
            {{value.occurrence.value}, value.visible != 0});
    }
    output.material_overrides.reserve(input->material_override_count);
    for (uint32_t index = 0; index < input->material_override_count; ++index) {
        const auto &value = input->material_overrides[index];
        output.material_overrides.push_back(
            {{value.occurrence.value}, {value.material.value}});
    }
    return NKS_OK;
}

} // namespace

extern "C" {

nkscene_result NKS_CALL nkscene_render_plan_compile(
    nkscene_snapshot snapshot_handle, const nkscene_render_view *view_input,
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

nkscene_result NKS_CALL nkscene_render_plan_get_item_count(
    nkscene_render_plan plan_handle, uint64_t *out_count) {
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

nkscene_result NKS_CALL nkscene_render_plan_update(
    nkscene_render_plan plan_handle, nkscene_snapshot snapshot_handle,
    nkscene_change_set changes_handle, const nkscene_render_view *view_input,
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
    return NKS_OK;
}

nkscene_result NKS_CALL nkscene_render_plan_pick(
    nkscene_render_plan plan_handle, nkscene_snapshot snapshot_handle, uint32_t primitive,
    const float world_position[3], float depth, nkscene_render_pick_result *out_result) {
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
    const auto result = nkscene::pick(
        *plan, *snapshot, primitive, {world_position[0], world_position[1], world_position[2]},
        depth);
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

} // extern "C"
