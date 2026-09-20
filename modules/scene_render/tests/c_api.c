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
    nkscene_render_plan plan = {0};
    assert(nkscene_render_plan_compile(snapshot, &view, &plan) == NKS_OK);
    uint64_t item_count = 0;
    assert(nkscene_render_plan_get_item_count(plan, &item_count) == NKS_OK);
    assert(item_count == 1);

    nkscene_render_pick_result pick = {0};
    const float world_position[3] = {0.0f, 0.0f, 0.0f};
    assert(nkscene_render_plan_pick(plan, snapshot, 0, world_position, 0.5f, &pick) == NKS_OK);
    assert(pick.occurrence.value == occurrence.value);
    assert(pick.subelement == 1);

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
    assert(update.updated_geometry_resources == 0);
    assert(update.updated_material_resources == 0);

    nkscene_render_plan_destroy(plan);
    assert(nkscene_render_plan_get_item_count(plan, &item_count) == NKS_ERROR_INVALID_HANDLE);
    nkscene_snapshot_destroy(snapshot);
    assert(nkscene_snapshot_get_revision(snapshot, &revision) == NKS_ERROR_INVALID_HANDLE);
    nkscene_change_set_destroy(changes);
    assert(nkscene_change_set_get_revision(changes, &revision) == NKS_ERROR_INVALID_HANDLE);
    nkscene_geometry_destroy(scene, geometry);
    nkscene_material_destroy(scene, material);
    nkscene_scene_destroy(scene);
    return 0;
}
