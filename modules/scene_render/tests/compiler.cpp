#include "nativekit_scene_render.h"

#include "scene_internal.hpp"

#include <algorithm>
#include <cassert>
#include <memory>
#include <vector>

namespace {

using nkscene::ChangeSet;
using nkscene::Scene;
using nkscene::Transaction;

nkscene::LocalTransform translated(float x) {
    nkscene::LocalTransform transform;
    transform.matrix[12] = x;
    return transform;
}

void geometry_payload_contract_is_validated() {
    auto scene = std::make_shared<Scene>();
    const auto valid_geometry = scene->reserve_geometry_id();
    auto &valid = scene->geometry_store().create(valid_geometry);
    valid.payload.vertices = {
        nkscene::GeometryVertex{{-1.0f, 0.0f, 0.0f}},
        nkscene::GeometryVertex{{1.0f, 0.0f, 0.0f}},
        nkscene::GeometryVertex{{0.0f, 1.0f, 0.0f}}};
    valid.payload.indices = {0, 1, 2};
    valid.subelements.ranges.push_back({0, 1, 7});
    assert(valid.payload.element_count() == 3);
    assert(valid.payload.indexed());
    assert(valid.subelements.id_for_primitive(0) == 7);

    const auto invalid_geometry = scene->reserve_geometry_id();
    auto &invalid = scene->geometry_store().create(invalid_geometry);
    invalid.payload.vertices = valid.payload.vertices;
    invalid.payload.indices = {0, 1, 3};

    const auto material = scene->reserve_material_id();
    scene->material_store().create(material);
    const auto occurrence = scene->reserve_occurrence_id();
    Transaction create(scene);
    create.add_create(occurrence);
    ChangeSet changes;
    assert(scene->commit(create, changes) == NKS_OK);
    create.close();
    Transaction configure(scene);
    configure.add_geometry(occurrence, invalid_geometry);
    configure.add_material(occurrence, material);
    assert(scene->commit(configure, changes) == NKS_OK);
    configure.close();

    const auto plan = nkscene::compile(scene->snapshot(), {});
    nkscene::NativeKitGpuExecutor executor;
    const auto stats = executor.execute(plan, scene->snapshot());
    assert(stats.result == NKGPU_ERROR_INVALID_ARGUMENT);
}

void resource_lifecycle_is_cache_safe() {
    auto scene = std::make_shared<Scene>();
    const auto geometry = scene->reserve_geometry_id();
    auto &geometry_resource = scene->geometry_store().create(geometry);
    geometry_resource.payload.vertices = {
        nkscene::GeometryVertex{{-1.0f, -1.0f, 0.0f}},
        nkscene::GeometryVertex{{1.0f, -1.0f, 0.0f}},
        nkscene::GeometryVertex{{0.0f, 1.0f, 0.0f}}};
    geometry_resource.payload.indices = {0, 1, 2};

    const auto material = scene->reserve_material_id();
    scene->material_store().create(material);
    const auto occurrence = scene->reserve_occurrence_id();
    Transaction create(scene);
    create.add_create(occurrence);
    ChangeSet changes;
    assert(scene->commit(create, changes) == NKS_OK);
    create.close();
    Transaction configure(scene);
    configure.add_geometry(occurrence, geometry);
    configure.add_material(occurrence, material);
    assert(scene->commit(configure, changes) == NKS_OK);
    configure.close();

    const auto view = nkscene::SceneView{};
    auto plan = nkscene::compile(scene->snapshot(), view);
    ChangeSet no_changes;
    nkscene::NativeKitGpuExecutor executor;
    auto stats = executor.execute(plan, scene->snapshot());
    assert(stats.result == NKGPU_OK);
    assert(stats.geometry_resources_created == 1);
    assert(stats.material_resources_created == 1);

    auto &non_indexed = scene->geometry_store().create(geometry);
    non_indexed.payload.indices.clear();
    auto update = nkscene::update(plan, scene->snapshot(), no_changes, view);
    assert(!update.plan_rebuilt);
    assert(update.updated_geometry_resources == 1);
    assert(update.updated_material_resources == 0);
    stats = executor.execute(plan, scene->snapshot());
    assert(stats.result == NKGPU_OK);
    assert(stats.geometry_resources_created == 0);
    assert(stats.geometry_resources_updated == 1);

    auto &indexed = scene->geometry_store().create(geometry);
    indexed.payload.indices = {0, 1, 2};
    update = nkscene::update(plan, scene->snapshot(), no_changes, view);
    assert(!update.plan_rebuilt);
    assert(update.updated_geometry_resources == 1);
    assert(update.updated_material_resources == 0);
    stats = executor.execute(plan, scene->snapshot());
    assert(stats.result == NKGPU_OK);
    assert(stats.geometry_resources_created == 0);
    assert(stats.geometry_resources_updated == 1);

    const auto vertices = indexed.payload.vertices;
    assert(scene->geometry_store().destroy(geometry));
    update = nkscene::update(plan, scene->snapshot(), no_changes, view);
    assert(update.plan_rebuilt);
    assert(update.invalidated_items == 1);
    assert(plan.items().empty());
    stats = executor.execute(plan, scene->snapshot());
    assert(stats.result == NKGPU_OK);
    assert(stats.geometry_resources_created == 0);
    assert(stats.geometry_resources_updated == 0);

    auto &restored_geometry = scene->geometry_store().create(geometry);
    restored_geometry.payload.vertices = vertices;
    restored_geometry.payload.indices.clear();
    plan = nkscene::compile(scene->snapshot(), view);
    assert(plan.items().size() == 1);
    stats = executor.execute(plan, scene->snapshot());
    assert(stats.result == NKGPU_OK);
    assert(stats.geometry_resources_created == 1);
    assert(stats.geometry_resources_updated == 0);

    scene->material_store().create(material);
    update = nkscene::update(plan, scene->snapshot(), no_changes, view);
    assert(!update.plan_rebuilt);
    assert(update.updated_geometry_resources == 0);
    assert(update.updated_material_resources == 1);
    stats = executor.execute(plan, scene->snapshot());
    assert(stats.result == NKGPU_OK);
    assert(stats.material_resources_created == 0);
    assert(stats.material_resources_updated == 1);

    assert(scene->material_store().destroy(material));
    update = nkscene::update(plan, scene->snapshot(), no_changes, view);
    assert(update.plan_rebuilt);
    assert(update.invalidated_items == 1);
    assert(plan.items().empty());
    stats = executor.execute(plan, scene->snapshot());
    assert(stats.result == NKGPU_OK);
    assert(stats.material_resources_created == 0);
    assert(stats.material_resources_updated == 0);

    scene->material_store().create(material);
    plan = nkscene::compile(scene->snapshot(), view);
    stats = executor.execute(plan, scene->snapshot());
    assert(stats.result == NKGPU_OK);
    assert(stats.material_resources_created == 1);
    assert(stats.material_resources_updated == 0);

    const auto missing_geometry = scene->reserve_geometry_id();
    Transaction invalid_geometry(scene);
    invalid_geometry.add_geometry(occurrence, missing_geometry);
    assert(scene->commit(invalid_geometry, changes) == NKS_OK);
    invalid_geometry.close();
    update = nkscene::update(plan, scene->snapshot(), changes, view);
    assert(update.plan_rebuilt);
    assert(update.invalidated_items == 1);
    assert(plan.items().empty());

    Transaction restore_geometry(scene);
    restore_geometry.add_geometry(occurrence, geometry);
    assert(scene->commit(restore_geometry, changes) == NKS_OK);
    restore_geometry.close();
    plan = nkscene::compile(scene->snapshot(), view);
    assert(plan.items().size() == 1);

    const auto missing_material = scene->reserve_material_id();
    Transaction invalid_material(scene);
    invalid_material.add_material(occurrence, missing_material);
    assert(scene->commit(invalid_material, changes) == NKS_OK);
    invalid_material.close();
    update = nkscene::update(plan, scene->snapshot(), changes, view);
    assert(update.plan_rebuilt);
    assert(update.invalidated_items == 1);
    assert(plan.items().empty());
}

void scene_views_are_hierarchy_aware() {
    auto scene = std::make_shared<Scene>();
    const auto geometry = scene->reserve_geometry_id();
    scene->geometry_store().create(geometry);
    const auto material_one = scene->reserve_material_id();
    const auto material_two = scene->reserve_material_id();
    scene->material_store().create(material_one);
    scene->material_store().create(material_two);

    const auto group = scene->reserve_occurrence_id();
    const auto leaf = scene->reserve_occurrence_id();
    const auto sibling = scene->reserve_occurrence_id();
    Transaction create(scene);
    create.add_create(group);
    create.add_create(leaf);
    create.add_create(sibling);
    ChangeSet changes;
    assert(scene->commit(create, changes) == NKS_OK);
    create.close();

    Transaction configure(scene);
    configure.add_parent(leaf, group);
    configure.add_geometry(leaf, geometry);
    configure.add_material(leaf, material_one);
    configure.add_geometry(sibling, geometry);
    configure.add_material(sibling, material_one);
    assert(scene->commit(configure, changes) == NKS_OK);
    configure.close();

    const auto before = scene->snapshot();
    const auto find_item = [](const nkscene::RenderPlan &plan,
                              nkscene::OccurrenceId occurrence) {
        return std::find_if(plan.items().begin(), plan.items().end(),
                            [occurrence](const nkscene::RenderItem &item) {
                                return item.occurrence == occurrence;
                            });
    };

    nkscene::SceneView full_view;
    auto plan = nkscene::compile(before, full_view);
    assert(plan.items().size() == 2);
    assert(find_item(plan, leaf) != plan.items().end());
    assert(!nkscene::has_render_flag(find_item(plan, leaf)->flags,
                                     nkscene::RenderFlags::Hidden));

    nkscene::SceneView subtree_view;
    subtree_view.root = group;
    plan = nkscene::compile(before, subtree_view);
    assert(plan.items().size() == 1);
    assert(plan.items().front().occurrence == leaf);

    nkscene::SceneView hidden_view = subtree_view;
    hidden_view.visibility_overrides.push_back({group, false});
    plan = nkscene::compile(before, hidden_view);
    assert(plan.items().size() == 1);
    assert(nkscene::has_render_flag(plan.items().front().flags,
                                    nkscene::RenderFlags::Hidden));

    nkscene::SceneView material_view = subtree_view;
    material_view.material_overrides.push_back({leaf, material_two});
    plan = nkscene::compile(before, material_view);
    assert(plan.items().front().material == material_two);
    assert(before.find(leaf)->material == material_one);
    assert(before.revision() == scene->revision());
    assert(scene->revision_counters().material == before.revisions().material);
    assert(scene->material_store().find(material_two)->revision == 1);

    plan = nkscene::compile(before, full_view);
    Transaction hide_parent(scene);
    hide_parent.add_visibility(group, false);
    assert(scene->commit(hide_parent, changes) == NKS_OK);
    hide_parent.close();
    const auto hidden_snapshot = scene->snapshot();
    auto update = nkscene::update(plan, hidden_snapshot, changes, full_view);
    assert(!update.plan_rebuilt);
    assert(update.patched_visibility == 1);
    assert(nkscene::has_render_flag(find_item(plan, leaf)->flags,
                                    nkscene::RenderFlags::Hidden));

    nkscene::SceneView inspection_view;
    inspection_view.include_invisible = true;
    plan = nkscene::compile(hidden_snapshot, inspection_view);
    assert(!nkscene::has_render_flag(find_item(plan, leaf)->flags,
                                     nkscene::RenderFlags::Hidden));

    ChangeSet no_changes;
    update = nkscene::update(plan, hidden_snapshot, no_changes, subtree_view);
    assert(update.plan_rebuilt);
    assert(plan.items().size() == 1);
    assert(plan.items().front().occurrence == leaf);
}

void scene_view_camera_culling_is_incremental() {
    auto scene = std::make_shared<Scene>();
    const auto geometry = scene->reserve_geometry_id();
    auto &geometry_resource = scene->geometry_store().create(geometry);
    geometry_resource.bounds.valid = true;
    geometry_resource.bounds.minimum = {-0.25f, -0.25f, -0.25f};
    geometry_resource.bounds.maximum = {0.25f, 0.25f, 0.25f};
    const auto material = scene->reserve_material_id();
    scene->material_store().create(material);
    const auto inside = scene->reserve_occurrence_id();
    const auto outside = scene->reserve_occurrence_id();

    Transaction create(scene);
    create.add_create(inside);
    create.add_create(outside);
    ChangeSet changes;
    assert(scene->commit(create, changes) == NKS_OK);
    create.close();

    Transaction configure(scene);
    configure.add_geometry(inside, geometry);
    configure.add_material(inside, material);
    configure.add_geometry(outside, geometry);
    configure.add_material(outside, material);
    configure.add_transform(outside, translated(2.0f));
    assert(scene->commit(configure, changes) == NKS_OK);
    configure.close();

    nkscene::SceneView view;
    view.camera.enabled = true;
    const auto snapshot = scene->snapshot();
    auto plan = nkscene::compile(snapshot, view);
    assert(plan.items().size() == 2);
    assert(plan.visible_items() == 1);
    assert(plan.culled_items() == 1);
    const auto find_item = [&](nkscene::OccurrenceId id) {
        return std::find_if(plan.items().begin(), plan.items().end(),
                            [id](const nkscene::RenderItem &item) {
                                return item.occurrence == id;
                            });
    };
    assert(!nkscene::has_render_flag(find_item(inside)->flags,
                                     nkscene::RenderFlags::Culled));
    assert(nkscene::has_render_flag(find_item(outside)->flags,
                                    nkscene::RenderFlags::Culled));
    const auto compile_count = plan.compile_count();

    Transaction move_inside(scene);
    move_inside.add_transform(outside, translated(0.5f));
    assert(scene->commit(move_inside, changes) == NKS_OK);
    move_inside.close();
    const auto moved_snapshot = scene->snapshot();
    const auto update = nkscene::update(plan, moved_snapshot, changes, view);
    assert(!update.plan_rebuilt);
    assert(update.patched_instances == 1);
    assert(update.patched_culling == 1);
    assert(update.visible_items == 2);
    assert(update.culled_items == 0);
    assert(plan.compile_count() == compile_count);
    assert(!nkscene::has_render_flag(find_item(outside)->flags,
                                     nkscene::RenderFlags::Culled));
}

void scene_view_clip_planes_are_incremental() {
    auto scene = std::make_shared<Scene>();
    const auto geometry = scene->reserve_geometry_id();
    auto &geometry_resource = scene->geometry_store().create(geometry);
    geometry_resource.bounds.valid = true;
    geometry_resource.bounds.minimum = {-0.25f, -0.25f, -0.25f};
    geometry_resource.bounds.maximum = {0.25f, 0.25f, 0.25f};
    const auto material = scene->reserve_material_id();
    scene->material_store().create(material);
    const auto inside = scene->reserve_occurrence_id();
    const auto outside = scene->reserve_occurrence_id();

    Transaction create(scene);
    create.add_create(inside);
    create.add_create(outside);
    ChangeSet changes;
    assert(scene->commit(create, changes) == NKS_OK);
    create.close();

    Transaction configure(scene);
    configure.add_geometry(inside, geometry);
    configure.add_material(inside, material);
    configure.add_geometry(outside, geometry);
    configure.add_material(outside, material);
    configure.add_transform(outside, translated(2.0f));
    assert(scene->commit(configure, changes) == NKS_OK);
    configure.close();

    nkscene::SceneView clipped_view;
    clipped_view.clip_planes.push_back({{-1.0f, 0.0f, 0.0f}, 0.0f, true});
    const auto snapshot = scene->snapshot();
    auto plan = nkscene::compile(snapshot, clipped_view);
    assert(plan.items().size() == 2);
    assert(plan.visible_items() == 1);
    assert(plan.culled_items() == 1);
    const auto compile_count = plan.compile_count();

    auto relaxed_view = clipped_view;
    relaxed_view.clip_planes.front().distance = 2.0f;
    const auto update = nkscene::refresh(plan, snapshot, relaxed_view);
    assert(!update.plan_rebuilt);
    assert(update.patched_culling == 1);
    assert(update.visible_items == 2);
    assert(update.culled_items == 0);
    assert(plan.compile_count() == compile_count);

    relaxed_view.clip_planes.clear();
    const auto cleared = nkscene::refresh(plan, snapshot, relaxed_view);
    assert(!cleared.plan_rebuilt);
    assert(cleared.patched_culling == 0);
    assert(cleared.visible_items == 2);
}

} // namespace

int main() {
    geometry_payload_contract_is_validated();
    resource_lifecycle_is_cache_safe();
    scene_views_are_hierarchy_aware();
    scene_view_camera_culling_is_incremental();
    scene_view_clip_planes_are_incremental();
    constexpr std::size_t count = 50000;
    auto scene = std::make_shared<Scene>();
    const auto geometry = scene->reserve_geometry_id();
    scene->geometry_store().create(geometry);
    std::vector<nkscene::MaterialId> materials;
    for (int index = 0; index < 4; ++index) {
        const auto material = scene->reserve_material_id();
        materials.push_back(material);
        scene->material_store().create(material);
    }

    std::vector<nkscene::OccurrenceId> occurrences;
    occurrences.reserve(count);
    Transaction create(scene);
    for (std::size_t index = 0; index < count; ++index) {
        const auto occurrence = scene->reserve_occurrence_id();
        occurrences.push_back(occurrence);
        create.add_create(occurrence);
    }
    ChangeSet changes;
    assert(scene->commit(create, changes) == NKS_OK);
    create.close();

    Transaction configure(scene);
    for (std::size_t index = 0; index < count; ++index) {
        configure.add_geometry(occurrences[index], geometry);
        configure.add_material(occurrences[index], materials[index % materials.size()]);
    }
    assert(scene->commit(configure, changes) == NKS_OK);
    configure.close();

    const auto snapshot = scene->snapshot();
    nkscene::SceneView view;
    auto plan = nkscene::compile(snapshot, view);
    assert(plan.items().size() == count);
    assert(plan.batches().size() == materials.size());
    const auto compile_count = plan.compile_count();
    const auto first = occurrences.front();
    const auto first_item = plan.items().front();
    assert(plan.transforms()[first_item.transformIndex].transform.matrix[12] == 0.0f);

    Transaction move(scene);
    move.add_transform(first, translated(5.0f));
    assert(scene->commit(move, changes) == NKS_OK);
    move.close();
    const auto moved_snapshot = scene->snapshot();
    auto update = nkscene::update(plan, moved_snapshot, changes, view);
    assert(!update.plan_rebuilt);
    assert(!update.geometry_rebuilt);
    assert(update.patched_instances == 1);
    assert(plan.compile_count() == compile_count);
    const auto moved_item = plan.items().front();
    assert(plan.transforms()[moved_item.transformIndex].transform.matrix[12] == 5.0f);

    Transaction change_material(scene);
    change_material.add_material(first, materials[1]);
    assert(scene->commit(change_material, changes) == NKS_OK);
    change_material.close();
    update = nkscene::update(plan, scene->snapshot(), changes, view);
    assert(!update.plan_rebuilt);
    assert(update.patched_materials == 1);
    assert(update.rebuilt_batches != 0);

    Transaction hide(scene);
    for (std::size_t index = 0; index < 1000; ++index)
        hide.add_visibility(occurrences[index], false);
    assert(scene->commit(hide, changes) == NKS_OK);
    hide.close();
    update = nkscene::update(plan, scene->snapshot(), changes, view);
    assert(!update.plan_rebuilt);
    assert(update.patched_visibility == 1000);
    assert(plan.items().size() == count);

    const auto hidden_snapshot = scene->snapshot();
    const auto scene_revision = scene->revision();
    nkscene::SceneView filtered_view;
    for (std::size_t index = 1000; index < 2000; ++index)
        filtered_view.visibility_overrides.push_back({occurrences[index], false});
    nkscene::ChangeSet no_changes;
    update = nkscene::update(plan, hidden_snapshot, no_changes, filtered_view);
    assert(!update.plan_rebuilt);
    assert(update.patched_visibility == 1000);
    assert(update.visible_items == count - 2000);
    assert(plan.items().size() == count);
    assert(scene->revision() == scene_revision);

    auto selected_view = filtered_view;
    selected_view.material_overrides.push_back({occurrences[2000], materials[3]});
    update = nkscene::update(plan, hidden_snapshot, no_changes, selected_view);
    assert(!update.plan_rebuilt);
    assert(update.patched_materials == 1);
    assert(update.rebuilt_batches != 0);
    assert(scene->revision() == scene_revision);

    nkscene::NativeKitGpuExecutor executor;
    auto gpu_stats = executor.execute(plan, scene->snapshot());
    assert(gpu_stats.geometry_resources_created == 1);
    assert(gpu_stats.material_resources_created == materials.size());
    assert(gpu_stats.commands == count - 2000);
    assert(executor.commands().size() == count - 2000);
    gpu_stats = executor.execute(plan, scene->snapshot());
    assert(gpu_stats.geometry_resources_created == 0);
    assert(gpu_stats.material_resources_created == 0);
    scene->geometry_store().create(geometry);
    gpu_stats = executor.execute(plan, scene->snapshot());
    assert(gpu_stats.geometry_resources_updated == 1);

    const auto pick_result = nkscene::pick(
        plan, scene->snapshot(), 0, nkscene::Vec3{1.0f, 2.0f, 3.0f}, 0.5f);
    assert(pick_result.occurrence.valid());
    assert(pick_result.subelement.valid());
    assert(pick_result.worldPosition.x == 1.0f);
    return 0;
}
