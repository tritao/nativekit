#include "scene_internal.hpp"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <memory>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using nkscene::ChangeSet;
using nkscene::Scene;
using nkscene::Transaction;

struct TimedChange {
    const char *name;
    double milliseconds;
    ChangeSet changes;
};

nkscene::LocalTransform translated(float x) {
    nkscene::LocalTransform transform;
    transform.matrix[12] = x;
    return transform;
}

TimedChange run(const char *name, const std::shared_ptr<Scene> &scene,
                Transaction &transaction) {
    const auto start = Clock::now();
    ChangeSet changes;
    assert(scene->commit(transaction, changes) == NKS_OK);
    transaction.close();
    const auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - start);
    return {name, elapsed.count(), std::move(changes)};
}

void print(const TimedChange &result) {
    const auto &stats = result.changes.stats;
    std::printf("%-18s %8.3f ms  occurrences=%zu resources=%zu world=%zu bounds=%zu full=%zu\n",
                result.name, result.milliseconds, stats.changed_occurrences,
                stats.changed_resources, stats.dirty_world_transforms, stats.dirty_bounds,
                stats.full_rebuilds);
}

} // namespace

int main() {
    constexpr std::size_t occurrence_count = 50000;
    constexpr std::size_t group_count = 50;
    constexpr std::size_t leaf_count = occurrence_count - group_count;
    auto scene = std::make_shared<Scene>();
    const auto geometry = scene->reserve_geometry_id();
    auto &geometry_resource = scene->geometry_store().create(geometry);
    geometry_resource.bounds.valid = true;
    geometry_resource.bounds.minimum = {-1.0f, -1.0f, -1.0f};
    geometry_resource.bounds.maximum = {1.0f, 1.0f, 1.0f};
    std::vector<nkscene::MaterialId> materials;
    for (int index = 0; index < 4; ++index)
        materials.push_back(scene->reserve_material_id());
    for (const auto material : materials)
        scene->material_store().create(material);

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
    print(run("create 50k", scene, create));

    Transaction configure(scene);
    for (std::size_t index = 0; index < leaves.size(); ++index) {
        configure.add_parent(leaves[index], groups[index % groups.size()]);
        configure.add_geometry(leaves[index], geometry);
        configure.add_material(leaves[index], materials[index % materials.size()]);
    }
    print(run("configure", scene, configure));

    const auto hierarchy_revision = scene->revision_counters().hierarchy;
    Transaction move_one(scene);
    move_one.add_transform(leaves[0], translated(1.0f));
    auto result = run("move one", scene, move_one);
    assert(result.changes.stats.changed_occurrences == 1);
    assert(result.changes.stats.changed_resources == 0);
    assert(result.changes.revisions.hierarchy == hierarchy_revision);
    print(result);

    Transaction move_hundred(scene);
    for (std::size_t index = 0; index < 100; ++index)
        move_hundred.add_transform(leaves[index], translated(static_cast<float>(index)));
    print(run("move 100", scene, move_hundred));

    Transaction change_material(scene);
    change_material.add_material(leaves[0], materials[1]);
    print(run("material one", scene, change_material));

    Transaction hide(scene);
    for (std::size_t index = 0; index < 1000; ++index)
        hide.add_visibility(leaves[index], false);
    print(run("hide 1000", scene, hide));

    Transaction reparent(scene);
    reparent.add_parent(groups[0], groups[1]);
    print(run("reparent subtree", scene, reparent));

    Transaction destroy(scene);
    for (std::size_t index = 0; index < 100; ++index)
        destroy.add_destroy(leaves[index]);
    print(run("destroy 100", scene, destroy));
    return 0;
}
