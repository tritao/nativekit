#include "nativekit_scene.h"

#include "handles.hpp"
#include "scene_internal.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <limits>
#include <mutex>
#include <span>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

namespace nkscene {

SceneSnapshot::SceneSnapshot() : state_(std::make_shared<PublishedSceneState>()) {}

SceneSnapshot::SceneSnapshot(std::shared_ptr<const PublishedSceneState> state)
    : state_(std::move(state)) {}

std::uint64_t SceneSnapshot::revision() const noexcept {
    return state_->revisions.scene;
}

const RevisionCounters &SceneSnapshot::revisions() const noexcept {
    return state_->revisions;
}

std::span<const SnapshotOccurrence> SceneSnapshot::occurrences() const noexcept {
    return state_->occurrences;
}

std::span<const OccurrenceId>
SceneSnapshot::occurrences_for_source(EntityId source) const noexcept {
    const auto found = state_->occurrences_by_source.find(source);
    return found == state_->occurrences_by_source.end()
               ? std::span<const OccurrenceId>{}
               : std::span<const OccurrenceId>{found->second};
}

const SnapshotOccurrence *SceneSnapshot::find(OccurrenceId id) const noexcept {
    const auto found =
        std::lower_bound(state_->occurrences.begin(), state_->occurrences.end(), id,
                         [](const SnapshotOccurrence &occurrence, OccurrenceId value) {
                             return occurrence.occurrence.value < value.value;
                         });
    return found == state_->occurrences.end() || found->occurrence != id ? nullptr : &*found;
}

std::string_view SceneSnapshot::name(OccurrenceId id) const noexcept {
    const auto *occurrence = find(id);
    return occurrence ? std::string_view{occurrence->name} : std::string_view{};
}

std::string_view SceneSnapshot::entity_name(EntityId id) const noexcept {
    const auto found = state_->entity_names.find(id);
    if (found == state_->entity_names.end())
        return {};
    return found->second;
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

std::span<const ImageResource> SceneSnapshot::images() const noexcept {
    return state_->images;
}

std::span<const TextureResource> SceneSnapshot::textures() const noexcept {
    return state_->textures;
}

std::span<const SamplerResource> SceneSnapshot::samplers() const noexcept {
    return state_->samplers;
}

std::span<const CameraResource> SceneSnapshot::cameras() const noexcept {
    return state_->cameras;
}

std::span<const LightResource> SceneSnapshot::lights() const noexcept {
    return state_->lights;
}

const ImageResource *SceneSnapshot::find_image(ImageId id) const noexcept {
    const auto found = std::lower_bound(state_->images.begin(), state_->images.end(), id,
                                        [](const ImageResource &resource, ImageId value) {
                                            return resource.id.value < value.value;
                                        });
    return found == state_->images.end() || found->id != id ? nullptr : &*found;
}

const TextureResource *SceneSnapshot::find_texture(TextureId id) const noexcept {
    const auto found = std::lower_bound(state_->textures.begin(), state_->textures.end(), id,
                                        [](const TextureResource &resource, TextureId value) {
                                            return resource.id.value < value.value;
                                        });
    return found == state_->textures.end() || found->id != id ? nullptr : &*found;
}

const SamplerResource *SceneSnapshot::find_sampler(SamplerId id) const noexcept {
    const auto found = std::lower_bound(state_->samplers.begin(), state_->samplers.end(), id,
                                        [](const SamplerResource &resource, SamplerId value) {
                                            return resource.id.value < value.value;
                                        });
    return found == state_->samplers.end() || found->id != id ? nullptr : &*found;
}

const CameraResource *SceneSnapshot::find_camera(CameraId id) const noexcept {
    const auto found = std::lower_bound(state_->cameras.begin(), state_->cameras.end(), id,
                                        [](const CameraResource &resource, CameraId value) {
                                            return resource.id.value < value.value;
                                        });
    return found == state_->cameras.end() || found->id != id ? nullptr : &*found;
}

const LightResource *SceneSnapshot::find_light(LightId id) const noexcept {
    const auto found = std::lower_bound(state_->lights.begin(), state_->lights.end(), id,
                                        [](const LightResource &resource, LightId value) {
                                            return resource.id.value < value.value;
                                        });
    return found == state_->lights.end() || found->id != id ? nullptr : &*found;
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

std::size_t vertex_format_size(nkscene_vertex_format format) noexcept {
    switch (format) {
    case NKS_VERTEX_FORMAT_FLOAT32X2:
        return sizeof(float) * 2;
    case NKS_VERTEX_FORMAT_FLOAT32X3:
        return sizeof(float) * 3;
    case NKS_VERTEX_FORMAT_FLOAT32X4:
        return sizeof(float) * 4;
    case NKS_VERTEX_FORMAT_UNORM8X4:
    case NKS_VERTEX_FORMAT_SNORM8X4:
        return sizeof(std::uint8_t) * 4;
    default:
        return 0;
    }
}

template <class Resource> struct ResourceCollector {
    std::vector<Resource> &resources;

    template <class Id> void operator()(Id, const Resource &resource) const {
        resources.push_back(resource);
    }
};

template <class Resource> struct ResourceIdLess {
    bool operator()(const Resource &lhs, const Resource &rhs) const {
        return lhs.id.value < rhs.id.value;
    }
};

template <class Store, class Resource>
void append_resources(const Store &store, std::vector<Resource> &resources) {
    store.for_each(ResourceCollector<Resource>{resources});
    std::sort(resources.begin(), resources.end(), ResourceIdLess<Resource>{});
}

std::uint32_t primitive_width(nkscene_primitive_type primitive) noexcept {
    switch (primitive) {
    case NKS_PRIMITIVE_TRIANGLES:
        return 3;
    case NKS_PRIMITIVE_LINES:
        return 2;
    case NKS_PRIMITIVE_POINTS:
        return 1;
    default:
        return 0;
    }
}

bool valid_vertex_semantic(nkscene_vertex_semantic semantic) noexcept {
    return semantic >= NKS_VERTEX_SEMANTIC_POSITION && semantic <= NKS_VERTEX_SEMANTIC_COLOR0;
}

std::size_t image_format_size(nkscene_image_format format) noexcept {
    switch (format) {
    case NKS_IMAGE_FORMAT_R8:
        return 1;
    case NKS_IMAGE_FORMAT_RGBA8:
        return 4;
    case NKS_IMAGE_FORMAT_RGBA16F:
        return 8;
    case NKS_IMAGE_FORMAT_R32F:
        return 4;
    default:
        return 0;
    }
}

bool valid_sampler_filter(nkscene_sampler_filter filter) noexcept {
    return filter == NKS_SAMPLER_FILTER_NEAREST || filter == NKS_SAMPLER_FILTER_LINEAR;
}

bool valid_sampler_wrap(nkscene_sampler_wrap wrap) noexcept {
    return wrap == NKS_SAMPLER_WRAP_REPEAT || wrap == NKS_SAMPLER_WRAP_CLAMP_TO_EDGE ||
           wrap == NKS_SAMPLER_WRAP_MIRRORED_REPEAT;
}

} // namespace

void Scene::recompute_world_transforms(ChangeSet &changes) {
    std::unordered_set<OccurrenceId> dirty;
    std::vector<OccurrenceId> pending;
    for (const auto &change : changes.changes) {
        if (has_domain(change.domains, ChangeDomain::Destroyed)) {
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
        hierarchy.for_each_child(occurrences.resolve(current),
                                [&](OccurrenceId child, OccurrenceHandle) {
                                    pending.push_back(child);
                                });
    }

    std::vector<OccurrenceId> roots;
    roots.reserve(dirty.size());
    for (const auto id : dirty) {
        const auto parent = hierarchy.parent_handle(occurrences.resolve(id));
        const auto parent_id = occurrences.id(parent);
        if (!parent.valid() || !dirty.contains(parent_id))
            roots.push_back(id);
    }
    std::vector<OccurrenceId> stack;
    for (const auto root : roots) {
        stack.push_back(root);
        while (!stack.empty()) {
            const auto current = stack.back();
            stack.pop_back();
            const auto handle = occurrences.resolve(current);
            const auto *local = local_transforms.find(handle);
            if (!local)
                continue;
            LocalTransform world = *local;
            const auto parent = hierarchy.parent_handle(handle);
            if (parent.valid()) {
                if (const auto *parent_world = world_transforms_.find(parent))
                    world = multiply(parent_world->transform, *local);
            }
            world_transforms_.insert_or_assign(handle,
                                               WorldTransform{world, revisions.scene + 1});
            ++changes.stats.dirty_world_transforms;
            if (const auto *geometry = geometry_refs.find(handle)) {
                const auto *resource = geometries.find(geometry->id);
                if (resource && resource->bounds.valid) {
                    bounds.insert_or_assign(handle, transformed_bounds(resource->bounds, world));
                    ++changes.stats.dirty_bounds;
                } else {
                    bounds.erase(handle);
                }
            } else {
                bounds.erase(handle);
            }
            hierarchy.for_each_child(handle, [&](OccurrenceId child, OccurrenceHandle) {
                if (dirty.contains(child))
                    stack.push_back(child);
            });
        }
    }
}

bool Scene::exists_after(const std::unordered_map<OccurrenceId, bool> &live,
                         OccurrenceId id) const noexcept {
    const auto found = live.find(id);
    return found == live.end() ? occurrences.contains(id) : found->second;
}

Scene::Scene() : hierarchy(occurrences) {
    publish_state();
}

GeometryId Scene::create_geometry() {
    const auto id = reserve_geometry_id();
    geometries.create(id);
    publish_state();
    return id;
}

MaterialId Scene::create_material() {
    const auto id = reserve_material_id();
    materials.create(id);
    publish_state();
    return id;
}

void Scene::destroy_geometry(GeometryId id) noexcept {
    if (geometries.destroy(id))
        publish_state();
}

void Scene::destroy_material(MaterialId id) noexcept {
    if (materials.destroy(id))
        publish_state();
}

void Scene::publish() const {
    publish_state();
}

void Scene::publish_state() const {
    auto state = std::make_shared<PublishedSceneState>();
    state->revisions = revisions;
    state->occurrences.reserve(occurrences.size());
    occurrences.for_each([&](OccurrenceId id, OccurrenceHandle handle) {
        SnapshotOccurrence occurrence;
        occurrence.occurrence = id;
        if (const auto *source = source_entities.find(handle))
            occurrence.source = source->id;
        if (const auto *name = names_.find(handle))
            occurrence.name = *name;
        occurrence.parent = hierarchy.parent(id);
        if (const auto *local = local_transforms.find(handle))
            occurrence.local_transform = *local;
        if (const auto *world = world_transforms_.find(handle))
            occurrence.world_transform = *world;
        if (const auto *geometry = geometry_refs.find(handle))
            occurrence.geometry = geometry->id;
        if (const auto *material = material_refs.find(handle))
            occurrence.material = material->id;
        if (const auto *camera = camera_refs_.find(handle))
            occurrence.camera = camera->id;
        if (const auto *light = light_refs_.find(handle))
            occurrence.light = light->id;
        if (const auto *visibility = visibilities_.find(handle))
            occurrence.visible = visibility->visible;
        if (const auto *bound = bounds.find(handle))
            occurrence.bounds = *bound;
        state->occurrences.push_back(occurrence);
    });
    std::sort(state->occurrences.begin(), state->occurrences.end(),
              [](const SnapshotOccurrence &lhs, const SnapshotOccurrence &rhs) {
                  return lhs.occurrence.value < rhs.occurrence.value;
              });
    state->occurrences_by_source.reserve(state->occurrences.size());
    for (const auto &occurrence : state->occurrences)
        state->occurrences_by_source[occurrence.source].push_back(occurrence.occurrence);
    state->entity_names = entity_names;
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
    append_resources(images, state->images);
    append_resources(textures, state->textures);
    append_resources(samplers, state->samplers);
    append_resources(cameras, state->cameras);
    append_resources(lights, state->lights);
    std::shared_ptr<const PublishedSceneState> published = std::move(state);
    std::atomic_store_explicit(&published_, std::move(published), std::memory_order_release);
}

SceneSnapshot Scene::snapshot() const {
    return SceneSnapshot(
        std::atomic_load_explicit(&published_, std::memory_order_acquire));
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
                } else if constexpr (std::is_same_v<T, SetCamera>) {
                    if (!value.occurrence.valid() || !exists_after(live, value.occurrence))
                        result = NKS_ERROR_STALE_ID;
                } else if constexpr (std::is_same_v<T, SetLight>) {
                    if (!value.occurrence.valid() || !exists_after(live, value.occurrence))
                        result = NKS_ERROR_STALE_ID;
                } else if constexpr (std::is_same_v<T, SetVisibility>) {
                    if (!value.occurrence.valid() || !exists_after(live, value.occurrence))
                        result = NKS_ERROR_STALE_ID;
                } else if constexpr (std::is_same_v<T, SetSourceEntity>) {
                    if (!value.occurrence.valid() || !exists_after(live, value.occurrence))
                        result = NKS_ERROR_STALE_ID;
                } else if constexpr (std::is_same_v<T, SetName>) {
                    if (!value.occurrence.valid() || !exists_after(live, value.occurrence))
                        result = NKS_ERROR_STALE_ID;
                } else if constexpr (std::is_same_v<T, SetEntityName>) {
                    if (!value.entity.valid())
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
    bool name_changed = false;
    std::unordered_map<OccurrenceId, std::size_t> change_indices;
    change_indices.reserve(transaction.mutations().size());
    for (const auto &mutation : transaction.mutations()) {
        std::visit(
            [&](const auto &value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, CreateOccurrence>) {
                    const auto handle = occurrences.create(value.occurrence);
                    hierarchy.add(handle);
                    local_transforms.insert_or_assign(handle, LocalTransform{});
                    visibilities_.insert_or_assign(handle, Visibility{});
                    record_change(changes, change_indices, value.occurrence, ChangeDomain::Created);
                } else if constexpr (std::is_same_v<T, DestroyOccurrence>) {
                    const auto handle = occurrences.resolve(value.occurrence);
                    source_entities.erase(handle);
                    parent_components.erase(handle);
                    local_transforms.erase(handle);
                    world_transforms_.erase(handle);
                    geometry_refs.erase(handle);
                    material_refs.erase(handle);
                    camera_refs_.erase(handle);
                    light_refs_.erase(handle);
                    visibilities_.erase(handle);
                    names_.erase(handle);
                    bounds.erase(handle);
                    hierarchy.remove(handle);
                    occurrences.destroy(value.occurrence);
                    record_change(changes, change_indices, value.occurrence,
                                  ChangeDomain::Destroyed);
                } else if constexpr (std::is_same_v<T, SetParent>) {
                    const auto previous = hierarchy.parent(value.occurrence);
                    if (previous != value.parent) {
                        hierarchy.reparent(occurrences.resolve(value.occurrence),
                                           occurrences.resolve(value.parent));
                        parent_components.insert_or_assign(occurrences.resolve(value.occurrence),
                                                           Parent{value.parent});
                        record_change(changes, change_indices, value.occurrence,
                                      ChangeDomain::Hierarchy);
                    }
                } else if constexpr (std::is_same_v<T, SetTransform>) {
                    const auto handle = occurrences.resolve(value.occurrence);
                    auto *previous = local_transforms.find(handle);
                    if (!previous || !transform_equal(*previous, value.transform)) {
                        local_transforms.insert_or_assign(handle, value.transform);
                        record_change(changes, change_indices, value.occurrence,
                                      ChangeDomain::Transform);
                    }
                } else if constexpr (std::is_same_v<T, SetGeometry>) {
                    const auto handle = occurrences.resolve(value.occurrence);
                    const auto *previous = geometry_refs.find(handle);
                    if (!previous || previous->id != value.geometry) {
                        geometry_refs.insert_or_assign(handle,
                                                       GeometryRef{value.geometry});
                        record_change(changes, change_indices, value.occurrence,
                                      ChangeDomain::Geometry);
                    }
                } else if constexpr (std::is_same_v<T, SetMaterial>) {
                    const auto handle = occurrences.resolve(value.occurrence);
                    const auto *previous = material_refs.find(handle);
                    if (!previous || previous->id != value.material) {
                        material_refs.insert_or_assign(handle,
                                                       MaterialRef{value.material});
                        record_change(changes, change_indices, value.occurrence,
                                      ChangeDomain::Material);
                    }
                } else if constexpr (std::is_same_v<T, SetCamera>) {
                    const auto handle = occurrences.resolve(value.occurrence);
                    const auto *previous = camera_refs_.find(handle);
                    if (value.camera.valid()) {
                        if (!previous || previous->id != value.camera) {
                            camera_refs_.insert_or_assign(handle,
                                                          CameraRef{value.camera});
                            record_change(changes, change_indices, value.occurrence,
                                          ChangeDomain::Camera);
                        }
                    } else if (previous) {
                        camera_refs_.erase(handle);
                        record_change(changes, change_indices, value.occurrence,
                                      ChangeDomain::Camera);
                    }
                } else if constexpr (std::is_same_v<T, SetLight>) {
                    const auto handle = occurrences.resolve(value.occurrence);
                    const auto *previous = light_refs_.find(handle);
                    if (value.light.valid()) {
                        if (!previous || previous->id != value.light) {
                            light_refs_.insert_or_assign(handle, LightRef{value.light});
                            record_change(changes, change_indices, value.occurrence,
                                          ChangeDomain::Light);
                        }
                    } else if (previous) {
                        light_refs_.erase(handle);
                        record_change(changes, change_indices, value.occurrence,
                                      ChangeDomain::Light);
                    }
                } else if constexpr (std::is_same_v<T, SetVisibility>) {
                    const auto handle = occurrences.resolve(value.occurrence);
                    const auto *previous = visibilities_.find(handle);
                    if (!previous || previous->visible != value.visible) {
                        visibilities_.insert_or_assign(handle, Visibility{value.visible});
                        record_change(changes, change_indices, value.occurrence,
                                      ChangeDomain::Visibility);
                    }
                } else if constexpr (std::is_same_v<T, SetSourceEntity>) {
                    const auto handle = occurrences.resolve(value.occurrence);
                    const auto *previous = source_entities.find(handle);
                    if (value.source.valid()) {
                        if (!previous || previous->id != value.source) {
                            source_entities.insert_or_assign(handle,
                                                             SourceEntity{value.source});
                            record_change(changes, change_indices, value.occurrence,
                                          ChangeDomain::Source);
                        }
                    } else if (previous) {
                        source_entities.erase(handle);
                        record_change(changes, change_indices, value.occurrence,
                                      ChangeDomain::Source);
                    }
                } else if constexpr (std::is_same_v<T, SetName>) {
                    const auto handle = occurrences.resolve(value.occurrence);
                    const auto *previous = names_.find(handle);
                    if (value.name.empty()) {
                        if (previous) {
                            names_.erase(handle);
                            name_changed = true;
                            record_change(changes, change_indices, value.occurrence,
                                          ChangeDomain::Name);
                        }
                    } else if (!previous || *previous != value.name) {
                        names_.insert_or_assign(handle, value.name);
                        name_changed = true;
                        record_change(changes, change_indices, value.occurrence,
                                      ChangeDomain::Name);
                    }
                } else if constexpr (std::is_same_v<T, SetEntityName>) {
                    const auto previous = entity_names.find(value.entity);
                    bool changed = false;
                    if (value.name.empty()) {
                        if (previous != entity_names.end()) {
                            entity_names.erase(previous);
                            changed = true;
                        }
                    } else if (previous == entity_names.end() || previous->second != value.name) {
                        entity_names.insert_or_assign(value.entity, value.name);
                        changed = true;
                    }
                    if (changed) {
                        name_changed = true;
                        occurrences.for_each([&](OccurrenceId occurrence, OccurrenceHandle handle) {
                            const auto *source = source_entities.find(handle);
                            if (source && source->id == value.entity)
                                record_change(changes, change_indices, occurrence,
                                              ChangeDomain::Name);
                        });
                    }
                }
            },
            mutation);
    }
    changes.stats.changed_occurrences = changes.changes.size();
    recompute_world_transforms(changes);
    if (!changes.changes.empty() || name_changed) {
        auto &revision = revisions;
        ++revision.scene;
        bool hierarchy_changed = false;
        bool transform_changed = false;
        bool geometry_changed = false;
        bool material_changed = false;
        bool visibility_changed = false;
        bool bounds_changed = false;
        bool source_changed = false;
        bool names_changed = name_changed;
        bool camera_changed = false;
        bool light_changed = false;
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
            source_changed = source_changed || has_domain(change.domains, ChangeDomain::Source);
            names_changed = names_changed || has_domain(change.domains, ChangeDomain::Name);
            camera_changed = camera_changed || has_domain(change.domains, ChangeDomain::Camera);
            light_changed = light_changed || has_domain(change.domains, ChangeDomain::Light);
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
        if (source_changed)
            ++revision.source;
        if (names_changed)
            ++revision.name;
        if (camera_changed)
            ++revision.camera;
        if (light_changed)
            ++revision.light;
    }
    changes.scene_revision = revisions.scene;
    changes.revisions = revisions;
    publish_state();
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
    target.camera.value = source.camera.value;
    target.light.value = source.light.value;
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

nkscene_result NKS_CALL nkscene_tx_set_transforms(nkscene_transaction handle,
                                                  const nkscene_transform_update *updates,
                                                  uint32_t update_count) {
    if (update_count != 0 && !updates)
        return NKS_ERROR_INVALID_ARGUMENT;
    auto &state = nkscene::registry();
    std::lock_guard lock(state.mutex);
    std::shared_ptr<nkscene::Transaction> transaction;
    const auto result = nkscene::require_transaction(handle, transaction);
    if (result != NKS_OK)
        return result;
    std::vector<nkscene::TransformUpdate> converted;
    converted.reserve(update_count);
    for (uint32_t index = 0; index < update_count; ++index) {
        converted.push_back({{updates[index].occurrence.value},
                             nkscene::from_public_transform(updates[index].transform)});
    }
    transaction->add_transforms(std::span<const nkscene::TransformUpdate>{converted});
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

nkscene_result NKS_CALL nkscene_tx_set_camera(nkscene_transaction handle,
                                              nkscene_occurrence_id occurrence,
                                              nkscene_camera_id camera) {
    auto &state = nkscene::registry();
    std::lock_guard lock(state.mutex);
    std::shared_ptr<nkscene::Transaction> transaction;
    const auto result = nkscene::require_transaction(handle, transaction);
    if (result != NKS_OK)
        return result;
    transaction->add_camera({occurrence.value}, {camera.value});
    return NKS_OK;
}

nkscene_result NKS_CALL nkscene_tx_set_light(nkscene_transaction handle,
                                             nkscene_occurrence_id occurrence,
                                             nkscene_light_id light) {
    auto &state = nkscene::registry();
    std::lock_guard lock(state.mutex);
    std::shared_ptr<nkscene::Transaction> transaction;
    const auto result = nkscene::require_transaction(handle, transaction);
    if (result != NKS_OK)
        return result;
    transaction->add_light({occurrence.value}, {light.value});
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

nkscene_result NKS_CALL nkscene_tx_set_source_entity(nkscene_transaction handle,
                                                     nkscene_occurrence_id occurrence,
                                                     nkscene_entity_id source) {
    auto &state = nkscene::registry();
    std::lock_guard lock(state.mutex);
    std::shared_ptr<nkscene::Transaction> transaction;
    const auto result = nkscene::require_transaction(handle, transaction);
    if (result != NKS_OK)
        return result;
    transaction->add_source_entity({occurrence.value}, {source.value});
    return NKS_OK;
}

nkscene_result NKS_CALL nkscene_tx_set_name(nkscene_transaction handle,
                                            nkscene_occurrence_id occurrence, const char *name) {
    if (!name)
        return NKS_ERROR_INVALID_ARGUMENT;
    auto &state = nkscene::registry();
    std::lock_guard lock(state.mutex);
    std::shared_ptr<nkscene::Transaction> transaction;
    const auto result = nkscene::require_transaction(handle, transaction);
    if (result != NKS_OK)
        return result;
    transaction->add_name({occurrence.value}, name);
    return NKS_OK;
}

nkscene_result NKS_CALL nkscene_tx_set_entity_name(nkscene_transaction handle,
                                                   nkscene_entity_id entity, const char *name) {
    if (!entity.value || !name)
        return NKS_ERROR_INVALID_ARGUMENT;
    auto &state = nkscene::registry();
    std::lock_guard lock(state.mutex);
    std::shared_ptr<nkscene::Transaction> transaction;
    const auto result = nkscene::require_transaction(handle, transaction);
    if (result != NKS_OK)
        return result;
    transaction->add_entity_name({entity.value}, name);
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

nkscene_result NKS_CALL nkscene_snapshot_get_occurrence_count(nkscene_snapshot snapshot,
                                                              uint64_t *out_count) {
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
    nkscene_snapshot snapshot, uint64_t index, nkscene_snapshot_occurrence *out_occurrence) {
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

nkscene_result NKS_CALL nkscene_snapshot_get_occurrence_page(
    nkscene_snapshot snapshot, uint64_t start_index, nkscene_snapshot_occurrence_page *out_page) {
    if (!out_page || out_page->struct_size < sizeof(nkscene_snapshot_occurrence_page))
        return NKS_ERROR_INVALID_ARGUMENT;
    auto &state = nkscene::registry();
    std::lock_guard lock(state.mutex);
    const auto value = state.snapshots.get(nkscene::unpack_handle(snapshot));
    if (!value)
        return NKS_ERROR_INVALID_HANDLE;
    const auto occurrences = value->occurrences();
    out_page->start_index = start_index;
    if (start_index >= occurrences.size()) {
        out_page->count = 0;
        return NKS_OK;
    }
    const auto remaining = occurrences.size() - static_cast<std::size_t>(start_index);
    const auto count =
        std::min<std::size_t>(remaining, NKS_SCENE_SNAPSHOT_OCCURRENCE_PAGE_CAPACITY);
    out_page->count = static_cast<uint32_t>(count);
    for (std::size_t index = 0; index < count; ++index)
        nkscene::copy_snapshot_occurrence(
            occurrences[static_cast<std::size_t>(start_index) + index],
            out_page->occurrences[index]);
    return NKS_OK;
}

nkscene_result NKS_CALL nkscene_snapshot_get_source_occurrence_count(nkscene_snapshot snapshot,
                                                                     nkscene_entity_id source,
                                                                     uint64_t *out_count) {
    if (!out_count)
        return NKS_ERROR_INVALID_ARGUMENT;
    auto &state = nkscene::registry();
    std::lock_guard lock(state.mutex);
    const auto value = state.snapshots.get(nkscene::unpack_handle(snapshot));
    if (!value)
        return NKS_ERROR_INVALID_HANDLE;
    *out_count = value->occurrences_for_source({source.value}).size();
    return NKS_OK;
}

nkscene_result NKS_CALL
nkscene_snapshot_get_source_occurrence(nkscene_snapshot snapshot, nkscene_entity_id source,
                                       uint64_t index, nkscene_occurrence_id *out_occurrence) {
    if (!out_occurrence)
        return NKS_ERROR_INVALID_ARGUMENT;
    auto &state = nkscene::registry();
    std::lock_guard lock(state.mutex);
    const auto value = state.snapshots.get(nkscene::unpack_handle(snapshot));
    if (!value)
        return NKS_ERROR_INVALID_HANDLE;
    const auto occurrences = value->occurrences_for_source({source.value});
    if (index >= occurrences.size())
        return NKS_ERROR_INVALID_ARGUMENT;
    *out_occurrence = {occurrences[static_cast<std::size_t>(index)].value};
    return NKS_OK;
}

nkscene_result NKS_CALL nkscene_snapshot_get_name(nkscene_snapshot snapshot,
                                                  nkscene_occurrence_id occurrence,
                                                  const char **out_name) {
    if (!out_name)
        return NKS_ERROR_INVALID_ARGUMENT;
    *out_name = nullptr;
    auto &state = nkscene::registry();
    std::lock_guard lock(state.mutex);
    const auto value = state.snapshots.get(nkscene::unpack_handle(snapshot));
    if (!value)
        return NKS_ERROR_INVALID_HANDLE;
    const auto *info = value->find({occurrence.value});
    if (!info)
        return NKS_ERROR_STALE_ID;
    *out_name = info->name.c_str();
    return NKS_OK;
}

nkscene_result NKS_CALL nkscene_snapshot_get_entity_name(nkscene_snapshot snapshot,
                                                         nkscene_entity_id entity,
                                                         const char **out_name) {
    if (!out_name || !entity.value)
        return NKS_ERROR_INVALID_ARGUMENT;
    *out_name = nullptr;
    auto &state = nkscene::registry();
    std::lock_guard lock(state.mutex);
    const auto value = state.snapshots.get(nkscene::unpack_handle(snapshot));
    if (!value)
        return NKS_ERROR_INVALID_HANDLE;
    const auto name = value->entity_name({entity.value});
    *out_name = name.empty() ? "" : name.data();
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
    constexpr auto minimum_size =
        offsetof(nkscene_geometry_data, subelement_count) + sizeof(data->subelement_count);
    if (!data || data->struct_size < minimum_size)
        return NKS_ERROR_INVALID_ARGUMENT;
    const auto has_stream_fields = data->struct_size >= sizeof(nkscene_geometry_data);
    const auto stream_count = has_stream_fields ? data->stream_count : 0;
    auto primitive = NKS_PRIMITIVE_TRIANGLES;
    if (has_stream_fields && data->primitive_type != 0)
        primitive = data->primitive_type;
    const auto width = nkscene::primitive_width(primitive);
    if (!width || (stream_count != 0 && !data->streams) ||
        (stream_count == 0 && data->vertex_count != 0 && !data->vertices) ||
        (data->index_count != 0 && !data->indices) ||
        (data->subelement_count != 0 && !data->subelements))
        return NKS_ERROR_INVALID_ARGUMENT;

    std::vector<nkscene::GeometryVertexStream> streams;
    std::vector<nkscene::GeometryVertex> vertices;
    std::uint32_t vertex_count = data->vertex_count;
    if (stream_count != 0) {
        std::unordered_set<nkscene_vertex_semantic> semantics;
        streams.reserve(stream_count);
        vertex_count = 0;
        bool position_found = false;
        for (uint32_t index = 0; index < stream_count; ++index) {
            const auto &input = data->streams[index];
            if (input.struct_size < sizeof(nkscene_vertex_stream) ||
                !nkscene::valid_vertex_semantic(input.semantic))
                return NKS_ERROR_INVALID_ARGUMENT;
            const auto element_size = nkscene::vertex_format_size(input.format);
            if (!element_size || !semantics.insert(input.semantic).second)
                return NKS_ERROR_INVALID_ARGUMENT;
            const auto stride = input.stride == 0 ? element_size : input.stride;
            if (stride < element_size || (input.count != 0 && !input.data) ||
                input.count > std::numeric_limits<std::size_t>::max() / stride)
                return NKS_ERROR_INVALID_ARGUMENT;
            if (input.semantic == NKS_VERTEX_SEMANTIC_POSITION) {
                if (input.format != NKS_VERTEX_FORMAT_FLOAT32X3)
                    return NKS_ERROR_INVALID_ARGUMENT;
                vertex_count = input.count;
                position_found = true;
            }
            nkscene::GeometryVertexStream output;
            output.semantic = static_cast<nkscene::VertexSemantic>(input.semantic);
            output.format = static_cast<nkscene::VertexFormat>(input.format);
            output.stride = stride;
            output.count = input.count;
            output.data.resize(static_cast<std::size_t>(stride) * input.count);
            if (!output.data.empty())
                std::memcpy(output.data.data(), input.data, output.data.size());
            streams.push_back(std::move(output));
        }
        if (!position_found)
            return NKS_ERROR_INVALID_ARGUMENT;
        for (const auto &stream : streams)
            if (stream.count != vertex_count)
                return NKS_ERROR_INVALID_ARGUMENT;

        vertices.resize(vertex_count);
        const auto position = std::find_if(streams.begin(), streams.end(), [](const auto &stream) {
            return stream.semantic == nkscene::VertexSemantic::Position;
        });
        for (std::uint32_t index = 0; index < vertex_count; ++index) {
            std::memcpy(vertices[index].position.data(),
                        position->data.data() + static_cast<std::size_t>(index) * position->stride,
                        sizeof(vertices[index].position));
        }
    } else {
        vertices.resize(vertex_count);
        for (uint32_t index = 0; index < vertex_count; ++index)
            std::copy(std::begin(data->vertices[index].position),
                      std::end(data->vertices[index].position), vertices[index].position.begin());
        nkscene::GeometryVertexStream position;
        position.semantic = nkscene::VertexSemantic::Position;
        position.format = nkscene::VertexFormat::Float32x3;
        position.stride = sizeof(nkscene::GeometryVertex);
        position.count = vertex_count;
        position.data.resize(static_cast<std::size_t>(position.stride) * vertex_count);
        if (!position.data.empty())
            std::memcpy(position.data.data(), vertices.data(), position.data.size());
        streams.push_back(std::move(position));
    }

    const auto element_count = data->index_count != 0 ? data->index_count : vertex_count;
    if (element_count % width != 0)
        return NKS_ERROR_INVALID_ARGUMENT;
    const auto *indices = static_cast<const uint32_t *>(data->indices);
    if (data->index_count != 0) {
        for (uint32_t index = 0; index < data->index_count; ++index)
            if (indices[index] >= vertex_count)
                return NKS_ERROR_INVALID_ARGUMENT;
    }
    const auto primitive_count = element_count / width;
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
    auto &payload = resource.edit_payload();
    payload.vertices = std::move(vertices);
    payload.streams = std::move(streams);
    payload.primitive_type = static_cast<nkscene::PrimitiveType>(primitive);
    payload.indices.clear();
    if (data->index_count != 0)
        payload.indices.assign(indices, indices + data->index_count);
    resource.bounds.valid = data->bounds.valid != 0;
    std::copy(std::begin(data->bounds.minimum), std::end(data->bounds.minimum),
              resource.bounds.minimum.begin());
    std::copy(std::begin(data->bounds.maximum), std::end(data->bounds.maximum),
              resource.bounds.maximum.begin());
    auto &subelements = resource.edit_subelements();
    subelements.ranges.clear();
    if (data->subelement_count != 0) {
        subelements.ranges.reserve(data->subelement_count);
        for (uint32_t index = 0; index < data->subelement_count; ++index) {
            const auto &range = data->subelements[index];
            subelements.ranges.push_back(
                {range.first_primitive, range.primitive_count, range.subelement});
        }
    }
    owner->publish();
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
    auto &material_state = resource.edit_state();
    std::copy(std::begin(data->base_color), std::end(data->base_color),
              material_state.base_color.begin());
    material_state.opacity = data->opacity;
    material_state.flags = data->flags;
    material_state.metallic = data->metallic;
    material_state.roughness = data->roughness;
    std::copy(std::begin(data->emissive), std::end(data->emissive),
              material_state.emissive.begin());
    material_state.alpha_cutoff = data->alpha_cutoff;
    material_state.alpha_mode = static_cast<nkscene::AlphaMode>(
        data->alpha_mode == 0 ? NKS_MATERIAL_ALPHA_OPAQUE : data->alpha_mode);
    material_state.base_color_texture = {data->base_color_texture.value};
    material_state.metallic_roughness_texture = {data->metallic_roughness_texture.value};
    material_state.normal_texture = {data->normal_texture.value};
    material_state.emissive_texture = {data->emissive_texture.value};
    material_state.occlusion_texture = {data->occlusion_texture.value};
    material_state.sampler = {data->sampler.value};
    const nkscene::TextureId textures[] = {
        material_state.base_color_texture, material_state.metallic_roughness_texture,
        material_state.normal_texture, material_state.emissive_texture,
        material_state.occlusion_texture};
    for (const auto texture : textures)
        if (texture.valid() && !owner->texture_store().find(texture))
            return NKS_ERROR_STALE_ID;
    if (material_state.sampler.valid() && !owner->sampler_store().find(material_state.sampler))
        return NKS_ERROR_STALE_ID;
    owner->publish();
    return NKS_OK;
}

nkscene_result NKS_CALL nkscene_image_create(nkscene_scene scene, nkscene_image_id *out_image) {
    if (!out_image)
        return NKS_ERROR_INVALID_ARGUMENT;
    auto &state = nkscene::registry();
    std::lock_guard lock(state.mutex);
    auto owner = state.scenes.get(nkscene::unpack_handle(scene));
    if (!owner)
        return NKS_ERROR_INVALID_HANDLE;
    out_image->value = owner->create_image().value;
    return NKS_OK;
}

void NKS_CALL nkscene_image_destroy(nkscene_scene scene, nkscene_image_id image) {
    auto &state = nkscene::registry();
    std::lock_guard lock(state.mutex);
    auto owner = state.scenes.get(nkscene::unpack_handle(scene));
    if (owner)
        owner->destroy_image({image.value});
}

nkscene_result NKS_CALL nkscene_image_set_data(nkscene_scene scene, nkscene_image_id image,
                                               const nkscene_image_data *data) {
    if (!data || data->struct_size < sizeof(nkscene_image_data) || !data->width || !data->height ||
        !nkscene::image_format_size(data->format) || (data->data_size != 0 && !data->data))
        return NKS_ERROR_INVALID_ARGUMENT;
    const auto mip_count = data->mip_count == 0 ? 1u : data->mip_count;
    auto &state = nkscene::registry();
    std::lock_guard lock(state.mutex);
    auto owner = state.scenes.get(nkscene::unpack_handle(scene));
    if (!owner)
        return NKS_ERROR_INVALID_HANDLE;
    auto *resource = owner->image_store().find({image.value});
    if (!resource)
        return NKS_ERROR_STALE_ID;
    resource = &owner->image_store().create({image.value});
    resource->width = data->width;
    resource->height = data->height;
    resource->format = static_cast<nkscene::ImageFormat>(data->format);
    resource->mip_count = mip_count;
    resource->data.resize(data->data_size);
    if (!resource->data.empty())
        std::memcpy(resource->data.data(), data->data, resource->data.size());
    return NKS_OK;
}

nkscene_result NKS_CALL nkscene_texture_create(nkscene_scene scene,
                                               nkscene_texture_id *out_texture) {
    if (!out_texture)
        return NKS_ERROR_INVALID_ARGUMENT;
    auto &state = nkscene::registry();
    std::lock_guard lock(state.mutex);
    auto owner = state.scenes.get(nkscene::unpack_handle(scene));
    if (!owner)
        return NKS_ERROR_INVALID_HANDLE;
    out_texture->value = owner->create_texture().value;
    return NKS_OK;
}

void NKS_CALL nkscene_texture_destroy(nkscene_scene scene, nkscene_texture_id texture) {
    auto &state = nkscene::registry();
    std::lock_guard lock(state.mutex);
    auto owner = state.scenes.get(nkscene::unpack_handle(scene));
    if (owner)
        owner->destroy_texture({texture.value});
}

nkscene_result NKS_CALL nkscene_texture_set_data(nkscene_scene scene, nkscene_texture_id texture,
                                                 const nkscene_texture_data *data) {
    if (!data || data->struct_size < sizeof(nkscene_texture_data))
        return NKS_ERROR_INVALID_ARGUMENT;
    auto &state = nkscene::registry();
    std::lock_guard lock(state.mutex);
    auto owner = state.scenes.get(nkscene::unpack_handle(scene));
    if (!owner)
        return NKS_ERROR_INVALID_HANDLE;
    if (!owner->texture_store().find({texture.value}) ||
        (data->image.value && !owner->image_store().find({data->image.value})))
        return NKS_ERROR_STALE_ID;
    auto &resource = owner->texture_store().create({texture.value});
    resource.image = {data->image.value};
    return NKS_OK;
}

nkscene_result NKS_CALL nkscene_sampler_create(nkscene_scene scene,
                                               nkscene_sampler_id *out_sampler) {
    if (!out_sampler)
        return NKS_ERROR_INVALID_ARGUMENT;
    auto &state = nkscene::registry();
    std::lock_guard lock(state.mutex);
    auto owner = state.scenes.get(nkscene::unpack_handle(scene));
    if (!owner)
        return NKS_ERROR_INVALID_HANDLE;
    out_sampler->value = owner->create_sampler().value;
    return NKS_OK;
}

void NKS_CALL nkscene_sampler_destroy(nkscene_scene scene, nkscene_sampler_id sampler) {
    auto &state = nkscene::registry();
    std::lock_guard lock(state.mutex);
    auto owner = state.scenes.get(nkscene::unpack_handle(scene));
    if (owner)
        owner->destroy_sampler({sampler.value});
}

nkscene_result NKS_CALL nkscene_sampler_set_data(nkscene_scene scene, nkscene_sampler_id sampler,
                                                 const nkscene_sampler_data *data) {
    if (!data || data->struct_size < sizeof(nkscene_sampler_data) ||
        !nkscene::valid_sampler_filter(data->min_filter) ||
        !nkscene::valid_sampler_filter(data->mag_filter) ||
        !nkscene::valid_sampler_wrap(data->wrap_u) || !nkscene::valid_sampler_wrap(data->wrap_v) ||
        !nkscene::valid_sampler_wrap(data->wrap_w) || data->max_anisotropy < 1.0f)
        return NKS_ERROR_INVALID_ARGUMENT;
    auto &state = nkscene::registry();
    std::lock_guard lock(state.mutex);
    auto owner = state.scenes.get(nkscene::unpack_handle(scene));
    if (!owner)
        return NKS_ERROR_INVALID_HANDLE;
    if (!owner->sampler_store().find({sampler.value}))
        return NKS_ERROR_STALE_ID;
    auto &resource = owner->sampler_store().create({sampler.value});
    resource.min_filter = static_cast<nkscene::SamplerFilter>(data->min_filter);
    resource.mag_filter = static_cast<nkscene::SamplerFilter>(data->mag_filter);
    resource.wrap_u = static_cast<nkscene::SamplerWrap>(data->wrap_u);
    resource.wrap_v = static_cast<nkscene::SamplerWrap>(data->wrap_v);
    resource.wrap_w = static_cast<nkscene::SamplerWrap>(data->wrap_w);
    resource.max_anisotropy = data->max_anisotropy;
    return NKS_OK;
}

nkscene_result NKS_CALL nkscene_camera_create(nkscene_scene scene, nkscene_camera_id *out_camera) {
    if (!out_camera)
        return NKS_ERROR_INVALID_ARGUMENT;
    auto &state = nkscene::registry();
    std::lock_guard lock(state.mutex);
    auto owner = state.scenes.get(nkscene::unpack_handle(scene));
    if (!owner)
        return NKS_ERROR_INVALID_HANDLE;
    out_camera->value = owner->create_camera().value;
    return NKS_OK;
}

void NKS_CALL nkscene_camera_destroy(nkscene_scene scene, nkscene_camera_id camera) {
    auto &state = nkscene::registry();
    std::lock_guard lock(state.mutex);
    auto owner = state.scenes.get(nkscene::unpack_handle(scene));
    if (owner)
        owner->destroy_camera({camera.value});
}

nkscene_result NKS_CALL nkscene_camera_set_data(nkscene_scene scene, nkscene_camera_id camera,
                                                const nkscene_camera_data *data) {
    if (!data || data->struct_size < sizeof(nkscene_camera_data) ||
        (data->projection != NKS_CAMERA_PERSPECTIVE &&
         data->projection != NKS_CAMERA_ORTHOGRAPHIC) ||
        data->near_plane <= 0.0f || data->far_plane <= data->near_plane ||
        (data->projection == NKS_CAMERA_PERSPECTIVE && data->fov_y <= 0.0f) ||
        (data->projection == NKS_CAMERA_ORTHOGRAPHIC && data->orthographic_height <= 0.0f) ||
        data->aspect_ratio < 0.0f)
        return NKS_ERROR_INVALID_ARGUMENT;
    auto &state = nkscene::registry();
    std::lock_guard lock(state.mutex);
    auto owner = state.scenes.get(nkscene::unpack_handle(scene));
    if (!owner)
        return NKS_ERROR_INVALID_HANDLE;
    if (!owner->camera_store().find({camera.value}))
        return NKS_ERROR_STALE_ID;
    auto &resource = owner->camera_store().create({camera.value});
    resource.projection = static_cast<nkscene::CameraProjection>(data->projection);
    resource.fov_y = data->fov_y;
    resource.orthographic_height = data->orthographic_height;
    resource.near_plane = data->near_plane;
    resource.far_plane = data->far_plane;
    resource.aspect_ratio = data->aspect_ratio;
    return NKS_OK;
}

nkscene_result NKS_CALL nkscene_light_create(nkscene_scene scene, nkscene_light_id *out_light) {
    if (!out_light)
        return NKS_ERROR_INVALID_ARGUMENT;
    auto &state = nkscene::registry();
    std::lock_guard lock(state.mutex);
    auto owner = state.scenes.get(nkscene::unpack_handle(scene));
    if (!owner)
        return NKS_ERROR_INVALID_HANDLE;
    out_light->value = owner->create_light().value;
    return NKS_OK;
}

void NKS_CALL nkscene_light_destroy(nkscene_scene scene, nkscene_light_id light) {
    auto &state = nkscene::registry();
    std::lock_guard lock(state.mutex);
    auto owner = state.scenes.get(nkscene::unpack_handle(scene));
    if (owner)
        owner->destroy_light({light.value});
}

nkscene_result NKS_CALL nkscene_light_set_data(nkscene_scene scene, nkscene_light_id light,
                                               const nkscene_light_data *data) {
    if (!data || data->struct_size < sizeof(nkscene_light_data) ||
        (data->type != NKS_LIGHT_DIRECTIONAL && data->type != NKS_LIGHT_POINT &&
         data->type != NKS_LIGHT_SPOT) ||
        data->intensity < 0.0f || data->range <= 0.0f || data->inner_cone_angle < 0.0f ||
        data->outer_cone_angle < data->inner_cone_angle)
        return NKS_ERROR_INVALID_ARGUMENT;
    auto &state = nkscene::registry();
    std::lock_guard lock(state.mutex);
    auto owner = state.scenes.get(nkscene::unpack_handle(scene));
    if (!owner)
        return NKS_ERROR_INVALID_HANDLE;
    if (!owner->light_store().find({light.value}))
        return NKS_ERROR_STALE_ID;
    auto &resource = owner->light_store().create({light.value});
    resource.type = static_cast<nkscene::LightType>(data->type);
    std::copy(std::begin(data->color), std::end(data->color), resource.color.begin());
    resource.intensity = data->intensity;
    resource.range = data->range;
    resource.inner_cone_angle = data->inner_cone_angle;
    resource.outer_cone_angle = data->outer_cone_angle;
    return NKS_OK;
}

} // extern "C"
