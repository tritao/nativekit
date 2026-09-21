#pragma once

#include "nativekit_scene.h"

#include "changeset.hpp"
#include "component_store.hpp"
#include "hierarchy.hpp"
#include "occurrence_store.hpp"
#include "resources.hpp"
#include "transaction.hpp"

#include <cstdint>
#include <array>
#include <memory>
#include <string>
#include <span>
#include <unordered_map>
#include <vector>

namespace nkscene {

constexpr std::size_t published_occurrence_page_capacity = 256;

struct SnapshotMaterialization;

struct PublishedResourceDelta {
    std::uint64_t geometry_revision = 0;
    std::uint64_t material_revision = 0;
    std::vector<GeometryId> geometries;
    std::vector<MaterialId> materials;
    std::shared_ptr<const PublishedResourceDelta> previous;
};

struct PublishedOccurrencePage {
    std::array<SnapshotOccurrence, published_occurrence_page_capacity> values{};
};

struct PublishedOccurrenceState {
    std::size_t slot_count = 0;
    std::vector<std::shared_ptr<const PublishedOccurrencePage>> pages;
    mutable std::shared_ptr<const std::unordered_map<OccurrenceId, const SnapshotOccurrence *>>
        lookup;
    mutable std::shared_ptr<const SnapshotMaterialization> materialized;
};

struct SnapshotMaterialization {
    std::vector<SnapshotOccurrence> occurrences;
    std::unordered_map<OccurrenceId, std::vector<OccurrenceId>> children_by_parent;
    std::unordered_map<EntityId, std::vector<OccurrenceId>> occurrences_by_source;
    std::unordered_map<GeometryId, std::vector<OccurrenceId>> occurrences_by_geometry;
    std::unordered_map<MaterialId, std::vector<OccurrenceId>> occurrences_by_material;
};

struct PublishedSceneState {
    RevisionCounters revisions;
    std::unordered_map<EntityId, std::string> entity_names;
    std::uint64_t geometry_store_revision = 0;
    std::uint64_t material_store_revision = 0;
    std::uint64_t geometry_resources_revision = 0;
    std::uint64_t material_resources_revision = 0;
    std::shared_ptr<const PublishedResourceDelta> resource_delta;
    std::shared_ptr<const PublishedOccurrenceState> occurrences;
    std::shared_ptr<const std::vector<GeometryResource>> geometries;
    std::shared_ptr<const std::vector<MaterialResource>> materials;
    std::shared_ptr<const std::vector<ImageResource>> images;
    std::shared_ptr<const std::vector<TextureResource>> textures;
    std::shared_ptr<const std::vector<SamplerResource>> samplers;
    std::shared_ptr<const std::vector<CameraResource>> cameras;
    std::shared_ptr<const std::vector<LightResource>> lights;
};

class NKS_API Scene {
public:
    Scene();

    OccurrenceId reserve_occurrence_id() noexcept { return occurrences.reserve_id(); }
    GeometryId reserve_geometry_id() noexcept { return GeometryId{next_geometry_id++}; }
    MaterialId reserve_material_id() noexcept { return MaterialId{next_material_id++}; }
    ImageId reserve_image_id() noexcept { return ImageId{next_image_id++}; }
    TextureId reserve_texture_id() noexcept { return TextureId{next_texture_id++}; }
    SamplerId reserve_sampler_id() noexcept { return SamplerId{next_sampler_id++}; }
    CameraId reserve_camera_id() noexcept { return CameraId{next_camera_id++}; }
    LightId reserve_light_id() noexcept { return LightId{next_light_id++}; }
    GeometryId create_geometry();
    MaterialId create_material();
    ImageId create_image() {
        const auto id = reserve_image_id();
        images.create(id);
        return id;
    }
    TextureId create_texture() {
        const auto id = reserve_texture_id();
        textures.create(id);
        return id;
    }
    SamplerId create_sampler() {
        const auto id = reserve_sampler_id();
        samplers.create(id);
        return id;
    }
    CameraId create_camera() {
        const auto id = reserve_camera_id();
        cameras.create(id);
        return id;
    }
    LightId create_light() {
        const auto id = reserve_light_id();
        lights.create(id);
        return id;
    }
    void destroy_geometry(GeometryId id) noexcept;
    void destroy_material(MaterialId id) noexcept;
    void destroy_image(ImageId id) noexcept { images.destroy(id); }
    void destroy_texture(TextureId id) noexcept { textures.destroy(id); }
    void destroy_sampler(SamplerId id) noexcept { samplers.destroy(id); }
    void destroy_camera(CameraId id) noexcept { cameras.destroy(id); }
    void destroy_light(LightId id) noexcept { lights.destroy(id); }
    std::size_t occurrence_count() const noexcept { return occurrences.size(); }
    bool contains(OccurrenceId id) const noexcept { return occurrences.contains(id); }

    nkscene_result commit(const Transaction &transaction, ChangeSet &changes);
    SceneSnapshot snapshot() const;

    std::uint64_t revision() const noexcept { return revisions.scene; }
    const RevisionCounters &revision_counters() const noexcept { return revisions; }

    const OccurrenceStore &occurrence_store() const noexcept { return occurrences; }
    const HierarchyIndex &hierarchy_index() const noexcept { return hierarchy; }
    const ComponentStore<LocalTransform> &transforms() const noexcept { return local_transforms; }
    const ComponentStore<WorldTransform> &world_transforms() const noexcept {
        return world_transforms_;
    }
    const ComponentStore<Visibility> &visibilities() const noexcept { return visibilities_; }
    const ComponentStore<CameraRef> &camera_refs() const noexcept { return camera_refs_; }
    const ComponentStore<LightRef> &light_refs() const noexcept { return light_refs_; }
    const ComponentStore<std::string> &names() const noexcept { return names_; }
    const GeometryStore &geometry_store() const noexcept { return geometries; }
    const MaterialStore &material_store() const noexcept { return materials; }
    GeometryStore &geometry_store() noexcept { return geometries; }
    MaterialStore &material_store() noexcept { return materials; }
    ImageStore &image_store() noexcept { return images; }
    TextureStore &texture_store() noexcept { return textures; }
    SamplerStore &sampler_store() noexcept { return samplers; }
    CameraStore &camera_store() noexcept { return cameras; }
    LightStore &light_store() noexcept { return lights; }
    const ImageStore &image_store() const noexcept { return images; }
    const TextureStore &texture_store() const noexcept { return textures; }
    const SamplerStore &sampler_store() const noexcept { return samplers; }
    const CameraStore &camera_store() const noexcept { return cameras; }
    const LightStore &light_store() const noexcept { return lights; }

    /** Publishes externally edited resources into the next immutable snapshot. */
    void publish() const;

private:
    nkscene_result validate(const Transaction &transaction) const noexcept;
    bool exists_after(const std::unordered_map<OccurrenceId, bool> &live,
                      OccurrenceId id) const noexcept;
    void recompute_world_transforms(ChangeSet &changes);
    void publish_state(const ChangeSet *changes,
                       std::span<const std::uint32_t> destroyed_slots) const;
    void record_change(ChangeSet &changes, std::unordered_map<OccurrenceId, std::size_t> &indices,
                       OccurrenceId id, ChangeDomain domain);

    OccurrenceStore occurrences;
    ComponentStore<SourceEntity> source_entities;
    ComponentStore<Parent> parent_components;
    ComponentStore<LocalTransform> local_transforms;
    ComponentStore<WorldTransform> world_transforms_;
    ComponentStore<GeometryRef> geometry_refs;
    ComponentStore<MaterialRef> material_refs;
    ComponentStore<CameraRef> camera_refs_;
    ComponentStore<LightRef> light_refs_;
    ComponentStore<Visibility> visibilities_;
    ComponentStore<std::string> names_;
    std::unordered_map<EntityId, std::string> entity_names;
    ComponentStore<Bounds> bounds;
    HierarchyIndex hierarchy;
    GeometryStore geometries;
    MaterialStore materials;
    ImageStore images;
    TextureStore textures;
    SamplerStore samplers;
    CameraStore cameras;
    LightStore lights;
    std::uint64_t next_geometry_id = 1;
    std::uint64_t next_material_id = 1;
    std::uint64_t next_image_id = 1;
    std::uint64_t next_texture_id = 1;
    std::uint64_t next_sampler_id = 1;
    std::uint64_t next_camera_id = 1;
    std::uint64_t next_light_id = 1;
    RevisionCounters revisions;
    mutable std::shared_ptr<const PublishedSceneState> published_;
};

NKS_API std::shared_ptr<const SceneSnapshot> resolve_snapshot_handle(
    nkscene_snapshot snapshot) noexcept;
NKS_API std::shared_ptr<const ChangeSet> resolve_change_set_handle(
    nkscene_change_set changes) noexcept;

} // namespace nkscene
