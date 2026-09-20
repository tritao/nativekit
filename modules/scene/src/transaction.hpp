#pragma once

#include "ids.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <variant>
#include <vector>

namespace nkscene {

class Scene;

struct SourceEntity {
    EntityId id;
};

struct Parent {
    OccurrenceId id;
};

struct GeometryRef {
    GeometryId id;
};

struct MaterialRef {
    MaterialId id;
};

struct Visibility {
    bool visible = true;
};

struct CreateOccurrence {
    OccurrenceId occurrence;
};

struct DestroyOccurrence {
    OccurrenceId occurrence;
};

struct SetParent {
    OccurrenceId occurrence;
    OccurrenceId parent;
};

struct SetTransform {
    OccurrenceId occurrence;
    LocalTransform transform;
};

struct SetGeometry {
    OccurrenceId occurrence;
    GeometryId geometry;
};

struct SetMaterial {
    OccurrenceId occurrence;
    MaterialId material;
};

struct SetVisibility {
    OccurrenceId occurrence;
    bool visible;
};

using Mutation = std::variant<CreateOccurrence, DestroyOccurrence, SetParent,
                              SetTransform, SetGeometry, SetMaterial, SetVisibility>;

class Transaction {
public:
    explicit Transaction(std::shared_ptr<Scene> scene) : scene_(std::move(scene)) {}

    const std::shared_ptr<Scene> &scene() const noexcept { return scene_; }
    bool active() const noexcept { return active_; }
    void close() noexcept { active_ = false; }

    void add_create(OccurrenceId id) { mutations_.emplace_back(CreateOccurrence{id}); }
    void add_destroy(OccurrenceId id) { mutations_.emplace_back(DestroyOccurrence{id}); }
    void add_parent(OccurrenceId id, OccurrenceId parent) {
        mutations_.emplace_back(SetParent{id, parent});
    }
    void add_transform(OccurrenceId id, const LocalTransform &transform) {
        mutations_.emplace_back(SetTransform{id, transform});
    }
    void add_geometry(OccurrenceId id, GeometryId geometry) {
        mutations_.emplace_back(SetGeometry{id, geometry});
    }
    void add_material(OccurrenceId id, MaterialId material) {
        mutations_.emplace_back(SetMaterial{id, material});
    }
    void add_visibility(OccurrenceId id, bool visible) {
        mutations_.emplace_back(SetVisibility{id, visible});
    }

    const std::vector<Mutation> &mutations() const noexcept { return mutations_; }

private:
    std::shared_ptr<Scene> scene_;
    std::vector<Mutation> mutations_;
    bool active_ = true;
};

} // namespace nkscene
