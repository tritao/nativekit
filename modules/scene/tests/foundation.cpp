#include "nativekit_scene.h"

#include <cassert>
#include <cstdint>
#include <vector>

namespace {

nkscene_transform identity_transform() {
    nkscene_transform transform{};
    transform.matrix[0] = 1.0f;
    transform.matrix[5] = 1.0f;
    transform.matrix[10] = 1.0f;
    transform.matrix[15] = 1.0f;
    return transform;
}

void create_destroy_stress(nkscene_scene scene) {
    constexpr std::size_t count = 100000;
    constexpr int rounds = 3;
    std::vector<nkscene_occurrence_id> ids;
    ids.reserve(count);
    std::vector<nkscene_occurrence_id> stale;
    stale.reserve(count);

    for (int round = 0; round < rounds; ++round) {
        nkscene_transaction transaction{};
        assert(nkscene_transaction_begin(scene, &transaction) == NKS_OK);
        ids.clear();
        for (std::size_t index = 0; index < count; ++index) {
            nkscene_occurrence_id occurrence{};
            assert(nkscene_tx_create_occurrence(transaction, &occurrence) == NKS_OK);
            assert(occurrence.value != 0);
            ids.push_back(occurrence);
        }
        assert(nkscene_transaction_commit(transaction) == NKS_OK);

        const auto transform = identity_transform();
        assert(nkscene_transaction_begin(scene, &transaction) == NKS_OK);
        for (const auto occurrence : ids) {
            assert(nkscene_tx_set_transform(transaction, occurrence, &transform) == NKS_OK);
        }
        assert(nkscene_transaction_commit(transaction) == NKS_OK);

        stale = ids;
        assert(nkscene_transaction_begin(scene, &transaction) == NKS_OK);
        for (const auto occurrence : ids)
            assert(nkscene_tx_destroy_occurrence(transaction, occurrence) == NKS_OK);
        assert(nkscene_transaction_commit(transaction) == NKS_OK);

        assert(nkscene_transaction_begin(scene, &transaction) == NKS_OK);
        assert(nkscene_tx_set_transform(transaction, stale.front(), &transform) == NKS_OK);
        assert(nkscene_transaction_commit(transaction) == NKS_ERROR_STALE_ID);
        nkscene_transaction_cancel(transaction);
    }
}

void stale_runtime_handles() {
    nkscene_scene scene{};
    assert(nkscene_scene_create(&scene) == NKS_OK);
    nkscene_transaction transaction{};
    assert(nkscene_transaction_begin(scene, &transaction) == NKS_OK);
    nkscene_transaction_cancel(transaction);
    assert(nkscene_transaction_commit(transaction) == NKS_ERROR_INVALID_HANDLE);
    nkscene_scene_destroy(scene);
    assert(nkscene_transaction_begin(scene, &transaction) == NKS_ERROR_INVALID_HANDLE);
    nkscene_scene_destroy(scene);
}

} // namespace

int main() {
    nkscene_scene scene{};
    assert(nkscene_scene_create(&scene) == NKS_OK);
    create_destroy_stress(scene);
    nkscene_scene_destroy(scene);
    stale_runtime_handles();
    return 0;
}
