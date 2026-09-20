#pragma once

#include "nativekit_scene.h"

#include "changeset.hpp"
#include "component_store.hpp"
#include "hierarchy.hpp"
#include "occurrence_store.hpp"
#include "resources.hpp"
#include "transaction.hpp"

#include <cstdint>
#include <memory>
#include <unordered_map>

namespace nkscene {

class Scene {
public:
    OccurrenceId reserve_occurrence_id() noexcept { return occurrences.reserve_id(); }
    GeometryId reserve_geometry_id() noexcept { return GeometryId{next_geometry_id++}; }
    MaterialId reserve_material_id() noexcept { return MaterialId{next_material_id++}; }
    GeometryId create_geometry() {
        const auto id = reserve_geometry_id();
        geometries.create(id);
        return id;
    }
    MaterialId create_material() {
        const auto id = reserve_material_id();
        materials.create(id);
        return id;
    }
    void destroy_geometry(GeometryId id) noexcept { geometries.destroy(id); }
    void destroy_material(MaterialId id) noexcept { materials.destroy(id); }
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
    const GeometryStore &geometry_store() const noexcept { return geometries; }
    const MaterialStore &material_store() const noexcept { return materials; }
    GeometryStore &geometry_store() noexcept { return geometries; }
    MaterialStore &material_store() noexcept { return materials; }

private:
    nkscene_result validate(const Transaction &transaction) const noexcept;
    bool exists_after(const std::unordered_map<OccurrenceId, bool> &live,
                      OccurrenceId id) const noexcept;
    void recompute_world_transforms(ChangeSet &changes);
    void record_change(ChangeSet &changes, std::unordered_map<OccurrenceId, std::size_t> &indices,
                       OccurrenceId id, ChangeDomain domain);

    OccurrenceStore occurrences;
    ComponentStore<SourceEntity> source_entities;
    ComponentStore<Parent> parent_components;
    ComponentStore<LocalTransform> local_transforms;
    ComponentStore<WorldTransform> world_transforms_;
    ComponentStore<GeometryRef> geometry_refs;
    ComponentStore<MaterialRef> material_refs;
    ComponentStore<Visibility> visibilities_;
    ComponentStore<Bounds> bounds;
    HierarchyIndex hierarchy;
    GeometryStore geometries;
    MaterialStore materials;
    std::uint64_t next_geometry_id = 1;
    std::uint64_t next_material_id = 1;
    RevisionCounters revisions;
};

} // namespace nkscene
