#include "scene_internal.hpp"

#include <atomic>
#include <cassert>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

namespace {

using nkscene::ChangeSet;
using nkscene::Scene;
using nkscene::Transaction;

void published_payloads_are_shared_and_snapshot_safe() {
    auto scene = std::make_shared<Scene>();
    const auto geometry = scene->reserve_geometry_id();
    auto &geometry_resource = scene->geometry_store().create(geometry);
    geometry_resource.edit_payload().vertices = {
        nkscene::GeometryVertex{{0.0f, 0.0f, 0.0f}},
        nkscene::GeometryVertex{{1.0f, 0.0f, 0.0f}},
        nkscene::GeometryVertex{{0.0f, 1.0f, 0.0f}}};
    const auto material = scene->reserve_material_id();
    auto &material_resource = scene->material_store().create(material);
    material_resource.edit_state().base_color = {0.2f, 0.4f, 0.8f, 1.0f};

    const auto occurrence = scene->reserve_occurrence_id();
    Transaction create(scene);
    create.add_create(occurrence);
    create.add_geometry(occurrence, geometry);
    create.add_material(occurrence, material);
    ChangeSet changes;
    assert(scene->commit(create, changes) == NKS_OK);
    create.close();

    const auto before = scene->snapshot();
    const auto before_geometry_resources_revision = before.geometry_resources_revision();
    const auto before_material_resources_revision = before.material_resources_revision();
    const auto *before_geometry = before.find_geometry(geometry);
    const auto *before_material = before.find_material(material);
    assert(before_geometry && before_material);
    assert(before_geometry->payload->vertices.size() == 3);
    assert(before_material->state->base_color[2] == 0.8f);
    const auto same = scene->snapshot();
    assert(before.find_geometry(geometry)->payload == same.find_geometry(geometry)->payload);
    assert(before.find_material(material)->state == same.find_material(material)->state);

    auto &updated_geometry = scene->geometry_store().create(geometry);
    const auto before_geometry_revision = before_geometry->revision;
    updated_geometry.edit_payload().vertices[0].position[0] = 7.0f;
    auto &updated_material = scene->material_store().create(material);
    const auto before_material_revision = before_material->revision;
    updated_material.edit_state().base_color[2] = 0.1f;
    scene->publish();

    const auto after = scene->snapshot();
    nkscene::ResourceChanges resource_changes;
    assert(after.resource_changes_since(before.geometry_resources_revision(),
                                       before.material_resources_revision(), resource_changes));
    assert(resource_changes.geometries.size() == 1);
    assert(resource_changes.geometries.front() == geometry);
    assert(resource_changes.materials.size() == 1);
    assert(resource_changes.materials.front() == material);
    assert(after.geometry_resources_revision() > before_geometry_resources_revision);
    assert(after.material_resources_revision() > before_material_resources_revision);
    assert(after.find_geometry(geometry)->revision > before_geometry_revision);
    assert(after.find_material(material)->revision > before_material_revision);
    assert(after.find_geometry(geometry)->payload->vertices[0].position[0] == 7.0f);
    assert(after.find_material(material)->state->base_color[2] == 0.1f);
    assert(before.find_geometry(geometry)->payload->vertices[0].position[0] == 0.0f);
    assert(before.find_material(material)->state->base_color[2] == 0.8f);

    const auto after_geometry_resources_revision = after.geometry_resources_revision();
    const auto after_material_resources_revision = after.material_resources_revision();
    scene->material_store().find(material)->edit_state().opacity = 0.5f;
    scene->publish();
    const auto material_only = scene->snapshot();
    resource_changes = {};
    assert(material_only.resource_changes_since(after.geometry_resources_revision(),
                                                after.material_resources_revision(),
                                                resource_changes));
    assert(resource_changes.geometries.empty());
    assert(resource_changes.materials.size() == 1);
    assert(resource_changes.materials.front() == material);
    assert(material_only.geometry_resources_revision() == after_geometry_resources_revision);
    assert(material_only.material_resources_revision() > after_material_resources_revision);

    const auto before_direct_edit = scene->snapshot();
    const auto before_direct_geometry_resources_revision =
        before_direct_edit.geometry_resources_revision();
    auto &direct_payload = scene->geometry_store().find(geometry)->edit_payload();
    direct_payload.vertices[0].position[0] = 11.0f;
    Transaction move(scene);
    nkscene::LocalTransform transform;
    transform.matrix[12] = 2.0f;
    move.add_transform(occurrence, transform);
    assert(scene->commit(move, changes) == NKS_OK);
    move.close();
    const auto after_direct_edit = scene->snapshot();
    resource_changes = {};
    assert(after_direct_edit.resource_changes_since(
        before_direct_edit.geometry_resources_revision(),
        before_direct_edit.material_resources_revision(), resource_changes));
    assert(resource_changes.geometries.size() == 1);
    assert(resource_changes.geometries.front() == geometry);
    assert(resource_changes.materials.empty());
    assert(after_direct_edit.geometry_resources_revision() >
           before_direct_geometry_resources_revision);
    assert(after_direct_edit.find_geometry(geometry)->payload->vertices[0].position[0] == 11.0f);
    assert(before_direct_edit.find_geometry(geometry)->payload->vertices[0].position[0] == 7.0f);
}

void readers_can_hold_old_snapshots_during_commits() {
    auto scene = std::make_shared<Scene>();
    const auto occurrence = scene->reserve_occurrence_id();
    Transaction create(scene);
    create.add_create(occurrence);
    ChangeSet changes;
    assert(scene->commit(create, changes) == NKS_OK);
    create.close();
    const auto initial = scene->snapshot();

    std::atomic<bool> stop = false;
    std::atomic<bool> failed = false;
    std::vector<std::thread> readers;
    for (int index = 0; index < 4; ++index) {
        readers.emplace_back([&] {
            while (!stop.load(std::memory_order_acquire)) {
                const auto snapshot = scene->snapshot();
                const auto occurrences = snapshot.occurrences();
                if (occurrences.size() != 1 || !snapshot.find(occurrence) ||
                    snapshot.find(occurrence)->occurrence != occurrence)
                    failed.store(true, std::memory_order_release);
            }
        });
    }

    for (std::uint64_t index = 1; index <= 200; ++index) {
        Transaction move(scene);
        nkscene::LocalTransform transform;
        transform.matrix[12] = static_cast<float>(index);
        move.add_transform(occurrence, transform);
        assert(scene->commit(move, changes) == NKS_OK);
        move.close();
    }

    stop.store(true, std::memory_order_release);
    for (auto &reader : readers)
        reader.join();

    assert(!failed.load(std::memory_order_acquire));
    assert(initial.find(occurrence)->world_transform.transform.matrix[12] == 0.0f);
    assert(scene->snapshot().find(occurrence)->world_transform.transform.matrix[12] == 200.0f);
}

void snapshots_expose_reverse_indexes() {
    auto scene = std::make_shared<Scene>();
    const auto geometry = scene->create_geometry();
    const auto material = scene->create_material();
    const auto root = scene->reserve_occurrence_id();
    const auto first = scene->reserve_occurrence_id();
    const auto second = scene->reserve_occurrence_id();

    Transaction transaction(scene);
    transaction.add_create(root);
    transaction.add_create(first);
    transaction.add_create(second);
    transaction.add_parent(first, root);
    transaction.add_parent(second, root);
    transaction.add_geometry(first, geometry);
    transaction.add_geometry(second, geometry);
    transaction.add_material(first, material);
    transaction.add_material(second, material);
    ChangeSet changes;
    assert(scene->commit(transaction, changes) == NKS_OK);
    transaction.close();

    const auto snapshot = scene->snapshot();
    const auto children = snapshot.children(root);
    assert(children.size() == 2);
    assert(children[0] == first && children[1] == second);
    const auto geometry_occurrences = snapshot.occurrences_for_geometry(geometry);
    assert(geometry_occurrences.size() == 2);
    const auto material_occurrences = snapshot.occurrences_for_material(material);
    assert(material_occurrences.size() == 2);
}

} // namespace

int main() {
    published_payloads_are_shared_and_snapshot_safe();
    readers_can_hold_old_snapshots_during_commits();
    snapshots_expose_reverse_indexes();
    return 0;
}
