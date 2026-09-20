#include "nativekit_scene.h"

#include "handles.hpp"
#include "scene_internal.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <limits>
#include <mutex>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

namespace nkscene {

struct SceneSnapshot::State {
    RevisionCounters revisions;
    std::vector<SnapshotOccurrence> occurrences;
    std::vector<GeometryResource> geometries;
    std::vector<MaterialResource> materials;
};

SceneSnapshot::SceneSnapshot() : state_(std::make_shared<State>()) {}

SceneSnapshot::SceneSnapshot(std::shared_ptr<const State> state) : state_(std::move(state)) {}

std::uint64_t SceneSnapshot::revision() const noexcept {
    return state_->revisions.scene;
}

const RevisionCounters &SceneSnapshot::revisions() const noexcept {
    return state_->revisions;
}

std::span<const SnapshotOccurrence> SceneSnapshot::occurrences() const noexcept {
    return state_->occurrences;
}

const SnapshotOccurrence *SceneSnapshot::find(OccurrenceId id) const noexcept {
    const auto found =
        std::lower_bound(state_->occurrences.begin(), state_->occurrences.end(), id,
                         [](const SnapshotOccurrence &occurrence, OccurrenceId value) {
                             return occurrence.occurrence.value < value.value;
                         });
    return found == state_->occurrences.end() || found->occurrence != id ? nullptr : &*found;
}

std::span<const GeometryResource> SceneSnapshot::geometries() const noexcept {
    return state_->geometries;
}

std::span<const MaterialResource> SceneSnapshot::materials() const noexcept {
    return state_->materials;
}

const GeometryResource *SceneSnapshot::find_geometry(GeometryId id) const noexcept {
    const auto found = std::lower_bound(state_->geometries.begin(), state_->geometries.end(), id,
                                        [](const GeometryResource &resource, GeometryId value) {
                                            return resource.id.value < value.value;
                                        });
    return found == state_->geometries.end() || found->id != id ? nullptr : &*found;
}

const MaterialResource *SceneSnapshot::find_material(MaterialId id) const noexcept {
    const auto found = std::lower_bound(state_->materials.begin(), state_->materials.end(), id,
                                        [](const MaterialResource &resource, MaterialId value) {
                                            return resource.id.value < value.value;
                                        });
    return found == state_->materials.end() || found->id != id ? nullptr : &*found;
}

namespace {

bool transform_equal(const LocalTransform &lhs, const LocalTransform &rhs) noexcept {
    return lhs.matrix == rhs.matrix;
}

LocalTransform multiply(const LocalTransform &lhs, const LocalTransform &rhs) noexcept {
    LocalTransform result{};
    for (std::size_t column = 0; column < 4; ++column) {
        for (std::size_t row = 0; row < 4; ++row) {
            result.matrix[column * 4 + row] = lhs.matrix[row] * rhs.matrix[column * 4] +
                                              lhs.matrix[4 + row] * rhs.matrix[column * 4 + 1] +
                                              lhs.matrix[8 + row] * rhs.matrix[column * 4 + 2] +
                                              lhs.matrix[12 + row] * rhs.matrix[column * 4 + 3];
        }
    }
    return result;
}

std::array<float, 3> transform_point(const LocalTransform &transform,
                                     const std::array<float, 3> &point) noexcept {
    return {transform.matrix[0] * point[0] + transform.matrix[4] * point[1] +
                transform.matrix[8] * point[2] + transform.matrix[12],
            transform.matrix[1] * point[0] + transform.matrix[5] * point[1] +
                transform.matrix[9] * point[2] + transform.matrix[13],
            transform.matrix[2] * point[0] + transform.matrix[6] * point[1] +
                transform.matrix[10] * point[2] + transform.matrix[14]};
}

Bounds transformed_bounds(const Bounds &local, const LocalTransform &transform) noexcept {
    Bounds result;
    result.valid = local.valid;
    if (!local.valid)
        return result;
    result.minimum.fill(std::numeric_limits<float>::infinity());
    result.maximum.fill(-std::numeric_limits<float>::infinity());
    for (int x = 0; x < 2; ++x) {
        for (int y = 0; y < 2; ++y) {
            for (int z = 0; z < 2; ++z) {
                const std::array<float, 3> point{x ? local.maximum[0] : local.minimum[0],
                                                 y ? local.maximum[1] : local.minimum[1],
                                                 z ? local.maximum[2] : local.minimum[2]};
                const auto transformed = transform_point(transform, point);
                for (int axis = 0; axis < 3; ++axis) {
                    result.minimum[axis] = std::min(result.minimum[axis], transformed[axis]);
                    result.maximum[axis] = std::max(result.maximum[axis], transformed[axis]);
                }
            }
        }
    }
    return result;
}

} // namespace

void Scene::recompute_world_transforms(ChangeSet &changes) {
    std::unordered_set<OccurrenceId> dirty;
    std::vector<OccurrenceId> pending;
    for (const auto &change : changes.changes) {
        if (has_domain(change.domains, ChangeDomain::Destroyed)) {
            world_transforms_.erase(change.occurrence);
            bounds.erase(change.occurrence);
            continue;
        }
        if (has_domain(change.domains, ChangeDomain::Created) ||
            has_domain(change.domains, ChangeDomain::Transform) ||
            has_domain(change.domains, ChangeDomain::Hierarchy) ||
            has_domain(change.domains, ChangeDomain::Geometry))
            pending.push_back(change.occurrence);
    }
    while (!pending.empty()) {
        const auto current = pending.back();
        pending.pop_back();
        if (!dirty.insert(current).second)
            continue;
        for (const auto child : hierarchy.children(current))
            pending.push_back(child);
    }

    std::vector<OccurrenceId> roots;
    roots.reserve(dirty.size());
    for (const auto id : dirty) {
        const auto parent = hierarchy.parent(id);
        if (!parent.valid() || !dirty.contains(parent))
            roots.push_back(id);
    }
    std::vector<OccurrenceId> stack;
    for (const auto root : roots) {
        stack.push_back(root);
        while (!stack.empty()) {
            const auto current = stack.back();
            stack.pop_back();
            const auto *local = local_transforms.find(current);
            if (!local)
                continue;
            LocalTransform world = *local;
            const auto parent = hierarchy.parent(current);
            if (parent.valid()) {
                if (const auto *parent_world = world_transforms_.find(parent))
                    world = multiply(parent_world->transform, *local);
            }
            world_transforms_.insert_or_assign(current, WorldTransform{world, revisions.scene + 1});
            ++changes.stats.dirty_world_transforms;
            if (const auto *geometry = geometry_refs.find(current)) {
                const auto *resource = geometries.find(geometry->id);
                if (resource && resource->bounds.valid) {
                    bounds.insert_or_assign(current, transformed_bounds(resource->bounds, world));
                    ++changes.stats.dirty_bounds;
                } else {
                    bounds.erase(current);
                }
            } else {
                bounds.erase(current);
            }
            for (const auto child : hierarchy.children(current))
                if (dirty.contains(child))
                    stack.push_back(child);
        }
    }
}

bool Scene::exists_after(const std::unordered_map<OccurrenceId, bool> &live,
                         OccurrenceId id) const noexcept {
    const auto found = live.find(id);
    return found == live.end() ? occurrences.contains(id) : found->second;
}

SceneSnapshot Scene::snapshot() const {
    auto state = std::make_shared<SceneSnapshot::State>();
    state->revisions = revisions;
    state->occurrences.reserve(occurrences.size());
    occurrences.for_each([&](OccurrenceId id, OccurrenceHandle) {
        SnapshotOccurrence occurrence;
        occurrence.occurrence = id;
        if (const auto *source = source_entities.find(id))
            occurrence.source = source->id;
        occurrence.parent = hierarchy.parent(id);
        if (const auto *local = local_transforms.find(id))
            occurrence.local_transform = *local;
        if (const auto *world = world_transforms_.find(id))
            occurrence.world_transform = *world;
        if (const auto *geometry = geometry_refs.find(id))
            occurrence.geometry = geometry->id;
        if (const auto *material = material_refs.find(id))
            occurrence.material = material->id;
        if (const auto *visibility = visibilities_.find(id))
            occurrence.visible = visibility->visible;
        if (const auto *bound = bounds.find(id))
            occurrence.bounds = *bound;
        state->occurrences.push_back(occurrence);
    });
    std::sort(state->occurrences.begin(), state->occurrences.end(),
              [](const SnapshotOccurrence &lhs, const SnapshotOccurrence &rhs) {
                  return lhs.occurrence.value < rhs.occurrence.value;
              });
    geometries.for_each([&](GeometryId, const GeometryResource &resource) {
        state->geometries.push_back(resource);
    });
    std::sort(state->geometries.begin(), state->geometries.end(),
              [](const GeometryResource &lhs, const GeometryResource &rhs) {
                  return lhs.id.value < rhs.id.value;
              });
    materials.for_each([&](MaterialId, const MaterialResource &resource) {
        state->materials.push_back(resource);
    });
    std::sort(state->materials.begin(), state->materials.end(),
              [](const MaterialResource &lhs, const MaterialResource &rhs) {
                  return lhs.id.value < rhs.id.value;
              });
    return SceneSnapshot(std::move(state));
}

nkscene_result Scene::validate(const Transaction &transaction) const noexcept {
    std::unordered_map<OccurrenceId, bool> live;
    std::unordered_map<OccurrenceId, OccurrenceId> final_parents;

    for (const auto &mutation : transaction.mutations()) {
        nkscene_result result = NKS_OK;
        std::visit(
            [&](const auto &value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, CreateOccurrence>) {
                    const bool valid = value.occurrence.valid() &&
                                       !occurrences.contains(value.occurrence) &&
                                       !live.contains(value.occurrence);
                    if (valid) {
                        live.emplace(value.occurrence, true);
                        final_parents.emplace(value.occurrence, invalid_occurrence);
                    } else {
                        result = NKS_ERROR_INVALID_ARGUMENT;
                    }
                } else if constexpr (std::is_same_v<T, DestroyOccurrence>) {
                    const bool valid =
                        value.occurrence.valid() && exists_after(live, value.occurrence);
                    if (valid) {
                        live[value.occurrence] = false;
                    } else {
                        result = NKS_ERROR_STALE_ID;
                    }
                } else if constexpr (std::is_same_v<T, SetParent>) {
                    const bool valid_target =
                        value.occurrence.valid() && exists_after(live, value.occurrence);
                    const bool valid_parent =
                        value.parent == invalid_occurrence || exists_after(live, value.parent);
                    if (!valid_target || !valid_parent) {
                        result = NKS_ERROR_STALE_ID;
                    } else if (value.occurrence == value.parent) {
                        result = NKS_ERROR_HIERARCHY_CYCLE;
                    } else {
                        final_parents[value.occurrence] = value.parent;
                    }
                } else if constexpr (std::is_same_v<T, SetTransform>) {
                    if (!value.occurrence.valid() || !exists_after(live, value.occurrence))
                        result = NKS_ERROR_STALE_ID;
                } else if constexpr (std::is_same_v<T, SetGeometry>) {
                    if (!value.occurrence.valid() || !exists_after(live, value.occurrence))
                        result = NKS_ERROR_STALE_ID;
                } else if constexpr (std::is_same_v<T, SetMaterial>) {
                    if (!value.occurrence.valid() || !exists_after(live, value.occurrence))
                        result = NKS_ERROR_STALE_ID;
                } else if constexpr (std::is_same_v<T, SetVisibility>) {
                    if (!value.occurrence.valid() || !exists_after(live, value.occurrence))
                        result = NKS_ERROR_STALE_ID;
                }
            },
            mutation);
        if (result != NKS_OK)
            return result;
    }

    for (const auto &[id, is_live] : live) {
        if (is_live)
            continue;
        for (const auto child : hierarchy.children(id)) {
            if (exists_after(live, child) &&
                (!final_parents.contains(child) || final_parents.at(child) == id))
                return NKS_ERROR_INVALID_ARGUMENT;
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
                return NKS_ERROR_STALE_ID;
            if (!visited.insert(current).second)
                return NKS_ERROR_HIERARCHY_CYCLE;
        }
    }
    return NKS_OK;
}

void Scene::record_change(ChangeSet &changes,
                          std::unordered_map<OccurrenceId, std::size_t> &indices, OccurrenceId id,
                          ChangeDomain domain) {
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
    const auto validation = validate(transaction);
    if (validation != NKS_OK)
        return validation;

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
                    record_change(changes, change_indices, value.occurrence, ChangeDomain::Created);
                } else if constexpr (std::is_same_v<T, DestroyOccurrence>) {
                    source_entities.erase(value.occurrence);
                    parent_components.erase(value.occurrence);
                    local_transforms.erase(value.occurrence);
                    world_transforms_.erase(value.occurrence);
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
                        parent_components.insert_or_assign(value.occurrence, Parent{value.parent});
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
                        visibilities_.insert_or_assign(value.occurrence, Visibility{value.visible});
                        record_change(changes, change_indices, value.occurrence,
                                      ChangeDomain::Visibility);
                    }
                }
            },
            mutation);
    }
    changes.stats.changed_occurrences = changes.changes.size();
    recompute_world_transforms(changes);
    if (!changes.changes.empty()) {
        auto &revision = revisions;
        ++revision.scene;
        bool hierarchy_changed = false;
        bool transform_changed = false;
        bool geometry_changed = false;
        bool material_changed = false;
        bool visibility_changed = false;
        bool bounds_changed = false;
        for (const auto &change : changes.changes) {
            hierarchy_changed = hierarchy_changed ||
                                has_domain(change.domains, ChangeDomain::Created) ||
                                has_domain(change.domains, ChangeDomain::Destroyed) ||
                                has_domain(change.domains, ChangeDomain::Hierarchy);
            transform_changed =
                transform_changed || has_domain(change.domains, ChangeDomain::Transform);
            geometry_changed =
                geometry_changed || has_domain(change.domains, ChangeDomain::Geometry);
            material_changed =
                material_changed || has_domain(change.domains, ChangeDomain::Material);
            visibility_changed =
                visibility_changed || has_domain(change.domains, ChangeDomain::Visibility);
            bounds_changed = bounds_changed || has_domain(change.domains, ChangeDomain::Bounds);
        }
        if (hierarchy_changed)
            ++revision.hierarchy;
        if (transform_changed)
            ++revision.transform;
        if (geometry_changed)
            ++revision.geometry;
        if (material_changed)
            ++revision.material;
        if (visibility_changed)
            ++revision.visibility;
        if (bounds_changed)
            ++revision.bounds;
    }
    changes.scene_revision = revisions.scene;
    changes.revisions = revisions;
    return NKS_OK;
}

namespace {

struct RuntimeRegistry {
    std::mutex mutex;
    HandleTable<Scene> scenes;
    HandleTable<Transaction> transactions;
    HandleTable<SceneSnapshot> snapshots;
    HandleTable<ChangeSet> change_sets;
};

RuntimeRegistry &registry() {
    static RuntimeRegistry value;
    return value;
}

std::shared_ptr<Scene> resolve_scene(nkscene_scene handle) {
    return registry().scenes.get(unpack_handle(handle));
}

std::shared_ptr<Transaction> resolve_transaction(nkscene_transaction handle) {
    return registry().transactions.get(unpack_handle(handle));
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

nkscene_result commit_transaction(nkscene_transaction transaction_handle,
                                  nkscene_change_set *out_changes) {
    if (out_changes)
        *out_changes = 0;
    auto &state = registry();
    std::lock_guard lock(state.mutex);
    std::shared_ptr<Transaction> transaction;
    const auto result = require_transaction(transaction_handle, transaction);
    if (result != NKS_OK)
        return result;

    ChangeSet changes;
    const auto commit_result = transaction->scene()->commit(*transaction, changes);
    if (commit_result != NKS_OK)
        return commit_result;

    if (out_changes) {
        auto change_set = std::make_shared<ChangeSet>(std::move(changes));
        const auto handle = state.change_sets.create(std::move(change_set));
        *out_changes = pack_handle(handle);
    }
    transaction->close();
    state.transactions.remove(unpack_handle(transaction_handle));
    return NKS_OK;
}

} // namespace

NKS_API std::shared_ptr<const SceneSnapshot>
resolve_snapshot_handle(nkscene_snapshot snapshot) noexcept {
    auto &state = registry();
    std::lock_guard lock(state.mutex);
    return state.snapshots.get(unpack_handle(snapshot));
}

NKS_API std::shared_ptr<const ChangeSet>
resolve_change_set_handle(nkscene_change_set changes) noexcept {
    auto &state = registry();
    std::lock_guard lock(state.mutex);
    return state.change_sets.get(unpack_handle(changes));
}

void copy_snapshot_occurrence(const SnapshotOccurrence &source,
                              nkscene_snapshot_occurrence &target) noexcept {
    target.occurrence.value = source.occurrence.value;
    target.source.value = source.source.value;
    target.parent.value = source.parent.value;
    std::copy(source.local_transform.matrix.begin(), source.local_transform.matrix.end(),
              std::begin(target.local_transform.matrix));
    std::copy(source.world_transform.transform.matrix.begin(),
              source.world_transform.transform.matrix.end(),
              std::begin(target.world_transform.matrix));
    target.world_transform_revision = source.world_transform.revision;
    target.geometry.value = source.geometry.value;
    target.material.value = source.material.value;
    target.visible = source.visible ? 1u : 0u;
    std::copy(source.bounds.minimum.begin(), source.bounds.minimum.end(),
              std::begin(target.bounds.minimum));
    std::copy(source.bounds.maximum.begin(), source.bounds.maximum.end(),
              std::begin(target.bounds.maximum));
    target.bounds.valid = source.bounds.valid ? 1u : 0u;
}

} // namespace nkscene

extern "C" {

nkscene_result NKS_CALL nkscene_scene_create(nkscene_scene *out_scene) {
    if (!out_scene)
        return NKS_ERROR_INVALID_ARGUMENT;
    auto &state = nkscene::registry();
    std::lock_guard lock(state.mutex);
    auto scene = std::make_shared<nkscene::Scene>();
    const auto handle = state.scenes.create(std::move(scene));
    *out_scene = nkscene::pack_handle(handle);
    return NKS_OK;
}

void NKS_CALL nkscene_scene_destroy(nkscene_scene scene) {
    auto &state = nkscene::registry();
    std::lock_guard lock(state.mutex);
    const auto scene_handle = nkscene::unpack_handle(scene);
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

nkscene_result NKS_CALL nkscene_transaction_begin(nkscene_scene scene,
                                                  nkscene_transaction *out_transaction) {
    if (!out_transaction)
        return NKS_ERROR_INVALID_ARGUMENT;
    auto &state = nkscene::registry();
    std::lock_guard lock(state.mutex);
    auto owner = state.scenes.get(nkscene::unpack_handle(scene));
    if (!owner)
        return NKS_ERROR_INVALID_HANDLE;
    auto transaction = std::make_shared<nkscene::Transaction>(std::move(owner));
    const auto handle = state.transactions.create(std::move(transaction));
    *out_transaction = nkscene::pack_handle(handle);
    return NKS_OK;
}

void NKS_CALL nkscene_transaction_cancel(nkscene_transaction transaction) {
    auto &state = nkscene::registry();
    std::lock_guard lock(state.mutex);
    state.transactions.remove(nkscene::unpack_handle(transaction));
}

nkscene_result NKS_CALL nkscene_transaction_commit(nkscene_transaction transaction_handle) {
    return nkscene::commit_transaction(transaction_handle, nullptr);
}

nkscene_result NKS_CALL nkscene_transaction_commit_with_changes(
    nkscene_transaction transaction_handle, nkscene_change_set *out_changes) {
    if (!out_changes)
        return NKS_ERROR_INVALID_ARGUMENT;
    return nkscene::commit_transaction(transaction_handle, out_changes);
}

nkscene_result NKS_CALL nkscene_tx_create_occurrence(nkscene_transaction handle,
                                                     nkscene_occurrence_id *out_occurrence) {
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

nkscene_result NKS_CALL nkscene_tx_destroy_occurrence(nkscene_transaction handle,
                                                      nkscene_occurrence_id occurrence) {
    auto &state = nkscene::registry();
    std::lock_guard lock(state.mutex);
    std::shared_ptr<nkscene::Transaction> transaction;
    const auto result = nkscene::require_transaction(handle, transaction);
    if (result != NKS_OK)
        return result;
    transaction->add_destroy({occurrence.value});
    return NKS_OK;
}

nkscene_result NKS_CALL nkscene_tx_set_parent(nkscene_transaction handle,
                                              nkscene_occurrence_id occurrence,
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

nkscene_result NKS_CALL nkscene_tx_set_transform(nkscene_transaction handle,
                                                 nkscene_occurrence_id occurrence,
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

nkscene_result NKS_CALL nkscene_tx_set_geometry(nkscene_transaction handle,
                                                nkscene_occurrence_id occurrence,
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

nkscene_result NKS_CALL nkscene_tx_set_material(nkscene_transaction handle,
                                                nkscene_occurrence_id occurrence,
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

nkscene_result NKS_CALL nkscene_tx_set_visibility(nkscene_transaction handle,
                                                  nkscene_occurrence_id occurrence,
                                                  uint32_t visible) {
    auto &state = nkscene::registry();
    std::lock_guard lock(state.mutex);
    std::shared_ptr<nkscene::Transaction> transaction;
    const auto result = nkscene::require_transaction(handle, transaction);
    if (result != NKS_OK)
        return result;
    transaction->add_visibility({occurrence.value}, visible != 0);
    return NKS_OK;
}

nkscene_result NKS_CALL nkscene_scene_snapshot(nkscene_scene scene,
                                               nkscene_snapshot *out_snapshot) {
    if (!out_snapshot)
        return NKS_ERROR_INVALID_ARGUMENT;
    *out_snapshot = 0;
    auto &state = nkscene::registry();
    std::lock_guard lock(state.mutex);
    auto owner = state.scenes.get(nkscene::unpack_handle(scene));
    if (!owner)
        return NKS_ERROR_INVALID_HANDLE;
    auto snapshot = std::make_shared<nkscene::SceneSnapshot>(owner->snapshot());
    const auto handle = state.snapshots.create(std::move(snapshot));
    *out_snapshot = nkscene::pack_handle(handle);
    return NKS_OK;
}

void NKS_CALL nkscene_snapshot_destroy(nkscene_snapshot snapshot) {
    auto &state = nkscene::registry();
    std::lock_guard lock(state.mutex);
    state.snapshots.remove(nkscene::unpack_handle(snapshot));
}

nkscene_result NKS_CALL nkscene_snapshot_get_revision(nkscene_snapshot snapshot,
                                                      uint64_t *out_revision) {
    if (!out_revision)
        return NKS_ERROR_INVALID_ARGUMENT;
    auto &state = nkscene::registry();
    std::lock_guard lock(state.mutex);
    const auto value = state.snapshots.get(nkscene::unpack_handle(snapshot));
    if (!value)
        return NKS_ERROR_INVALID_HANDLE;
    *out_revision = value->revision();
    return NKS_OK;
}

nkscene_result NKS_CALL nkscene_snapshot_get_occurrence_count(
    nkscene_snapshot snapshot, uint64_t *out_count) {
    if (!out_count)
        return NKS_ERROR_INVALID_ARGUMENT;
    auto &state = nkscene::registry();
    std::lock_guard lock(state.mutex);
    const auto value = state.snapshots.get(nkscene::unpack_handle(snapshot));
    if (!value)
        return NKS_ERROR_INVALID_HANDLE;
    *out_count = value->occurrences().size();
    return NKS_OK;
}

nkscene_result NKS_CALL nkscene_snapshot_get_occurrence(
    nkscene_snapshot snapshot, uint64_t index,
    nkscene_snapshot_occurrence *out_occurrence) {
    if (!out_occurrence)
        return NKS_ERROR_INVALID_ARGUMENT;
    if (out_occurrence->struct_size < sizeof(nkscene_snapshot_occurrence))
        return NKS_ERROR_INVALID_ARGUMENT;
    auto &state = nkscene::registry();
    std::lock_guard lock(state.mutex);
    const auto value = state.snapshots.get(nkscene::unpack_handle(snapshot));
    if (!value)
        return NKS_ERROR_INVALID_HANDLE;
    const auto occurrences = value->occurrences();
    if (index >= occurrences.size())
        return NKS_ERROR_INVALID_ARGUMENT;
    nkscene::copy_snapshot_occurrence(occurrences[static_cast<std::size_t>(index)],
                                      *out_occurrence);
    return NKS_OK;
}

void NKS_CALL nkscene_change_set_destroy(nkscene_change_set changes) {
    auto &state = nkscene::registry();
    std::lock_guard lock(state.mutex);
    state.change_sets.remove(nkscene::unpack_handle(changes));
}

nkscene_result NKS_CALL nkscene_change_set_get_revision(nkscene_change_set changes,
                                                        uint64_t *out_revision) {
    if (!out_revision)
        return NKS_ERROR_INVALID_ARGUMENT;
    auto &state = nkscene::registry();
    std::lock_guard lock(state.mutex);
    const auto value = state.change_sets.get(nkscene::unpack_handle(changes));
    if (!value)
        return NKS_ERROR_INVALID_HANDLE;
    *out_revision = value->scene_revision;
    return NKS_OK;
}

nkscene_result NKS_CALL nkscene_geometry_create(nkscene_scene scene,
                                                nkscene_geometry_id *out_geometry) {
    if (!out_geometry)
        return NKS_ERROR_INVALID_ARGUMENT;
    auto &state = nkscene::registry();
    std::lock_guard lock(state.mutex);
    auto owner = state.scenes.get(nkscene::unpack_handle(scene));
    if (!owner)
        return NKS_ERROR_INVALID_HANDLE;
    out_geometry->value = owner->create_geometry().value;
    return NKS_OK;
}

void NKS_CALL nkscene_geometry_destroy(nkscene_scene scene, nkscene_geometry_id geometry) {
    auto &state = nkscene::registry();
    std::lock_guard lock(state.mutex);
    auto owner = state.scenes.get(nkscene::unpack_handle(scene));
    if (owner)
        owner->destroy_geometry({geometry.value});
}

nkscene_result NKS_CALL nkscene_material_create(nkscene_scene scene,
                                                nkscene_material_id *out_material) {
    if (!out_material)
        return NKS_ERROR_INVALID_ARGUMENT;
    auto &state = nkscene::registry();
    std::lock_guard lock(state.mutex);
    auto owner = state.scenes.get(nkscene::unpack_handle(scene));
    if (!owner)
        return NKS_ERROR_INVALID_HANDLE;
    out_material->value = owner->create_material().value;
    return NKS_OK;
}

void NKS_CALL nkscene_material_destroy(nkscene_scene scene, nkscene_material_id material) {
    auto &state = nkscene::registry();
    std::lock_guard lock(state.mutex);
    auto owner = state.scenes.get(nkscene::unpack_handle(scene));
    if (owner)
        owner->destroy_material({material.value});
}

nkscene_result NKS_CALL nkscene_geometry_set_data(nkscene_scene scene, nkscene_geometry_id geometry,
                                                  const nkscene_geometry_data *data) {
    if (!data || data->struct_size < sizeof(nkscene_geometry_data))
        return NKS_ERROR_INVALID_ARGUMENT;
    if ((data->vertex_count != 0 && !data->vertices) ||
        (data->index_count != 0 && !data->indices) ||
        (data->subelement_count != 0 && !data->subelements))
        return NKS_ERROR_INVALID_ARGUMENT;
    const auto element_count = data->index_count != 0 ? data->index_count : data->vertex_count;
    if (element_count % 3 != 0)
        return NKS_ERROR_INVALID_ARGUMENT;
    const auto *indices = static_cast<const uint32_t *>(data->indices);
    if (data->index_count != 0) {
        for (uint32_t index = 0; index < data->index_count; ++index)
            if (indices[index] >= data->vertex_count)
                return NKS_ERROR_INVALID_ARGUMENT;
    }
    const auto primitive_count = element_count / 3;
    for (uint32_t index = 0; index < data->subelement_count; ++index) {
        const auto &range = data->subelements[index];
        if (static_cast<uint64_t>(range.first_primitive) + range.primitive_count > primitive_count)
            return NKS_ERROR_INVALID_ARGUMENT;
    }

    auto &state = nkscene::registry();
    std::lock_guard lock(state.mutex);
    auto owner = state.scenes.get(nkscene::unpack_handle(scene));
    if (!owner)
        return NKS_ERROR_INVALID_HANDLE;
    if (!owner->geometry_store().find({geometry.value}))
        return NKS_ERROR_STALE_ID;
    auto &resource = owner->geometry_store().create({geometry.value});
    resource.payload.vertices.resize(data->vertex_count);
    for (uint32_t index = 0; index < data->vertex_count; ++index)
        std::copy(std::begin(data->vertices[index].position),
                  std::end(data->vertices[index].position),
                  resource.payload.vertices[index].position.begin());
    resource.payload.indices.clear();
    if (data->index_count != 0)
        resource.payload.indices.assign(indices, indices + data->index_count);
    resource.bounds.valid = data->bounds.valid != 0;
    std::copy(std::begin(data->bounds.minimum), std::end(data->bounds.minimum),
              resource.bounds.minimum.begin());
    std::copy(std::begin(data->bounds.maximum), std::end(data->bounds.maximum),
              resource.bounds.maximum.begin());
    resource.subelements.ranges.clear();
    if (data->subelement_count != 0) {
        resource.subelements.ranges.reserve(data->subelement_count);
        for (uint32_t index = 0; index < data->subelement_count; ++index) {
            const auto &range = data->subelements[index];
            resource.subelements.ranges.push_back(
                {range.first_primitive, range.primitive_count, range.subelement});
        }
    }
    return NKS_OK;
}

nkscene_result NKS_CALL nkscene_material_set_data(nkscene_scene scene, nkscene_material_id material,
                                                  const nkscene_material_data *data) {
    if (!data || data->struct_size < sizeof(nkscene_material_data))
        return NKS_ERROR_INVALID_ARGUMENT;
    auto &state = nkscene::registry();
    std::lock_guard lock(state.mutex);
    auto owner = state.scenes.get(nkscene::unpack_handle(scene));
    if (!owner)
        return NKS_ERROR_INVALID_HANDLE;
    if (!owner->material_store().find({material.value}))
        return NKS_ERROR_STALE_ID;
    auto &resource = owner->material_store().create({material.value});
    std::copy(std::begin(data->base_color), std::end(data->base_color),
              resource.base_color.begin());
    resource.opacity = data->opacity;
    resource.flags = data->flags;
    return NKS_OK;
}

} // extern "C"
