#include "nativekit_scene_render.h"

#include "scene_internal.hpp"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <memory>
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using nkscene::ChangeSet;
using nkscene::RenderPlan;
using nkscene::RenderUpdate;
using nkscene::Scene;
using nkscene::Transaction;

struct TimedUpdate {
    const char *name;
    double milliseconds;
    ChangeSet changes;
    RenderUpdate render;
};

struct TimedViewUpdate {
    const char *name;
    double milliseconds;
    RenderUpdate render;
};

nkscene::LocalTransform translated(float x) {
    nkscene::LocalTransform transform;
    transform.matrix[12] = x;
    return transform;
}

TimedUpdate run(const char *name, const std::shared_ptr<Scene> &scene, Transaction &transaction,
               RenderPlan &plan, const nkscene::SceneView &view) {
    const auto start = Clock::now();
    ChangeSet changes;
    assert(scene->commit(transaction, changes) == NKS_OK);
    transaction.close();
    const auto snapshot = scene->snapshot();
    const auto render = nkscene::update(plan, snapshot, changes, view);
    const auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - start);
    return {name, elapsed.count(), std::move(changes), render};
}

void print(const TimedUpdate &result) {
    const auto &scene = result.changes.stats;
    std::printf(
        "%-18s %8.3f ms  scene(changes=%zu resources=%zu world=%zu bounds=%zu full=%zu) "
        "render(rebuild=%d geometry=%d instances=%zu visibility=%zu materials=%zu batches=%zu)\n",
        result.name, result.milliseconds, scene.changed_occurrences, scene.changed_resources,
        scene.dirty_world_transforms, scene.dirty_bounds, scene.full_rebuilds,
        result.render.plan_rebuilt, result.render.geometry_rebuilt,
        result.render.patched_instances,
        result.render.patched_visibility, result.render.patched_materials,
        result.render.rebuilt_batches);
}

TimedViewUpdate run_view(const char *name, RenderPlan &plan,
                         const nkscene::SceneSnapshot &snapshot,
                         const nkscene::SceneView &view) {
    const auto start = Clock::now();
    const auto render = nkscene::refresh(plan, snapshot, view);
    const auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - start);
    return {name, elapsed.count(), render};
}

void print(const TimedViewUpdate &result) {
    std::printf(
        "%-18s %8.3f ms  view(rebuild=%d geometry=%d instances=%zu visibility=%zu "
        "materials=%zu resources(g=%zu m=%zu) culling=%zu batches=%zu visible=%zu culled=%zu)\n",
        result.name, result.milliseconds, result.render.plan_rebuilt,
        result.render.geometry_rebuilt, result.render.patched_instances,
        result.render.patched_visibility, result.render.patched_materials,
        result.render.updated_geometry_resources, result.render.updated_material_resources,
        result.render.patched_culling, result.render.rebuilt_batches,
        result.render.visible_items, result.render.culled_items);
}

} // namespace

int main() {
constexpr std::size_t occurrence_count = 50000;
constexpr std::size_t group_count = 50;
constexpr std::size_t leaf_count = occurrence_count - group_count;
constexpr std::size_t source_count = 500;

    auto scene = std::make_shared<Scene>();
    const auto geometry = scene->reserve_geometry_id();
    auto &geometry_resource = scene->geometry_store().create(geometry);
    geometry_resource.bounds.valid = true;
    geometry_resource.bounds.minimum = {-1.0f, -1.0f, 0.0f};
    geometry_resource.bounds.maximum = {1.0f, 1.0f, 0.0f};
    geometry_resource.payload.vertices = {
        nkscene::GeometryVertex{{-1.0f, -1.0f, 0.0f}},
        nkscene::GeometryVertex{{1.0f, -1.0f, 0.0f}},
        nkscene::GeometryVertex{{0.0f, 1.0f, 0.0f}}};

    std::vector<nkscene::MaterialId> materials;
    materials.reserve(4);
    for (int index = 0; index < 4; ++index) {
        const auto material = scene->reserve_material_id();
        materials.push_back(material);
        scene->material_store().create(material);
    }

    std::vector<nkscene::OccurrenceId> groups;
    std::vector<nkscene::OccurrenceId> leaves;
    groups.reserve(group_count);
    leaves.reserve(leaf_count);
    Transaction create(scene);
    for (std::size_t index = 0; index < group_count; ++index) {
        const auto id = scene->reserve_occurrence_id();
        groups.push_back(id);
        create.add_create(id);
    }
    for (std::size_t index = 0; index < leaf_count; ++index) {
        const auto id = scene->reserve_occurrence_id();
        leaves.push_back(id);
        create.add_create(id);
    }
    ChangeSet changes;
    assert(scene->commit(create, changes) == NKS_OK);
    create.close();

    Transaction configure(scene);
    for (std::size_t index = 0; index < leaves.size(); ++index) {
        configure.add_parent(leaves[index], groups[index % groups.size()]);
        configure.add_geometry(leaves[index], geometry);
        configure.add_material(leaves[index], materials[index % materials.size()]);
        configure.add_source_entity(leaves[index],
                                    nkscene::EntityId{(index % source_count) + 1});
    }
    assert(scene->commit(configure, changes) == NKS_OK);
    configure.close();

    const nkscene::SceneView view;
    auto plan = nkscene::compile(scene->snapshot(), view);
    assert(plan.items().size() == leaf_count);
    assert(plan.batches().size() == materials.size());

    const auto hierarchy_revision = scene->revision_counters().hierarchy;
    Transaction move_one(scene);
    move_one.add_transform(leaves[0], translated(1.0f));
    auto result = run("move one", scene, move_one, plan, view);
    assert(result.changes.stats.changed_occurrences == 1);
    assert(result.changes.stats.changed_resources == 0);
    assert(result.changes.stats.dirty_world_transforms == 1);
    assert(result.changes.stats.dirty_bounds == 1);
    assert(result.changes.revisions.hierarchy == hierarchy_revision);
    assert(!result.render.plan_rebuilt);
    assert(!result.render.geometry_rebuilt);
    assert(result.render.patched_instances == 1);
    assert(result.render.patched_visibility == 0);
    assert(result.render.patched_materials == 0);
    assert(result.render.rebuilt_batches == 0);
    print(result);

    Transaction move_hundred(scene);
    for (std::size_t index = 0; index < 100; ++index)
        move_hundred.add_transform(leaves[index], translated(static_cast<float>(index)));
    result = run("move 100", scene, move_hundred, plan, view);
    assert(result.changes.stats.changed_occurrences == 100);
    assert(result.changes.stats.dirty_world_transforms == 100);
    assert(!result.render.plan_rebuilt);
    assert(!result.render.geometry_rebuilt);
    assert(result.render.patched_instances == 100);
    print(result);

    Transaction change_material(scene);
    change_material.add_material(leaves[0], materials[1]);
    result = run("material one", scene, change_material, plan, view);
    assert(result.changes.stats.changed_occurrences == 1);
    assert(!result.render.plan_rebuilt);
    assert(!result.render.geometry_rebuilt);
    assert(result.render.patched_instances == 0);
    assert(result.render.patched_materials == 1);
    assert(result.render.rebuilt_batches == materials.size());
    print(result);

    Transaction hide(scene);
    for (std::size_t index = 0; index < 1000; ++index)
        hide.add_visibility(leaves[index], false);
    result = run("hide 1000", scene, hide, plan, view);
    assert(result.changes.stats.changed_occurrences == 1000);
    assert(result.changes.stats.dirty_world_transforms == 0);
    assert(result.changes.stats.dirty_bounds == 0);
    assert(!result.render.plan_rebuilt);
    assert(!result.render.geometry_rebuilt);
    assert(result.render.patched_instances == 0);
    assert(result.render.patched_visibility == 1000);
    assert(result.render.patched_materials == 0);
    print(result);

    Transaction reparent(scene);
    reparent.add_parent(groups[0], groups[1]);
    result = run("reparent subtree", scene, reparent, plan, view);
    constexpr std::size_t group_zero_leaf_count = (leaf_count + group_count - 1) / group_count;
    assert(result.changes.stats.changed_occurrences == 1);
    assert(result.changes.stats.dirty_world_transforms == group_zero_leaf_count + 1);
    assert(result.changes.stats.dirty_bounds == group_zero_leaf_count);
    assert(!result.render.plan_rebuilt);
    assert(!result.render.geometry_rebuilt);
    assert(result.render.patched_instances == group_zero_leaf_count);
    assert(result.render.patched_visibility == 0);
    print(result);

    Transaction destroy(scene);
    for (std::size_t index = 0; index < 100; ++index)
        destroy.add_destroy(leaves[index]);
    result = run("destroy 100", scene, destroy, plan, view);
    assert(result.changes.stats.changed_occurrences == 100);
    assert(result.render.plan_rebuilt);
    assert(!result.render.geometry_rebuilt);
    assert(result.render.patched_instances == 0);
    assert(result.render.patched_visibility == 0);
    assert(result.render.patched_materials == 0);
    assert(plan.items().size() == leaf_count - 100);
    print(result);

    nkscene::NativeKitGpuExecutor executor;
    const auto stats = executor.execute(plan, scene->snapshot());
    assert(stats.result == NKGPU_OK);
    assert(stats.geometry_resources_created == 1);
    assert(stats.material_resources_created == materials.size());
    assert(stats.commands == leaf_count - 1000);

    const auto presentation_snapshot = scene->snapshot();
    nkscene::SceneView presentation_view;
    auto presentation_plan = nkscene::compile(presentation_snapshot, presentation_view);
    assert(presentation_plan.items().size() == leaf_count - 100);
    const auto geometry_revision = scene->geometry_store().find(geometry)->revision;
    std::vector<std::uint64_t> material_revisions;
    material_revisions.reserve(materials.size());
    for (const auto material : materials)
        material_revisions.push_back(scene->material_store().find(material)->revision);

    nkscene::SceneView one_hidden_view = presentation_view;
    one_hidden_view.set_visibility_override(leaves[1000], false);
    auto one_hidden_plan = nkscene::compile(presentation_snapshot, presentation_view);
    auto view_result = run_view("view hide one", one_hidden_plan,
                                presentation_snapshot, one_hidden_view);
    assert(!view_result.render.plan_rebuilt);
    assert(!view_result.render.geometry_rebuilt);
    assert(view_result.render.patched_instances == 0);
    assert(view_result.render.patched_visibility == 1);
    assert(view_result.render.patched_materials == 0);
    assert(view_result.render.updated_geometry_resources == 0);
    assert(view_result.render.updated_material_resources == 0);
    assert(view_result.render.patched_culling == 0);
    print(view_result);

    nkscene::SceneView hidden_view = presentation_view;
    hidden_view.visibility_overrides.reserve(1000);
    for (std::size_t index = 1000; index < 2000; ++index)
        hidden_view.visibility_overrides.push_back({leaves[index], false});
    presentation_plan = nkscene::compile(presentation_snapshot, presentation_view);
    view_result = run_view("view hide 1000", presentation_plan, presentation_snapshot, hidden_view);
    assert(!view_result.render.plan_rebuilt);
    assert(!view_result.render.geometry_rebuilt);
    assert(view_result.render.patched_instances == 0);
    assert(view_result.render.patched_visibility == 1000);
    assert(view_result.render.patched_materials == 0);
    assert(view_result.render.updated_geometry_resources == 0);
    assert(view_result.render.updated_material_resources == 0);
    assert(view_result.render.patched_culling == 0);
    print(view_result);

    nkscene::SceneView material_view = presentation_view;
    material_view.set_material_override(leaves[2000], materials[1]);
    presentation_plan = nkscene::compile(presentation_snapshot, presentation_view);
    view_result = run_view("view material one", presentation_plan, presentation_snapshot,
                           material_view);
    assert(!view_result.render.plan_rebuilt);
    assert(!view_result.render.geometry_rebuilt);
    assert(view_result.render.patched_instances == 0);
    assert(view_result.render.patched_visibility == 0);
    assert(view_result.render.patched_materials == 1);
    assert(view_result.render.updated_geometry_resources == 0);
    assert(view_result.render.updated_material_resources == 0);
    assert(view_result.render.patched_culling == 0);
    print(view_result);

    nkscene::SceneView composed_view = hidden_view;
    composed_view.set_material_override(leaves[2000], materials[1]);
    composed_view.clip_planes = {
        {{1.0f, 0.0f, 0.0f}, -0.5f, true},
        {{0.0f, 1.0f, 0.0f}, -0.5f, true}};
    presentation_plan = nkscene::compile(presentation_snapshot, hidden_view);
    view_result = run_view("view composed", presentation_plan, presentation_snapshot,
                           composed_view);
    assert(!view_result.render.plan_rebuilt);
    assert(!view_result.render.geometry_rebuilt);
    assert(view_result.render.patched_instances == 0);
    assert(view_result.render.patched_visibility == 0);
    assert(view_result.render.patched_materials == 1);
    assert(view_result.render.updated_geometry_resources == 0);
    assert(view_result.render.updated_material_resources == 0);
    assert(view_result.render.patched_culling == 0);
    assert(presentation_plan.clip_planes().size() == 2);
    print(view_result);

    nkscene::SceneView disabled_plane_view = composed_view;
    disabled_plane_view.clip_planes.push_back({{1.0f, 0.0f, 0.0f}, -100.0f, false});
    view_result = run_view("view disabled plane", presentation_plan, presentation_snapshot,
                           disabled_plane_view);
    assert(!view_result.render.plan_rebuilt);
    assert(!view_result.render.geometry_rebuilt);
    assert(view_result.render.patched_instances == 0);
    assert(view_result.render.patched_visibility == 0);
    assert(view_result.render.patched_materials == 0);
    assert(view_result.render.updated_geometry_resources == 0);
    assert(view_result.render.updated_material_resources == 0);
    assert(view_result.render.patched_culling == 0);
    assert(presentation_plan.clip_planes().size() == 2);
    print(view_result);

    nkscene::SceneView source_view = presentation_view;
    const nkscene::EntityId material_source{42};
    const nkscene::EntityId hidden_source{84};
    std::size_t material_source_count = 0;
    std::size_t hidden_source_count = 0;
    for (const auto occurrence : presentation_snapshot.occurrences_for_source(material_source)) {
        source_view.set_material_override(occurrence, materials[2]);
        ++material_source_count;
    }
    for (const auto occurrence : presentation_snapshot.occurrences_for_source(hidden_source)) {
        source_view.set_visibility_override(occurrence, false);
        if (const auto *info = presentation_snapshot.find(occurrence); info && info->visible)
            ++hidden_source_count;
    }
    assert(material_source_count > 0);
    assert(hidden_source_count > 0);
    auto source_plan = nkscene::compile(presentation_snapshot, presentation_view);
    view_result = run_view("view source rules", source_plan, presentation_snapshot, source_view);
    assert(!view_result.render.plan_rebuilt);
    assert(!view_result.render.geometry_rebuilt);
    assert(view_result.render.patched_instances == 0);
    assert(view_result.render.patched_visibility == hidden_source_count);
    assert(view_result.render.patched_materials == material_source_count);
    assert(view_result.render.updated_geometry_resources == 0);
    assert(view_result.render.updated_material_resources == 0);
    assert(view_result.render.patched_culling == 0);
    print(view_result);

    assert(scene->geometry_store().find(geometry)->revision == geometry_revision);
    for (std::size_t index = 0; index < materials.size(); ++index)
        assert(scene->material_store().find(materials[index])->revision == material_revisions[index]);
    return 0;
}
