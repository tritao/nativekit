#include "nativekit_scene.h"

#include "handles.hpp"
#include "scene_internal.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <mutex>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

namespace nkscene {

namespace {

bool transform_equal(const LocalTransform &lhs, const LocalTransform &rhs) noexcept {
    return lhs.matrix == rhs.matrix;
}

} // namespace

bool Scene::exists_after(const std::unordered_map<OccurrenceId, bool> &live,
                         OccurrenceId id) const noexcept {
    const auto found = live.find(id);
    return found == live.end() ? occurrences.contains(id) : found->second;
}

bool Scene::validate(const Transaction &transaction) const noexcept {
    std::unordered_map<OccurrenceId, bool> live;
    std::unordered_map<OccurrenceId, OccurrenceId> final_parents;

    for (const auto &mutation : transaction.mutations()) {
        bool valid = true;
        std::visit(
            [&](const auto &value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, CreateOccurrence>) {
                    valid = value.occurrence.valid() && !occurrences.contains(value.occurrence) &&
                        !live.contains(value.occurrence);
                    if (valid) {
                        live.emplace(value.occurrence, true);
                        final_parents.emplace(value.occurrence, invalid_occurrence);
                    }
                } else if constexpr (std::is_same_v<T, DestroyOccurrence>) {
                    valid = value.occurrence.valid() && exists_after(live, value.occurrence);
                    if (valid)
                        live[value.occurrence] = false;
                } else if constexpr (std::is_same_v<T, SetParent>) {
                    valid = value.occurrence.valid() && exists_after(live, value.occurrence) &&
                        (value.parent == invalid_occurrence ||
                         exists_after(live, value.parent)) &&
                        value.occurrence != value.parent;
                    if (valid)
                        final_parents[value.occurrence] = value.parent;
                } else if constexpr (std::is_same_v<T, SetTransform>) {
                    valid = value.occurrence.valid() && exists_after(live, value.occurrence);
                } else if constexpr (std::is_same_v<T, SetGeometry>) {
                    valid = value.occurrence.valid() && exists_after(live, value.occurrence);
                } else if constexpr (std::is_same_v<T, SetMaterial>) {
                    valid = value.occurrence.valid() && exists_after(live, value.occurrence);
                } else if constexpr (std::is_same_v<T, SetVisibility>) {
                    valid = value.occurrence.valid() && exists_after(live, value.occurrence);
                }
            },
            mutation);
        if (!valid)
            return false;
    }

    for (const auto &[id, is_live] : live) {
        if (is_live)
            continue;
        for (const auto child : hierarchy.children(id)) {
            if (exists_after(live, child) &&
                (!final_parents.contains(child) || final_parents.at(child) == id))
                return false;
        }
    }

    auto parent_of = [&](OccurrenceId id) {
        const auto found = final_parents.find(id);
        return found == final_parents.end() ? hierarchy.parent(id) : found->second;
    };

    std::vector<OccurrenceId> candidates;
    candidates.reserve(final_parents.size() + live.size());
    for (const auto &[id, unused] : final_parents)
        candidates.push_back(id);
    for (const auto &[id, unused] : live)
        candidates.push_back(id);
    for (const auto start : candidates) {
        if (!exists_after(live, start))
            continue;
        std::unordered_set<OccurrenceId> visited;
        for (auto current = start; current.valid(); current = parent_of(current)) {
            if (!exists_after(live, current))
                return false;
            if (!visited.insert(current).second)
                return false;
        }
    }
    return true;
}

void Scene::record_change(ChangeSet &changes,
                          std::unordered_map<OccurrenceId, std::size_t> &indices,
                          OccurrenceId id, ChangeDomain domain) {
    const auto found = indices.find(id);
    if (found == indices.end()) {
        indices.emplace(id, changes.changes.size());
        changes.changes.push_back({id, domain});
    } else {
        changes.changes[found->second].domains |= domain;
    }
}

nkscene_result Scene::commit(const Transaction &transaction, ChangeSet &changes) {
    if (!transaction.active() || transaction.scene().get() != this)
        return NKS_ERROR_INVALID_STATE;
    if (!validate(transaction))
        return NKS_ERROR_INVALID_ARGUMENT;

    changes = {};
    std::unordered_map<OccurrenceId, std::size_t> change_indices;
    change_indices.reserve(transaction.mutations().size());
    for (const auto &mutation : transaction.mutations()) {
        std::visit(
            [&](const auto &value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, CreateOccurrence>) {
                    occurrences.create(value.occurrence);
                    hierarchy.add(value.occurrence);
                    local_transforms.insert_or_assign(value.occurrence, LocalTransform{});
                    visibilities_.insert_or_assign(value.occurrence, Visibility{});
                    record_change(changes, change_indices, value.occurrence,
                                  ChangeDomain::Created);
                } else if constexpr (std::is_same_v<T, DestroyOccurrence>) {
                    source_entities.erase(value.occurrence);
                    parent_components.erase(value.occurrence);
                    local_transforms.erase(value.occurrence);
                    geometry_refs.erase(value.occurrence);
                    material_refs.erase(value.occurrence);
                    visibilities_.erase(value.occurrence);
                    bounds.erase(value.occurrence);
                    hierarchy.remove(value.occurrence);
                    occurrences.destroy(value.occurrence);
                    record_change(changes, change_indices, value.occurrence,
                                  ChangeDomain::Destroyed);
                } else if constexpr (std::is_same_v<T, SetParent>) {
                    const auto previous = hierarchy.parent(value.occurrence);
                    if (previous != value.parent) {
                        hierarchy.reparent(value.occurrence, value.parent);
                        parent_components.insert_or_assign(value.occurrence,
                                                           Parent{value.parent});
                        record_change(changes, change_indices, value.occurrence,
                                      ChangeDomain::Hierarchy);
                    }
                } else if constexpr (std::is_same_v<T, SetTransform>) {
                    auto *previous = local_transforms.find(value.occurrence);
                    if (!previous || !transform_equal(*previous, value.transform)) {
                        local_transforms.insert_or_assign(value.occurrence, value.transform);
                        record_change(changes, change_indices, value.occurrence,
                                      ChangeDomain::Transform);
                    }
                } else if constexpr (std::is_same_v<T, SetGeometry>) {
                    const auto *previous = geometry_refs.find(value.occurrence);
                    if (!previous || previous->id != value.geometry) {
                        geometry_refs.insert_or_assign(value.occurrence,
                                                       GeometryRef{value.geometry});
                        record_change(changes, change_indices, value.occurrence,
                                      ChangeDomain::Geometry);
                    }
                } else if constexpr (std::is_same_v<T, SetMaterial>) {
                    const auto *previous = material_refs.find(value.occurrence);
                    if (!previous || previous->id != value.material) {
                        material_refs.insert_or_assign(value.occurrence,
                                                       MaterialRef{value.material});
                        record_change(changes, change_indices, value.occurrence,
                                      ChangeDomain::Material);
                    }
                } else if constexpr (std::is_same_v<T, SetVisibility>) {
                    const auto *previous = visibilities_.find(value.occurrence);
                    if (!previous || previous->visible != value.visible) {
                        visibilities_.insert_or_assign(value.occurrence,
                                                       Visibility{value.visible});
                        record_change(changes, change_indices, value.occurrence,
                                      ChangeDomain::Visibility);
                    }
                }
            },
            mutation);
    }
    if (!changes.changes.empty())
        ++scene_revision;
    changes.scene_revision = scene_revision;
    return NKS_OK;
}

namespace {

struct RuntimeRegistry {
    std::mutex mutex;
    HandleTable<Scene> scenes;
    HandleTable<Transaction> transactions;
};

RuntimeRegistry &registry() {
    static RuntimeRegistry value;
    return value;
}

std::shared_ptr<Scene> resolve_scene(nkscene_scene handle) {
    return registry().scenes.get(unpack_handle(handle.value));
}

std::shared_ptr<Transaction> resolve_transaction(nkscene_transaction handle) {
    return registry().transactions.get(unpack_handle(handle.value));
}

nkscene_result require_transaction(nkscene_transaction handle,
                                   std::shared_ptr<Transaction> &transaction) {
    transaction = resolve_transaction(handle);
    if (!transaction)
        return NKS_ERROR_INVALID_HANDLE;
    if (!transaction->active())
        return NKS_ERROR_INVALID_STATE;
    return NKS_OK;
}

LocalTransform from_public_transform(const nkscene_transform &transform) {
    LocalTransform result;
    std::copy(std::begin(transform.matrix), std::end(transform.matrix), result.matrix.begin());
    return result;
}

} // namespace

} // namespace nkscene

extern "C" {

nkscene_result NKS_CALL nkscene_scene_create(nkscene_scene *out_scene) {
    if (!out_scene)
        return NKS_ERROR_INVALID_ARGUMENT;
    auto &state = nkscene::registry();
    std::lock_guard lock(state.mutex);
    auto scene = std::make_shared<nkscene::Scene>();
    const auto handle = state.scenes.create(std::move(scene));
    out_scene->value = nkscene::pack_handle(handle);
    return NKS_OK;
}

void NKS_CALL nkscene_scene_destroy(nkscene_scene scene) {
    auto &state = nkscene::registry();
    std::lock_guard lock(state.mutex);
    const auto scene_handle = nkscene::unpack_handle(scene.value);
    auto owner = state.scenes.get(scene_handle);
    if (!owner)
        return;
    std::vector<nkscene::RuntimeHandle> transactions;
    state.transactions.for_each([&](nkscene::RuntimeHandle handle,
                                    const std::shared_ptr<nkscene::Transaction> &transaction) {
        if (transaction->scene() == owner)
            transactions.push_back(handle);
    });
    for (const auto handle : transactions)
        state.transactions.remove(handle);
    state.scenes.remove(scene_handle);
}

nkscene_result NKS_CALL nkscene_transaction_begin(
    nkscene_scene scene, nkscene_transaction *out_transaction) {
    if (!out_transaction)
        return NKS_ERROR_INVALID_ARGUMENT;
    auto &state = nkscene::registry();
    std::lock_guard lock(state.mutex);
    auto owner = state.scenes.get(nkscene::unpack_handle(scene.value));
    if (!owner)
        return NKS_ERROR_INVALID_HANDLE;
    auto transaction = std::make_shared<nkscene::Transaction>(std::move(owner));
    const auto handle = state.transactions.create(std::move(transaction));
    out_transaction->value = nkscene::pack_handle(handle);
    return NKS_OK;
}

void NKS_CALL nkscene_transaction_cancel(nkscene_transaction transaction) {
    auto &state = nkscene::registry();
    std::lock_guard lock(state.mutex);
    state.transactions.remove(nkscene::unpack_handle(transaction.value));
}

nkscene_result NKS_CALL nkscene_transaction_commit(nkscene_transaction transaction_handle) {
    auto &state = nkscene::registry();
    std::lock_guard lock(state.mutex);
    std::shared_ptr<nkscene::Transaction> transaction;
    const auto result = nkscene::require_transaction(transaction_handle, transaction);
    if (result != NKS_OK)
        return result;
    nkscene::ChangeSet changes;
    const auto commit_result = transaction->scene()->commit(*transaction, changes);
    if (commit_result == NKS_OK) {
        transaction->close();
        state.transactions.remove(nkscene::unpack_handle(transaction_handle.value));
    }
    return commit_result;
}

nkscene_result NKS_CALL nkscene_tx_create_occurrence(
    nkscene_transaction handle, nkscene_occurrence_id *out_occurrence) {
    if (!out_occurrence)
        return NKS_ERROR_INVALID_ARGUMENT;
    auto &state = nkscene::registry();
    std::lock_guard lock(state.mutex);
    std::shared_ptr<nkscene::Transaction> transaction;
    const auto result = nkscene::require_transaction(handle, transaction);
    if (result != NKS_OK)
        return result;
    const auto id = transaction->scene()->reserve_occurrence_id();
    transaction->add_create(id);
    out_occurrence->value = id.value;
    return NKS_OK;
}

nkscene_result NKS_CALL nkscene_tx_destroy_occurrence(
    nkscene_transaction handle, nkscene_occurrence_id occurrence) {
    auto &state = nkscene::registry();
    std::lock_guard lock(state.mutex);
    std::shared_ptr<nkscene::Transaction> transaction;
    const auto result = nkscene::require_transaction(handle, transaction);
    if (result != NKS_OK)
        return result;
    transaction->add_destroy({occurrence.value});
    return NKS_OK;
}

nkscene_result NKS_CALL nkscene_tx_set_parent(
    nkscene_transaction handle, nkscene_occurrence_id occurrence,
    nkscene_occurrence_id parent) {
    auto &state = nkscene::registry();
    std::lock_guard lock(state.mutex);
    std::shared_ptr<nkscene::Transaction> transaction;
    const auto result = nkscene::require_transaction(handle, transaction);
    if (result != NKS_OK)
        return result;
    transaction->add_parent({occurrence.value}, {parent.value});
    return NKS_OK;
}

nkscene_result NKS_CALL nkscene_tx_set_transform(
    nkscene_transaction handle, nkscene_occurrence_id occurrence,
    const nkscene_transform *transform) {
    if (!transform)
        return NKS_ERROR_INVALID_ARGUMENT;
    auto &state = nkscene::registry();
    std::lock_guard lock(state.mutex);
    std::shared_ptr<nkscene::Transaction> transaction;
    const auto result = nkscene::require_transaction(handle, transaction);
    if (result != NKS_OK)
        return result;
    transaction->add_transform({occurrence.value}, nkscene::from_public_transform(*transform));
    return NKS_OK;
}

nkscene_result NKS_CALL nkscene_tx_set_geometry(
    nkscene_transaction handle, nkscene_occurrence_id occurrence,
    nkscene_geometry_id geometry) {
    auto &state = nkscene::registry();
    std::lock_guard lock(state.mutex);
    std::shared_ptr<nkscene::Transaction> transaction;
    const auto result = nkscene::require_transaction(handle, transaction);
    if (result != NKS_OK)
        return result;
    transaction->add_geometry({occurrence.value}, {geometry.value});
    return NKS_OK;
}

nkscene_result NKS_CALL nkscene_tx_set_material(
    nkscene_transaction handle, nkscene_occurrence_id occurrence,
    nkscene_material_id material) {
    auto &state = nkscene::registry();
    std::lock_guard lock(state.mutex);
    std::shared_ptr<nkscene::Transaction> transaction;
    const auto result = nkscene::require_transaction(handle, transaction);
    if (result != NKS_OK)
        return result;
    transaction->add_material({occurrence.value}, {material.value});
    return NKS_OK;
}

nkscene_result NKS_CALL nkscene_tx_set_visibility(
    nkscene_transaction handle, nkscene_occurrence_id occurrence, uint32_t visible) {
    auto &state = nkscene::registry();
    std::lock_guard lock(state.mutex);
    std::shared_ptr<nkscene::Transaction> transaction;
    const auto result = nkscene::require_transaction(handle, transaction);
    if (result != NKS_OK)
        return result;
    transaction->add_visibility({occurrence.value}, visible != 0);
    return NKS_OK;
}

} // extern "C"
