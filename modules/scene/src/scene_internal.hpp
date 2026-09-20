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
    std::size_t occurrence_count() const noexcept { return occurrences.size(); }
    bool contains(OccurrenceId id) const noexcept { return occurrences.contains(id); }

    nkscene_result commit(const Transaction &transaction, ChangeSet &changes);

    std::uint64_t revision() const noexcept { return scene_revision; }

    const OccurrenceStore &occurrence_store() const noexcept { return occurrences; }
    const ComponentStore<LocalTransform> &transforms() const noexcept { return local_transforms; }
    const ComponentStore<Visibility> &visibilities() const noexcept { return visibilities_; }

private:
    bool validate(const Transaction &transaction) const noexcept;
    bool exists_after(const std::unordered_map<OccurrenceId, bool> &live,
                      OccurrenceId id) const noexcept;
    void record_change(ChangeSet &changes, std::unordered_map<OccurrenceId, std::size_t> &indices,
                       OccurrenceId id, ChangeDomain domain);

    OccurrenceStore occurrences;
    ComponentStore<SourceEntity> source_entities;
    ComponentStore<Parent> parent_components;
    ComponentStore<LocalTransform> local_transforms;
    ComponentStore<GeometryRef> geometry_refs;
    ComponentStore<MaterialRef> material_refs;
    ComponentStore<Visibility> visibilities_;
    ComponentStore<Bounds> bounds;
    HierarchyIndex hierarchy;
    GeometryStore geometries;
    MaterialStore materials;
    std::uint64_t scene_revision = 0;
};

} // namespace nkscene
