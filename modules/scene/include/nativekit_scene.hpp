#pragma once

#include "nativekit_scene.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
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
constexpr EntityId invalid_entity{};
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

struct TransformUpdate {
    OccurrenceId occurrence;
    LocalTransform transform;
};

struct Bounds {
    std::array<float, 3> minimum{0.0f, 0.0f, 0.0f};
    std::array<float, 3> maximum{0.0f, 0.0f, 0.0f};
    bool valid = false;
};

struct GeometryVertex {
    std::array<float, 3> position{};
};

enum class VertexSemantic : std::uint32_t {
    Position = NKS_VERTEX_SEMANTIC_POSITION,
    Normal = NKS_VERTEX_SEMANTIC_NORMAL,
    Tangent = NKS_VERTEX_SEMANTIC_TANGENT,
    Texcoord0 = NKS_VERTEX_SEMANTIC_TEXCOORD0,
    Texcoord1 = NKS_VERTEX_SEMANTIC_TEXCOORD1,
    Color0 = NKS_VERTEX_SEMANTIC_COLOR0
};

enum class VertexFormat : std::uint32_t {
    Float32x2 = NKS_VERTEX_FORMAT_FLOAT32X2,
    Float32x3 = NKS_VERTEX_FORMAT_FLOAT32X3,
    Float32x4 = NKS_VERTEX_FORMAT_FLOAT32X4,
    Unorm8x4 = NKS_VERTEX_FORMAT_UNORM8X4,
    Snorm8x4 = NKS_VERTEX_FORMAT_SNORM8X4
};

enum class PrimitiveType : std::uint32_t {
    Triangles = NKS_PRIMITIVE_TRIANGLES,
    Lines = NKS_PRIMITIVE_LINES,
    Points = NKS_PRIMITIVE_POINTS
};

struct GeometryVertexStream {
    VertexSemantic semantic = VertexSemantic::Position;
    VertexFormat format = VertexFormat::Float32x3;
    std::uint32_t stride = 0;
    std::uint32_t count = 0;
    std::vector<std::byte> data;
};

struct GeometryPayload {
    /** Positions are object-local and use a tightly packed float3 layout. */
    std::vector<GeometryVertex> vertices;
    std::vector<GeometryVertexStream> streams;
    /** Optional uint32 triangle indices. Empty means sequential triangles. */
    std::vector<std::uint32_t> indices;
    PrimitiveType primitive_type = PrimitiveType::Triangles;

    std::size_t element_count() const noexcept {
        return indices.empty() ? vertices.size() : indices.size();
    }

    bool indexed() const noexcept { return !indices.empty(); }
};

struct SubelementRange {
    std::uint32_t first_primitive = 0;
    std::uint32_t primitive_count = 0;
    std::uint32_t subelement = 0;
};

struct SubelementTable {
    std::vector<SubelementRange> ranges;

    std::uint32_t id_for_primitive(std::size_t primitive) const noexcept {
        for (const auto &range : ranges) {
            const auto first = static_cast<std::size_t>(range.first_primitive);
            const auto count = static_cast<std::size_t>(range.primitive_count);
            if (primitive >= first &&
                primitive - first < count)
                return range.subelement;
        }
        return static_cast<std::uint32_t>(primitive + 1);
    }
};

struct GeometryResource {
    GeometryId id;
    std::uint64_t revision = 1;
    Bounds bounds;
    GeometryPayload payload;
    SubelementTable subelements;
};

enum class MaterialFlags : std::uint32_t {
    Opaque = 1u << 0,
    DoubleSided = 1u << 1
};

constexpr bool has_material_flag(std::uint32_t value, MaterialFlags flag) noexcept {
    return (value & static_cast<std::uint32_t>(flag)) != 0;
}

struct MaterialResource {
    MaterialId id;
    std::uint64_t revision = 1;
    std::array<float, 4> base_color{1.0f, 1.0f, 1.0f, 1.0f};
    float opacity = 1.0f;
    std::uint32_t flags = static_cast<std::uint32_t>(MaterialFlags::Opaque);
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
    Bounds = 1u << 7,
    Source = 1u << 8,
    Name = 1u << 9
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
    std::uint64_t source = 0;
    std::uint64_t name = 0;
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
    std::string name;
    OccurrenceId parent;
    LocalTransform local_transform;
    WorldTransform world_transform;
    GeometryId geometry;
    MaterialId material;
    bool visible = true;
    Bounds bounds;
};

class Scene;

class NKS_API SceneSnapshot {
public:
    SceneSnapshot();

    std::uint64_t revision() const noexcept;
    const RevisionCounters &revisions() const noexcept;
    std::span<const SnapshotOccurrence> occurrences() const noexcept;
    std::span<const OccurrenceId> occurrences_for_source(EntityId source) const noexcept;
    const SnapshotOccurrence *find(OccurrenceId id) const noexcept;
    std::string_view name(OccurrenceId id) const noexcept;
    std::string_view entity_name(EntityId id) const noexcept;
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
