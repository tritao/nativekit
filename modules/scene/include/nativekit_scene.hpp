#pragma once

#include "nativekit_scene.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <vector>

namespace nkscene {

template<class Tag>
struct Id {
    std::uint64_t value = 0;

    constexpr bool valid() const noexcept { return value != 0; }
    friend constexpr bool operator==(Id, Id) noexcept = default;
    friend constexpr bool operator!=(Id lhs, Id rhs) noexcept { return !(lhs == rhs); }
};

struct OccurrenceTag;
struct EntityTag;
struct GeometryTag;
struct MaterialTag;

using OccurrenceId = Id<OccurrenceTag>;
using EntityId = Id<EntityTag>;
using GeometryId = Id<GeometryTag>;
using MaterialId = Id<MaterialTag>;

constexpr OccurrenceId invalid_occurrence{};
constexpr GeometryId invalid_geometry{};
constexpr MaterialId invalid_material{};

struct LocalTransform {
    std::array<float, 16> matrix{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f};
};

struct WorldTransform {
    LocalTransform transform;
    std::uint64_t revision = 0;
};

struct Bounds {
    std::array<float, 3> minimum{0.0f, 0.0f, 0.0f};
    std::array<float, 3> maximum{0.0f, 0.0f, 0.0f};
    bool valid = false;
};

struct GeometryPayload {
    std::vector<std::byte> bytes;
};

struct SubelementTable {
    std::vector<std::uint32_t> offsets;
};

struct GeometryResource {
    GeometryId id;
    std::uint64_t revision = 1;
    Bounds bounds;
    GeometryPayload payload;
    SubelementTable subelements;
};

struct MaterialResource {
    MaterialId id;
    std::uint64_t revision = 1;
};

enum class ChangeDomain : std::uint32_t {
    None = 0,
    Created = 1u << 0,
    Destroyed = 1u << 1,
    Transform = 1u << 2,
    Hierarchy = 1u << 3,
    Geometry = 1u << 4,
    Material = 1u << 5,
    Visibility = 1u << 6,
    Bounds = 1u << 7
};

constexpr ChangeDomain operator|(ChangeDomain lhs, ChangeDomain rhs) noexcept {
    return static_cast<ChangeDomain>(static_cast<std::uint32_t>(lhs) |
                                      static_cast<std::uint32_t>(rhs));
}

constexpr ChangeDomain &operator|=(ChangeDomain &lhs, ChangeDomain rhs) noexcept {
    lhs = lhs | rhs;
    return lhs;
}

constexpr bool has_domain(ChangeDomain value, ChangeDomain domain) noexcept {
    return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(domain)) != 0;
}

struct SceneChange {
    OccurrenceId occurrence;
    ChangeDomain domains = ChangeDomain::None;
};

struct RevisionCounters {
    std::uint64_t scene = 0;
    std::uint64_t hierarchy = 0;
    std::uint64_t transform = 0;
    std::uint64_t geometry = 0;
    std::uint64_t material = 0;
    std::uint64_t visibility = 0;
    std::uint64_t bounds = 0;
};

struct ChangeStats {
    std::size_t changed_occurrences = 0;
    std::size_t changed_resources = 0;
    std::size_t dirty_world_transforms = 0;
    std::size_t dirty_bounds = 0;
    std::size_t full_rebuilds = 0;
};

struct ChangeSet {
    std::uint64_t scene_revision = 0;
    RevisionCounters revisions;
    ChangeStats stats;
    std::vector<SceneChange> changes;
};

struct SnapshotOccurrence {
    OccurrenceId occurrence;
    EntityId source;
    OccurrenceId parent;
    LocalTransform local_transform;
    WorldTransform world_transform;
    GeometryId geometry;
    MaterialId material;
    bool visible = true;
    Bounds bounds;
};

class Scene;

class SceneSnapshot {
public:
    SceneSnapshot();

    std::uint64_t revision() const noexcept;
    const RevisionCounters &revisions() const noexcept;
    std::span<const SnapshotOccurrence> occurrences() const noexcept;
    const SnapshotOccurrence *find(OccurrenceId id) const noexcept;
    std::span<const GeometryResource> geometries() const noexcept;
    std::span<const MaterialResource> materials() const noexcept;
    const GeometryResource *find_geometry(GeometryId id) const noexcept;
    const MaterialResource *find_material(MaterialId id) const noexcept;

private:
    struct State;
    explicit SceneSnapshot(std::shared_ptr<const State> state);

    std::shared_ptr<const State> state_;
    friend class Scene;
};

} // namespace nkscene

namespace std {

template<class Tag>
struct hash<nkscene::Id<Tag>> {
    std::size_t operator()(nkscene::Id<Tag> id) const noexcept {
        return static_cast<std::size_t>(id.value ^ (id.value >> 32));
    }
};

} // namespace std
