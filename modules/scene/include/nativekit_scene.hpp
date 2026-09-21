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
struct ImageTag;
struct TextureTag;
struct SamplerTag;
struct CameraTag;
struct LightTag;

using OccurrenceId = Id<OccurrenceTag>;
using EntityId = Id<EntityTag>;
using GeometryId = Id<GeometryTag>;
using MaterialId = Id<MaterialTag>;
using ImageId = Id<ImageTag>;
using TextureId = Id<TextureTag>;
using SamplerId = Id<SamplerTag>;
using CameraId = Id<CameraTag>;
using LightId = Id<LightTag>;

constexpr OccurrenceId invalid_occurrence{};
constexpr EntityId invalid_entity{};
constexpr GeometryId invalid_geometry{};
constexpr MaterialId invalid_material{};
constexpr ImageId invalid_image{};
constexpr TextureId invalid_texture{};
constexpr SamplerId invalid_sampler{};
constexpr CameraId invalid_camera{};
constexpr LightId invalid_light{};

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

enum class ImageFormat : std::uint32_t {
    R8 = NKS_IMAGE_FORMAT_R8,
    RGBA8 = NKS_IMAGE_FORMAT_RGBA8,
    RGBA16F = NKS_IMAGE_FORMAT_RGBA16F,
    R32F = NKS_IMAGE_FORMAT_R32F
};

enum class SamplerFilter : std::uint32_t {
    Nearest = NKS_SAMPLER_FILTER_NEAREST,
    Linear = NKS_SAMPLER_FILTER_LINEAR
};

enum class SamplerWrap : std::uint32_t {
    Repeat = NKS_SAMPLER_WRAP_REPEAT,
    ClampToEdge = NKS_SAMPLER_WRAP_CLAMP_TO_EDGE,
    MirroredRepeat = NKS_SAMPLER_WRAP_MIRRORED_REPEAT
};

enum class AlphaMode : std::uint32_t {
    Opaque = NKS_MATERIAL_ALPHA_OPAQUE,
    Mask = NKS_MATERIAL_ALPHA_MASK,
    Blend = NKS_MATERIAL_ALPHA_BLEND
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
    std::shared_ptr<const GeometryPayload> payload = std::make_shared<GeometryPayload>();
    std::shared_ptr<const SubelementTable> subelements = std::make_shared<SubelementTable>();

    GeometryPayload &edit_payload() {
        auto next = std::make_shared<GeometryPayload>(*payload);
        payload = next;
        return *next;
    }

    SubelementTable &edit_subelements() {
        auto next = std::make_shared<SubelementTable>(*subelements);
        subelements = next;
        return *next;
    }
};

struct ImageResource {
    ImageId id;
    std::uint64_t revision = 1;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    ImageFormat format = ImageFormat::RGBA8;
    std::uint32_t mip_count = 1;
    std::vector<std::byte> data;
};

struct TextureResource {
    TextureId id;
    std::uint64_t revision = 1;
    ImageId image;
};

struct SamplerResource {
    SamplerId id;
    std::uint64_t revision = 1;
    SamplerFilter min_filter = SamplerFilter::Linear;
    SamplerFilter mag_filter = SamplerFilter::Linear;
    SamplerWrap wrap_u = SamplerWrap::Repeat;
    SamplerWrap wrap_v = SamplerWrap::Repeat;
    SamplerWrap wrap_w = SamplerWrap::Repeat;
    float max_anisotropy = 1.0f;
};

enum class MaterialFlags : std::uint32_t {
    Opaque = 1u << 0,
    DoubleSided = 1u << 1
};

constexpr bool has_material_flag(std::uint32_t value, MaterialFlags flag) noexcept {
    return (value & static_cast<std::uint32_t>(flag)) != 0;
}

struct MaterialState {
    std::array<float, 4> base_color{1.0f, 1.0f, 1.0f, 1.0f};
    float opacity = 1.0f;
    std::uint32_t flags = static_cast<std::uint32_t>(MaterialFlags::Opaque);
    float metallic = 0.0f;
    float roughness = 1.0f;
    std::array<float, 3> emissive{0.0f, 0.0f, 0.0f};
    float alpha_cutoff = 0.5f;
    AlphaMode alpha_mode = AlphaMode::Opaque;
    TextureId base_color_texture;
    TextureId metallic_roughness_texture;
    TextureId normal_texture;
    TextureId emissive_texture;
    TextureId occlusion_texture;
    SamplerId sampler;
};

enum class CameraProjection : std::uint32_t {
    Perspective = NKS_CAMERA_PERSPECTIVE,
    Orthographic = NKS_CAMERA_ORTHOGRAPHIC
};

struct CameraResource {
    CameraId id;
    std::uint64_t revision = 1;
    CameraProjection projection = CameraProjection::Perspective;
    float fov_y = 1.04719755f;
    float orthographic_height = 10.0f;
    float near_plane = 0.01f;
    float far_plane = 1000.0f;
    float aspect_ratio = 0.0f;
};

enum class LightType : std::uint32_t {
    Directional = NKS_LIGHT_DIRECTIONAL,
    Point = NKS_LIGHT_POINT,
    Spot = NKS_LIGHT_SPOT
};

struct LightResource {
    LightId id;
    std::uint64_t revision = 1;
    LightType type = LightType::Directional;
    std::array<float, 3> color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    float range = 10.0f;
    float inner_cone_angle = 0.2617994f;
    float outer_cone_angle = 0.7853982f;
};

struct MaterialResource {
    MaterialId id;
    std::uint64_t revision = 1;
    std::shared_ptr<const MaterialState> state = std::make_shared<MaterialState>();

    MaterialState &edit_state() {
        auto next = std::make_shared<MaterialState>(*state);
        state = next;
        return *next;
    }
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
    Name = 1u << 9,
    Camera = 1u << 10,
    Light = 1u << 11
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
    std::uint64_t camera = 0;
    std::uint64_t light = 0;
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
    CameraId camera;
    LightId light;
    bool visible = true;
    Bounds bounds;
};

class Scene;
struct PublishedSceneState;

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
    std::span<const ImageResource> images() const noexcept;
    std::span<const TextureResource> textures() const noexcept;
    std::span<const SamplerResource> samplers() const noexcept;
    std::span<const CameraResource> cameras() const noexcept;
    std::span<const LightResource> lights() const noexcept;
    const GeometryResource *find_geometry(GeometryId id) const noexcept;
    const MaterialResource *find_material(MaterialId id) const noexcept;
    const ImageResource *find_image(ImageId id) const noexcept;
    const TextureResource *find_texture(TextureId id) const noexcept;
    const SamplerResource *find_sampler(SamplerId id) const noexcept;
    const CameraResource *find_camera(CameraId id) const noexcept;
    const LightResource *find_light(LightId id) const noexcept;

private:
    explicit SceneSnapshot(std::shared_ptr<const PublishedSceneState> state);

    std::shared_ptr<const PublishedSceneState> state_;
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
