#include "nativekit_scene.h"

#include <assert.h>
#include <stdint.h>

static nkscene_transform translated(float x) {
    nkscene_transform result = {0};
    result.matrix[0] = 1.0f;
    result.matrix[5] = 1.0f;
    result.matrix[10] = 1.0f;
    result.matrix[15] = 1.0f;
    result.matrix[12] = x;
    return result;
}

int main(void) {
    nkscene_scene scene = {0};
    assert(nkscene_scene_create(&scene) == NKS_OK);

    nkscene_transaction transaction = {0};
    assert(nkscene_transaction_begin(scene, &transaction) == NKS_OK);
    nkscene_occurrence_id group = {0};
    nkscene_occurrence_id first = {0};
    nkscene_occurrence_id second = {0};
    assert(nkscene_tx_create_occurrence(transaction, &group) == NKS_OK);
    assert(nkscene_tx_create_occurrence(transaction, &first) == NKS_OK);
    assert(nkscene_tx_create_occurrence(transaction, &second) == NKS_OK);
    assert(nkscene_tx_set_parent(transaction, first, group) == NKS_OK);
    assert(nkscene_tx_set_parent(transaction, second, group) == NKS_OK);
    const nkscene_transform first_transform = translated(-2.0f);
    assert(nkscene_tx_set_transform(transaction, first, &first_transform) == NKS_OK);
    assert(nkscene_transaction_commit(transaction) == NKS_OK);

    nkscene_snapshot snapshot = {0};
    assert(nkscene_scene_snapshot(scene, &snapshot) == NKS_OK);
    uint64_t count = 0;
    assert(nkscene_snapshot_get_occurrence_count(snapshot, &count) == NKS_OK);
    assert(count == 3);

    nkscene_snapshot_occurrence info = {0};
    info.struct_size = sizeof(info);
    int found_group = 0;
    int found_first = 0;
    int found_second = 0;
    for (uint64_t index = 0; index < count; ++index) {
        assert(nkscene_snapshot_get_occurrence(snapshot, index, &info) == NKS_OK);
        if (info.occurrence.value == group.value) {
            found_group = 1;
            assert(info.parent.value == 0);
            assert(info.visible != 0);
        } else if (info.occurrence.value == first.value) {
            found_first = 1;
            assert(info.parent.value == group.value);
            assert(info.world_transform.matrix[12] == -2.0f);
        } else if (info.occurrence.value == second.value) {
            found_second = 1;
            assert(info.parent.value == group.value);
        }
    }
    assert(found_group && found_first && found_second);
    assert(nkscene_snapshot_get_occurrence(snapshot, count, &info) ==
           NKS_ERROR_INVALID_ARGUMENT);

    nkscene_snapshot_destroy(snapshot);
    nkscene_scene_destroy(scene);
    return 0;
}
