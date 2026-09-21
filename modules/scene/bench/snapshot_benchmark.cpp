#include "scene_internal.hpp"

#include <chrono>
#include <cstdio>
#include <memory>

int main() {
    constexpr std::size_t occurrence_count = 50000;
    constexpr std::size_t snapshot_count = 10000;
    auto scene = std::make_shared<nkscene::Scene>();
    nkscene::Transaction create(scene);
    for (std::size_t index = 0; index < occurrence_count; ++index)
        create.add_create(scene->reserve_occurrence_id());
    nkscene::ChangeSet changes;
    if (scene->commit(create, changes) != NKS_OK)
        return 1;

    const auto start = std::chrono::steady_clock::now();
    std::uint64_t revision_sum = 0;
    for (std::size_t index = 0; index < snapshot_count; ++index)
        revision_sum += scene->snapshot().revision();
    const auto elapsed = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start);
    std::printf("published snapshot x%zu over %zu occurrences: %.3f ms (revision sum=%llu)\n",
                snapshot_count, occurrence_count, elapsed.count(),
                static_cast<unsigned long long>(revision_sum));
    return 0;
}
