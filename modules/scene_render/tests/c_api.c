#include "nativekit_scene_render.h"

#include <assert.h>
#include <stddef.h>

int main(void) {
    nkscene_scene scene = NKS_INVALID_SCENE;
    assert(nkscene_scene_create(&scene) == NKS_OK);

    nkscene_geometry_id geometry = NKS_INVALID_GEOMETRY;
    nkscene_material_id material = NKS_INVALID_MATERIAL;
    assert(nkscene_geometry_create(scene, &geometry) == NKS_OK);
    assert(nkscene_material_create(scene, &material) == NKS_OK);

    const nkscene_geometry_vertex vertices[] = {
        {{0.0f, 0.0f, 0.0f}},
        {{1.0f, 0.0f, 0.0f}},
        {{0.0f, 1.0f, 0.0f}},
    };
    nkscene_geometry_data geometry_data = {0};
    geometry_data.struct_size = sizeof(geometry_data);
    geometry_data.vertices = vertices;
    geometry_data.vertex_count = 3;
    geometry_data.bounds.valid = 1;
    geometry_data.bounds.maximum[0] = 1.0f;
    geometry_data.bounds.maximum[1] = 1.0f;
    assert(nkscene_geometry_set_data(scene, geometry, &geometry_data) == NKS_OK);

    nkscene_material_data material_data = {0};
    material_data.struct_size = sizeof(material_data);
    material_data.base_color[0] = 0.2f;
    material_data.base_color[1] = 0.4f;
    material_data.base_color[2] = 0.8f;
    material_data.base_color[3] = 1.0f;
    material_data.opacity = 1.0f;
    material_data.flags = NKS_MATERIAL_OPAQUE;
    assert(nkscene_material_set_data(scene, material, &material_data) == NKS_OK);

    nkscene_transaction transaction = NKS_INVALID_TRANSACTION;
    assert(nkscene_transaction_begin(scene, &transaction) == NKS_OK);
    nkscene_occurrence_id occurrence = NKS_INVALID_OCCURRENCE;
    assert(nkscene_tx_create_occurrence(transaction, &occurrence) == NKS_OK);
    assert(nkscene_tx_set_geometry(transaction, occurrence, geometry) == NKS_OK);
    assert(nkscene_tx_set_material(transaction, occurrence, material) == NKS_OK);
    nkscene_change_set changes = {0};
    assert(nkscene_transaction_commit_with_changes(transaction, &changes) == NKS_OK);

    nkscene_snapshot snapshot = {0};
    assert(nkscene_scene_snapshot(scene, &snapshot) == NKS_OK);
    uint64_t revision = 0;
    assert(nkscene_snapshot_get_revision(snapshot, &revision) == NKS_OK);
    assert(revision == 1);
    assert(nkscene_change_set_get_revision(changes, &revision) == NKS_OK);
    assert(revision == 1);

    nkscene_render_view view = {0};
    view.struct_size = sizeof(view);
    view.camera.enabled = 1;
    view.camera.view_projection.matrix[0] = 1.0f;
    view.camera.view_projection.matrix[5] = 1.0f;
    view.camera.view_projection.matrix[10] = 1.0f;
    view.camera.view_projection.matrix[15] = 1.0f;
    nkscene_render_plan plan = {0};
    assert(nkscene_render_plan_compile(snapshot, &view, &plan) == NKS_OK);
    uint64_t item_count = 0;
    assert(nkscene_render_plan_get_item_count(plan, &item_count) == NKS_OK);
    assert(item_count == 1);

    nkgpu_renderer renderer = {0};
    nkscene_render_executor executor = 0;
    assert(nkscene_render_executor_create(renderer, &executor) == NKS_OK);
    nkscene_render_pick_request pick_request = 0;
    assert(nkscene_render_executor_pick_pixel_begin(
               executor, plan, snapshot, 1, 1, 0, 0, &pick_request) == NKS_ERROR_INVALID_STATE);
    assert(pick_request == 0);
    nkscene_render_pick_request_destroy(pick_request);
    nkscene_render_execution_stats execution = {0};
    execution.struct_size = sizeof(execution);
    assert(nkscene_render_executor_execute(executor, plan, snapshot, &execution) == NKS_OK);
    assert(execution.result == NKGPU_OK);
    assert(execution.geometry_resources_created == 1);
    assert(execution.commands == 1);
    assert(execution.draw_calls == 1);
    nkgpu_result last_result = NKGPU_ERROR_UNKNOWN;
    assert(nkscene_render_executor_get_last_result(executor, &last_result) == NKS_OK);
    assert(last_result == NKGPU_OK);

    nkscene_render_update refresh = {0};
    refresh.struct_size = sizeof(refresh);
    view.include_invisible = 1;
    assert(nkscene_render_plan_refresh(plan, snapshot, &view, &refresh) == NKS_OK);
    assert(refresh.plan_rebuilt == 0);
    assert(refresh.patched_visibility == 0);
    refresh.struct_size = sizeof(refresh);
    assert(nkscene_render_plan_refresh(plan, snapshot, &view, &refresh) == NKS_OK);
    assert(refresh.plan_rebuilt == 0);

    nkscene_render_pick_result pick = {0};
    const float world_position[3] = {0.0f, 0.0f, 0.0f};
    assert(nkscene_render_plan_pick(plan, snapshot, 0, world_position, 0.5f, &pick) == NKS_OK);
    assert(pick.occurrence.value == occurrence.value);
    assert(pick.subelement == 1);

    nkscene_render_spatial_index spatial_index = 0;
    assert(nkscene_render_spatial_index_create(snapshot, &spatial_index) == NKS_OK);
    assert(nkscene_render_spatial_index_get_revision(spatial_index, &revision) == NKS_OK);
    nkscene_bounds query_bounds = {0};
    query_bounds.valid = 1;
    query_bounds.minimum[0] = -1.0f;
    query_bounds.minimum[1] = -1.0f;
    query_bounds.minimum[2] = -1.0f;
    query_bounds.maximum[0] = 2.0f;
    query_bounds.maximum[1] = 2.0f;
    query_bounds.maximum[2] = 1.0f;
    uint64_t query_count = 0;
    assert(nkscene_render_spatial_index_query_bounds(
               spatial_index, &query_bounds, &query_count) == NKS_OK);
    assert(query_count == 1);
    nkscene_render_spatial_occurrence query_result = {0};
    assert(nkscene_render_spatial_index_get_occurrence(
               spatial_index, 0, &query_result) == NKS_OK);
    assert(query_result.occurrence.value == occurrence.value);
    nkscene_render_ray ray = {{0.25f, 0.25f, 1.0f}, {0.0f, 0.0f, -1.0f}};
    assert(nkscene_render_spatial_index_query_ray(spatial_index, &ray, &query_count) == NKS_OK);
    assert(query_count == 1);
    assert(nkscene_render_spatial_index_pick_ray(spatial_index, &ray, &pick) == NKS_OK);
    assert(pick.occurrence.value == occurrence.value);
    assert(pick.subelement == 1);
    assert(pick.depth == 1.0f);
    nkscene_render_spatial_index_destroy(spatial_index);
    assert(nkscene_render_spatial_index_get_revision(spatial_index, &revision) ==
           NKS_ERROR_INVALID_HANDLE);

    nkscene_snapshot_destroy(snapshot);
    nkscene_change_set_destroy(changes);

    assert(nkscene_transaction_begin(scene, &transaction) == NKS_OK);
    nkscene_transform transform = {{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        2.0f, 0.0f, 0.0f, 1.0f,
    }};
    assert(nkscene_tx_set_transform(transaction, occurrence, &transform) == NKS_OK);
    changes = 0;
    assert(nkscene_transaction_commit_with_changes(transaction, &changes) == NKS_OK);
    assert(nkscene_scene_snapshot(scene, &snapshot) == NKS_OK);

    nkscene_render_update update = {0};
    update.struct_size = sizeof(update);
    assert(nkscene_render_plan_update(plan, snapshot, changes, &view, &update) == NKS_OK);
    assert(update.plan_rebuilt == 0);
    assert(update.geometry_rebuilt == 0);
    assert(update.patched_instances == 1);
    assert(update.patched_culling == 1);
    assert(update.visible_items == 0);
    assert(update.culled_items == 1);
    assert(update.updated_geometry_resources == 0);
    assert(update.updated_material_resources == 0);

    nkscene_render_plan_destroy(plan);
    assert(nkscene_render_plan_get_item_count(plan, &item_count) == NKS_ERROR_INVALID_HANDLE);
    nkscene_render_executor_destroy(executor);
    assert(nkscene_render_executor_get_last_result(executor, &last_result) == NKS_ERROR_INVALID_HANDLE);
    nkscene_snapshot_destroy(snapshot);
    assert(nkscene_snapshot_get_revision(snapshot, &revision) == NKS_ERROR_INVALID_HANDLE);
    nkscene_change_set_destroy(changes);
    assert(nkscene_change_set_get_revision(changes, &revision) == NKS_ERROR_INVALID_HANDLE);
    nkscene_geometry_destroy(scene, geometry);
    nkscene_material_destroy(scene, material);
    nkscene_scene_destroy(scene);
    return 0;
}
