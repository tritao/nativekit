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
    assert(after.find_geometry(geometry)->revision > before_geometry_revision);
    assert(after.find_material(material)->revision > before_material_revision);
    assert(after.find_geometry(geometry)->payload->vertices[0].position[0] == 7.0f);
    assert(after.find_material(material)->state->base_color[2] == 0.1f);
    assert(before.find_geometry(geometry)->payload->vertices[0].position[0] == 0.0f);
    assert(before.find_material(material)->state->base_color[2] == 0.8f);
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

} // namespace

int main() {
    published_payloads_are_shared_and_snapshot_safe();
    readers_can_hold_old_snapshots_during_commits();
    return 0;
}
