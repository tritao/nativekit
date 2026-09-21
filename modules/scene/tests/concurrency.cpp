#include "scene_internal.hpp"

#include <atomic>
#include <cassert>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

int main() {
    constexpr std::size_t occurrence_count = 10000;
    constexpr int reader_count = 4;
    constexpr int writer_iterations = 100;

    auto scene = std::make_shared<nkscene::Scene>();
    const auto source = nkscene::EntityId{7};
    const auto geometry = scene->create_geometry();
    const auto material = scene->create_material();
    std::vector<nkscene::OccurrenceId> occurrences;
    occurrences.reserve(occurrence_count);

    nkscene::Transaction create(scene);
    for (std::size_t index = 0; index < occurrence_count; ++index) {
        const auto occurrence = scene->reserve_occurrence_id();
        occurrences.push_back(occurrence);
        create.add_create(occurrence);
        create.add_source_entity(occurrence, source);
        create.add_geometry(occurrence, geometry);
        create.add_material(occurrence, material);
    }
    nkscene::ChangeSet changes;
    assert(scene->commit(create, changes) == NKS_OK);
    create.close();

    const auto initial = scene->snapshot();
    assert(initial.occurrences().size() == occurrence_count);
    assert(initial.occurrences_for_source(source).size() == occurrence_count);
    assert(initial.occurrences_for_geometry(geometry).size() == occurrence_count);
    assert(initial.occurrences_for_material(material).size() == occurrence_count);

    std::atomic<bool> stop = false;
    std::atomic<bool> failed = false;
    std::vector<std::thread> readers;
    readers.reserve(reader_count);
    for (int index = 0; index < reader_count; ++index) {
        readers.emplace_back([&] {
            while (!stop.load(std::memory_order_acquire)) {
                const auto snapshot = scene->snapshot();
                if (snapshot.occurrences().size() != occurrence_count ||
                    snapshot.occurrences_for_source(source).size() != occurrence_count ||
                    snapshot.occurrences_for_geometry(geometry).size() != occurrence_count ||
                    snapshot.occurrences_for_material(material).size() != occurrence_count ||
                    !snapshot.find(occurrences[occurrence_count / 2])) {
                    failed.store(true, std::memory_order_release);
                    return;
                }
            }
        });
    }

    for (int iteration = 1; iteration <= writer_iterations; ++iteration) {
        nkscene::LocalTransform transform;
        transform.matrix[12] = static_cast<float>(iteration);
        nkscene::Transaction move(scene);
        move.add_transform(occurrences.front(), transform);
        assert(scene->commit(move, changes) == NKS_OK);
        move.close();
    }

    stop.store(true, std::memory_order_release);
    for (auto &reader : readers)
        reader.join();

    assert(!failed.load(std::memory_order_acquire));
    assert(initial.find(occurrences.front())->world_transform.transform.matrix[12] == 0.0f);
    assert(scene->snapshot().find(occurrences.front())->world_transform.transform.matrix[12] ==
           static_cast<float>(writer_iterations));
    return 0;
}
