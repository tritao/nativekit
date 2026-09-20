#include "nativekit_scene.h"

#include "component_store.hpp"
#include "scene_internal.hpp"

#include <cassert>
#include <cstdint>
#include <memory>
#include <vector>

namespace {

using nkscene::ChangeDomain;
using nkscene::ComponentStore;
using nkscene::LocalTransform;
using nkscene::Scene;
using nkscene::Transaction;

LocalTransform translated(float x) {
    LocalTransform transform;
    transform.matrix[12] = x;
    return transform;
}

void component_store_is_dense_and_sparse() {
    ComponentStore<std::uint32_t> store;
    const nkscene::OccurrenceId first{1};
    const nkscene::OccurrenceId second{2};
    const nkscene::OccurrenceId third{3};
    store.insert_or_assign(first, 10);
    store.insert_or_assign(second, 20);
    store.insert_or_assign(third, 30);
    assert(store.size() == 3);
    assert(*store.find(second) == 20);
    assert(store.erase(second));
    assert(!store.contains(second));
    assert(*store.find(third) == 30);
    store.insert_or_assign(first, 11);
    assert(*store.find(first) == 11);
}

void changes_are_domain_precise() {
    auto scene = std::make_shared<Scene>();
    const auto first = scene->reserve_occurrence_id();
    const auto second = scene->reserve_occurrence_id();
    Transaction create(scene);
    create.add_create(first);
    create.add_create(second);
    nkscene::ChangeSet changes;
    assert(scene->commit(create, changes) == NKS_OK);
    create.close();
    assert(changes.changes.size() == 2);
    assert(changes.revisions.scene == 1);
    assert(changes.revisions.hierarchy == 1);

    const auto transform = translated(4.0f);
    Transaction move(scene);
    move.add_transform(first, transform);
    assert(scene->commit(move, changes) == NKS_OK);
    move.close();
    assert(changes.changes.size() == 1);
    assert(changes.changes.front().occurrence == first);
    assert(changes.changes.front().domains == ChangeDomain::Transform);
    assert(changes.revisions.scene == 2);
    assert(changes.revisions.transform == 1);
    assert(changes.revisions.hierarchy == 1);
    assert(changes.revisions.geometry == 0);
    assert(changes.revisions.material == 0);
    assert(changes.revisions.visibility == 0);
    assert(scene->transforms().find(first)->matrix[12] == 4.0f);

    Transaction invalid(scene);
    invalid.add_transform(first, translated(9.0f));
    invalid.add_transform({999999}, translated(10.0f));
    assert(scene->commit(invalid, changes) == NKS_ERROR_STALE_ID);
    assert(scene->transforms().find(first)->matrix[12] == 4.0f);
    assert(scene->revision() == 2);

    Transaction cycle(scene);
    cycle.add_parent(first, second);
    cycle.add_parent(second, first);
    assert(scene->commit(cycle, changes) == NKS_ERROR_HIERARCHY_CYCLE);
    assert(scene->hierarchy_index().parent(first) == nkscene::invalid_occurrence);
    assert(scene->hierarchy_index().parent(second) == nkscene::invalid_occurrence);
}

void shared_resources_do_not_follow_instance_transforms() {
    constexpr std::size_t count = 50000;
    auto scene = std::make_shared<Scene>();
    const auto geometry = scene->reserve_geometry_id();
    auto &resource = scene->geometry_store().create(geometry);
    resource.bounds.valid = true;
    resource.bounds.minimum = {-1.0f, -1.0f, -1.0f};
    resource.bounds.maximum = {1.0f, 1.0f, 1.0f};
    const auto resource_revision = resource.revision;

    std::vector<nkscene::OccurrenceId> occurrences;
    occurrences.reserve(count);
    Transaction create(scene);
    for (std::size_t index = 0; index < count; ++index) {
        const auto occurrence = scene->reserve_occurrence_id();
        occurrences.push_back(occurrence);
        create.add_create(occurrence);
    }
    nkscene::ChangeSet changes;
    assert(scene->commit(create, changes) == NKS_OK);
    create.close();

    Transaction assign_geometry(scene);
    for (const auto occurrence : occurrences)
        assign_geometry.add_geometry(occurrence, geometry);
    assert(scene->commit(assign_geometry, changes) == NKS_OK);
    assign_geometry.close();
    assert(changes.stats.changed_occurrences == count);
    assert(scene->geometry_store().find(geometry)->revision == resource_revision);

    Transaction move(scene);
    move.add_transform(occurrences.front(), translated(12.0f));
    assert(scene->commit(move, changes) == NKS_OK);
    move.close();
    assert(changes.stats.changed_occurrences == 1);
    assert(changes.stats.dirty_world_transforms == 1);
    assert(changes.stats.dirty_bounds == 1);
    assert(scene->geometry_store().find(geometry)->revision == resource_revision);
    assert(scene->world_transforms().find(occurrences.front())->transform.matrix[12] == 12.0f);
    assert(scene->world_transforms().find(occurrences.front())->revision != 0);
}

void snapshots_are_immutable() {
    auto scene = std::make_shared<Scene>();
    const auto occurrence = scene->reserve_occurrence_id();
    Transaction create(scene);
    create.add_create(occurrence);
    nkscene::ChangeSet changes;
    assert(scene->commit(create, changes) == NKS_OK);
    create.close();

    Transaction move(scene);
    move.add_transform(occurrence, translated(3.0f));
    assert(scene->commit(move, changes) == NKS_OK);
    move.close();
    const auto before = scene->snapshot();
    assert(before.revision() == scene->revision());
    assert(before.occurrences().size() == 1);
    assert(before.find(occurrence)->world_transform.transform.matrix[12] == 3.0f);

    Transaction move_again(scene);
    move_again.add_transform(occurrence, translated(7.0f));
    assert(scene->commit(move_again, changes) == NKS_OK);
    move_again.close();
    assert(scene->snapshot().find(occurrence)->world_transform.transform.matrix[12] == 7.0f);
    assert(before.find(occurrence)->world_transform.transform.matrix[12] == 3.0f);
    assert(before.revision() != scene->revision());
}

} // namespace

int main() {
    component_store_is_dense_and_sparse();
    changes_are_domain_precise();
    shared_resources_do_not_follow_instance_transforms();
    snapshots_are_immutable();
    return 0;
}
