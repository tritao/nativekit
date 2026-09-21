#include "scene_internal.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>
#include <vector>

int main() {
    constexpr std::size_t occurrence_count = 1'000'000;
    constexpr int reader_count = 4;
    auto scene = std::make_shared<nkscene::Scene>();
    std::vector<nkscene::OccurrenceId> occurrences;
    occurrences.reserve(occurrence_count);
    nkscene::Transaction create(scene);
    for (std::size_t index = 0; index < occurrence_count; ++index) {
        const auto occurrence = scene->reserve_occurrence_id();
        occurrences.push_back(occurrence);
        create.add_create(occurrence);
    }

    nkscene::ChangeSet changes;
    const auto create_start = std::chrono::steady_clock::now();
    assert(scene->commit(create, changes) == NKS_OK);
    create.close();
    const auto create_elapsed = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - create_start);
    assert(changes.stats.changed_occurrences == occurrence_count);
    assert(scene->snapshot().occurrences().size() == occurrence_count);

    const auto before = scene->snapshot();
    std::atomic<bool> failed = false;
    std::vector<std::thread> readers;
    for (int index = 0; index < reader_count; ++index) {
        readers.emplace_back([&] {
            for (int iteration = 0; iteration < 1000; ++iteration) {
                const auto snapshot = scene->snapshot();
                if (snapshot.occurrences().size() != occurrence_count ||
                    !snapshot.find(occurrences[occurrence_count / 2]))
                    failed.store(true, std::memory_order_release);
            }
        });
    }

    nkscene::LocalTransform transform;
    transform.matrix[12] = 3.0f;
    nkscene::Transaction move(scene);
    move.add_transform(occurrences.front(), transform);
    const auto move_start = std::chrono::steady_clock::now();
    assert(scene->commit(move, changes) == NKS_OK);
    move.close();
    const auto move_elapsed = std::chrono::duration<double, std::micro>(
        std::chrono::steady_clock::now() - move_start);
    for (auto &reader : readers)
        reader.join();

    assert(!failed.load(std::memory_order_acquire));
    assert(changes.stats.changed_occurrences == 1);
    assert(changes.stats.dirty_world_transforms == 1);
    assert(changes.stats.dirty_bounds == 0);
    assert(changes.world_transform_occurrences.size() == 1);
    assert(changes.world_transform_occurrences.front() == occurrences.front());
    assert(before.revision() != scene->snapshot().revision());

    std::printf("scale occurrences=%zu create=%.3f ms move=%.3f us readers=%d\n",
                occurrence_count, create_elapsed.count(), move_elapsed.count(), reader_count);
    return 0;
}
