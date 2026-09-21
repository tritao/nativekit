#ifndef NATIVEKIT_SCENE_RENDER_H
#define NATIVEKIT_SCENE_RENDER_H

/* ------------------------------------------------------------------------- */
/* Dependencies                                                              */
/* ------------------------------------------------------------------------- */

#include "nativekit_scene.h"
#include "nativekit_gpu.h"

/* ------------------------------------------------------------------------- */
/* Export visibility                                                         */
/* ------------------------------------------------------------------------- */

#if defined(_WIN32)
#if defined(NK_STATIC)
#define NKSRENDER_API
#elif defined(NKSRENDER_BUILDING_LIBRARY)
#define NKSRENDER_API __declspec(dllexport)
#else
#define NKSRENDER_API __declspec(dllimport)
#endif
#else
#define NKSRENDER_API __attribute__((visibility("default")))
#endif

/* ------------------------------------------------------------------------- */
/* C linkage                                                                 */
/* ------------------------------------------------------------------------- */

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t nkscene_render_plan NK_HANDLE NK_HANDLE_DESTROY(nkscene_render_plan_destroy);
typedef uint32_t
    nkscene_render_executor NK_HANDLE NK_HANDLE_DESTROY(nkscene_render_executor_destroy);
typedef uint32_t
    nkscene_render_spatial_index NK_HANDLE NK_HANDLE_DESTROY(nkscene_render_spatial_index_destroy);
typedef uint32_t
    nkscene_render_pick_request NK_HANDLE NK_HANDLE_DESTROY(nkscene_render_pick_request_destroy);

typedef struct nkscene_render_visibility_override {
    nkscene_occurrence_id occurrence;
    uint32_t visible NK_BOOL32;
} nkscene_render_visibility_override;

typedef struct nkscene_render_material_override {
    nkscene_occurrence_id occurrence;
    nkscene_material_id material;
} nkscene_render_material_override;

typedef struct nkscene_render_source_visibility_override {
    nkscene_entity_id source;
    uint32_t visible NK_BOOL32;
} nkscene_render_source_visibility_override;

typedef struct nkscene_render_source_material_override {
    nkscene_entity_id source;
    nkscene_material_id material;
} nkscene_render_source_material_override;

typedef struct nkscene_render_clip_plane {
    float normal[3];
    float distance;
    uint32_t enabled NK_BOOL32;
} nkscene_render_clip_plane;

typedef struct nkscene_render_camera {
    uint32_t enabled NK_BOOL32;
    nkscene_transform view_projection;
} nkscene_render_camera;

typedef struct nkscene_render_view {
    uint32_t struct_size NK_STRUCT_SIZE;
    nkscene_occurrence_id root;
    uint32_t include_invisible NK_BOOL32;
    const nkscene_render_visibility_override *
        visibility_overrides NK_BORROWED_ARRAY(visibility_override_count);
    uint32_t visibility_override_count;
    const nkscene_render_material_override *
        material_overrides NK_BORROWED_ARRAY(material_override_count);
    uint32_t material_override_count;
    nkscene_render_camera camera;
    const nkscene_render_clip_plane *clip_planes NK_BORROWED_ARRAY(clip_plane_count);
    uint32_t clip_plane_count;
    const nkscene_render_material_override *
        selection_overrides NK_BORROWED_ARRAY(selection_override_count);
    uint32_t selection_override_count;
    const nkscene_render_material_override *hover_overrides NK_BORROWED_ARRAY(hover_override_count);
    uint32_t hover_override_count;
    const nkscene_entity_id *isolated_sources NK_BORROWED_ARRAY(isolated_source_count);
    uint32_t isolated_source_count;
    const nkscene_render_source_visibility_override *
        source_visibility_overrides NK_BORROWED_ARRAY(source_visibility_override_count);
    uint32_t source_visibility_override_count;
    const nkscene_render_source_material_override *
        source_material_overrides NK_BORROWED_ARRAY(source_material_override_count);
    uint32_t source_material_override_count;
    const nkscene_occurrence_id *isolated_occurrences NK_BORROWED_ARRAY(isolated_occurrence_count);
    uint32_t isolated_occurrence_count;
    /** Optional scene camera occurrence used when camera.enabled is zero. */
    nkscene_occurrence_id camera_occurrence;
} nkscene_render_view;

typedef struct nkscene_render_update {
    uint32_t struct_size NK_STRUCT_SIZE;
    uint32_t plan_rebuilt NK_BOOL32;
    uint32_t geometry_rebuilt NK_BOOL32;
    uint64_t patched_instances;
    uint64_t patched_visibility;
    uint64_t patched_materials;
    uint64_t rebuilt_batches;
    uint64_t updated_geometry_resources;
    uint64_t updated_material_resources;
    uint64_t invalidated_items;
    uint64_t patched_culling;
    uint64_t visible_items;
    uint64_t culled_items;
} nkscene_render_update;

typedef struct nkscene_render_pick_result {
    nkscene_occurrence_id occurrence;
    nkscene_entity_id source;
    uint32_t subelement;
    float world_position[3];
    float depth;
} nkscene_render_pick_result;

typedef struct nkscene_render_ray {
    float origin[3];
    float direction[3];
} nkscene_render_ray;

typedef struct nkscene_render_spatial_occurrence {
    nkscene_occurrence_id occurrence;
} nkscene_render_spatial_occurrence;

enum {
    NKS_RENDER_PICK_PENDING = 1,
    NKS_RENDER_PICK_READY = 2,
    NKS_RENDER_PICK_STALE = 3,
    NKS_RENDER_PICK_FAILED = 4
};

typedef struct nkscene_render_execution_stats {
    uint32_t struct_size NK_STRUCT_SIZE;
    nkgpu_result result;
    uint64_t geometry_resources_created;
    uint64_t geometry_resources_updated;
    uint64_t material_resources_created;
    uint64_t material_resources_updated;
    uint64_t instance_buffers_created;
    uint64_t instance_records_updated;
    uint64_t commands;
    uint64_t draw_calls;
} nkscene_render_execution_stats;

NKSRENDER_API nkscene_result NKS_CALL
nkscene_render_plan_compile(nkscene_snapshot snapshot, const nkscene_render_view *view,
                            nkscene_render_plan *out_plan NK_OUT NK_OWNED);
NKSRENDER_API void NKS_CALL nkscene_render_plan_destroy(nkscene_render_plan plan);
NKSRENDER_API nkscene_result NKS_CALL
nkscene_render_plan_get_item_count(nkscene_render_plan plan, uint64_t *out_count NK_OUT);
NKSRENDER_API nkscene_result NKS_CALL nkscene_render_plan_update(
    nkscene_render_plan plan, nkscene_snapshot snapshot, nkscene_change_set changes,
    const nkscene_render_view *view, nkscene_render_update *out_update NK_INOUT);
/** Refreshes a plan when no scene ChangeSet is available. */
NKSRENDER_API nkscene_result NKS_CALL nkscene_render_plan_refresh(
    nkscene_render_plan plan, nkscene_snapshot snapshot, const nkscene_render_view *view,
    nkscene_render_update *out_update NK_INOUT);
NKSRENDER_API nkscene_result NKS_CALL nkscene_render_plan_pick(
    nkscene_render_plan plan, nkscene_snapshot snapshot, uint32_t primitive,
    const float world_position[3], float depth, nkscene_render_pick_result *out_result NK_OUT);

/** Builds a read-only spatial index for one immutable scene snapshot. */
NKSRENDER_API nkscene_result NKS_CALL nkscene_render_spatial_index_create(
    nkscene_snapshot snapshot, nkscene_render_spatial_index *out_index NK_OUT NK_OWNED);
NKSRENDER_API void NKS_CALL
nkscene_render_spatial_index_destroy(nkscene_render_spatial_index index);
NKSRENDER_API nkscene_result NKS_CALL nkscene_render_spatial_index_get_revision(
    nkscene_render_spatial_index index, uint64_t *out_revision NK_OUT);
NKSRENDER_API nkscene_result NKS_CALL nkscene_render_spatial_index_query_bounds(
    nkscene_render_spatial_index index, const nkscene_bounds *bounds, uint64_t *out_count NK_OUT);
NKSRENDER_API nkscene_result NKS_CALL nkscene_render_spatial_index_query_ray(
    nkscene_render_spatial_index index, const nkscene_render_ray *ray, uint64_t *out_count NK_OUT);
NKSRENDER_API nkscene_result NKS_CALL nkscene_render_spatial_index_get_occurrence(
    nkscene_render_spatial_index index, uint64_t result_index,
    nkscene_render_spatial_occurrence *out_result NK_OUT);
NKSRENDER_API nkscene_result NKS_CALL nkscene_render_spatial_index_pick_ray(
    nkscene_render_spatial_index index, const nkscene_render_ray *ray,
    nkscene_render_pick_result *out_result NK_OUT);
/**
 * Creates an executor bound to a GPU renderer. Pass a zero renderer for the
 * headless resource and command path. The renderer must outlive the executor.
 */
NKSRENDER_API nkscene_result NKS_CALL nkscene_render_executor_create(
    nkgpu_renderer renderer, nkscene_render_executor *out_executor NK_OUT NK_OWNED);
NKSRENDER_API void NKS_CALL nkscene_render_executor_destroy(nkscene_render_executor executor);
/** Executes a plan and reports resource, instance, command, and draw counters. */
NKSRENDER_API nkscene_result NKS_CALL nkscene_render_executor_execute(
    nkscene_render_executor executor, nkscene_render_plan plan, nkscene_snapshot snapshot,
    nkscene_render_execution_stats *out_stats NK_OUT);
NKSRENDER_API nkscene_result NKS_CALL nkscene_render_executor_get_last_result(
    nkscene_render_executor executor, nkgpu_result *out_result NK_OUT);
/** Runs the GPU ID pass and resolves one pixel to scene ownership. */
NKSRENDER_API nkscene_result NKS_CALL nkscene_render_executor_pick_pixel(
    nkscene_render_executor executor, nkscene_render_plan plan, nkscene_snapshot snapshot,
    uint32_t width, uint32_t height, uint32_t x, uint32_t y,
    nkscene_render_pick_result *out_result NK_OUT);
/** Starts an asynchronous GPU ID pass and pixel readback. */
NKSRENDER_API nkscene_result NKS_CALL nkscene_render_executor_pick_pixel_begin(
    nkscene_render_executor executor, nkscene_render_plan plan, nkscene_snapshot snapshot,
    uint32_t width, uint32_t height, uint32_t x, uint32_t y,
    nkscene_render_pick_request *out_request NK_OUT NK_OWNED);
NKSRENDER_API void NKS_CALL
nkscene_render_pick_request_destroy(nkscene_render_pick_request request);
/** Polls an asynchronous pick without blocking. A ready result is valid only for the
 * supplied current plan and snapshot; otherwise the state is stale. */
NKSRENDER_API nkscene_result NKS_CALL nkscene_render_executor_pick_pixel_poll(
    nkscene_render_executor executor, nkscene_render_pick_request request,
    nkscene_render_plan current_plan, nkscene_snapshot current_snapshot, uint32_t *out_state NK_OUT,
    nkgpu_result *out_error NK_OUT, nkscene_render_pick_result *out_result NK_OUT);

#ifdef __cplusplus
}

/* ------------------------------------------------------------------------- */
/* C++ linkage                                                               */
/* ------------------------------------------------------------------------- */

#include "nativekit_scene.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace nkscene {

struct SceneView;
class RenderPlan;
namespace render_internal {
void rebuild_batches(RenderPlan &plan);
void build_items(RenderPlan &plan, const SceneSnapshot &snapshot, const SceneView &view);
std::size_t move_item_batch(RenderPlan &plan, std::size_t item_index, GeometryId geometry,
                            MaterialId material);
void capture_view_policy(RenderPlan &plan, const SceneView &view);
} // namespace render_internal

struct VisibilityOverride {
    OccurrenceId occurrence;
    bool visible = true;
};

struct MaterialOverride {
    OccurrenceId occurrence;
    MaterialId material;
};

struct SourceVisibilityOverride {
    EntityId source;
    bool visible = true;
};

struct SourceMaterialOverride {
    EntityId source;
    MaterialId material;
};

/** Declarative, scene-independent presentation filter for one SceneView. */
struct SceneViewFilter {
    /** Source entities retained by isolation, including their ancestors. */
    std::vector<EntityId> isolated_sources;
    /** Source-level visibility rules below explicit occurrence overrides. */
    std::vector<SourceVisibilityOverride> source_visibility_overrides;
    /** Source-level base materials below occurrence and interaction layers. */
    std::vector<SourceMaterialOverride> source_material_overrides;
    /** Explicit occurrences retained by isolation, including their subtrees. */
    std::vector<OccurrenceId> isolated_occurrences;

    void set_isolated_source(EntityId source, bool isolated) {
        const auto found = std::find(isolated_sources.begin(), isolated_sources.end(), source);
        if (isolated && found == isolated_sources.end())
            isolated_sources.push_back(source);
        else if (!isolated && found != isolated_sources.end())
            isolated_sources.erase(found);
    }

    void set_source_visibility_override(EntityId source, bool visible) {
        const auto found =
            std::find_if(source_visibility_overrides.begin(), source_visibility_overrides.end(),
                         [source](const auto &value) { return value.source == source; });
        if (found != source_visibility_overrides.end())
            found->visible = visible;
        else
            source_visibility_overrides.push_back({source, visible});
    }

    void set_source_material_override(EntityId source, MaterialId material) {
        const auto found =
            std::find_if(source_material_overrides.begin(), source_material_overrides.end(),
                         [source](const auto &value) { return value.source == source; });
        if (found != source_material_overrides.end())
            found->material = material;
        else
            source_material_overrides.push_back({source, material});
    }

    void set_isolated_occurrence(OccurrenceId occurrence, bool isolated) {
        const auto found =
            std::find(isolated_occurrences.begin(), isolated_occurrences.end(), occurrence);
        if (isolated && found == isolated_occurrences.end())
            isolated_occurrences.push_back(occurrence);
        else if (!isolated && found != isolated_occurrences.end())
            isolated_occurrences.erase(found);
    }

    void clear_isolation() noexcept {
        isolated_sources.clear();
        isolated_occurrences.clear();
    }

    void clear() noexcept {
        isolated_sources.clear();
        source_visibility_overrides.clear();
        source_material_overrides.clear();
        isolated_occurrences.clear();
    }
};

struct ClipPlane {
    std::array<float, 3> normal{0.0f, 0.0f, 1.0f};
    float distance = 0.0f;
    bool enabled = true;
};

struct SceneCamera {
    bool enabled = false;
    std::array<float, 16> view_projection{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                                          0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
};

struct SceneView {
    /** Invalid means that the view contains every occurrence. */
    OccurrenceId root;
    /** Include scene-hidden occurrences as visible for inspection views. */
    bool include_invisible = false;
    /** Later entries replace earlier entries for the same occurrence. */
    std::vector<VisibilityOverride> visibility_overrides;
    /** Base presentation material overrides. */
    std::vector<MaterialOverride> material_overrides;
    /** Selection material layer, below hover material overrides. */
    std::vector<MaterialOverride> selection_material_overrides;
    /** Hover material layer, above selection and base material overrides. */
    std::vector<MaterialOverride> hover_material_overrides;
    /** Declarative source and isolation presentation rules. */
    SceneViewFilter filter;
    /** Optional world-to-clip transform used for bounds culling and rendering. */
    SceneCamera camera;
    /** Optional scene camera occurrence used when camera.enabled is false. */
    OccurrenceId camera_occurrence;
    /** Conservative occurrence-level sectioning planes. */
    std::vector<ClipPlane> clip_planes;

    void set_visibility_override(OccurrenceId occurrence, bool visible) {
        const auto found = std::find_if(
            visibility_overrides.begin(), visibility_overrides.end(),
            [occurrence](const auto &value) { return value.occurrence == occurrence; });
        if (found != visibility_overrides.end())
            found->visible = visible;
        else
            visibility_overrides.push_back({occurrence, visible});
    }

    void set_material_override(OccurrenceId occurrence, MaterialId material) {
        set_material_override_in(material_overrides, occurrence, material);
    }

    void set_selection_material_override(OccurrenceId occurrence, MaterialId material) {
        set_material_override_in(selection_material_overrides, occurrence, material);
    }

    void set_hover_material_override(OccurrenceId occurrence, MaterialId material) {
        set_material_override_in(hover_material_overrides, occurrence, material);
    }

    void set_isolated_source(EntityId source, bool isolated) {
        filter.set_isolated_source(source, isolated);
    }

    void set_source_visibility_override(EntityId source, bool visible) {
        filter.set_source_visibility_override(source, visible);
    }

    void set_source_material_override(EntityId source, MaterialId material) {
        filter.set_source_material_override(source, material);
    }

    void set_isolated_occurrence(OccurrenceId occurrence, bool isolated) {
        filter.set_isolated_occurrence(occurrence, isolated);
    }

    void clear_selection_material_overrides() noexcept { selection_material_overrides.clear(); }

    void clear_hover_material_overrides() noexcept { hover_material_overrides.clear(); }

  private:
    static void set_material_override_in(std::vector<MaterialOverride> &overrides,
                                         OccurrenceId occurrence, MaterialId material) {
        const auto found =
            std::find_if(overrides.begin(), overrides.end(), [occurrence](const auto &value) {
                return value.occurrence == occurrence;
            });
        if (found != overrides.end())
            found->material = material;
        else
            overrides.push_back({occurrence, material});
    }
};

enum class RenderFlags : std::uint32_t {
    None = 0,
    Hidden = 1u << 0,
    Opaque = 1u << 1,
    Culled = 1u << 2
};

constexpr RenderFlags operator|(RenderFlags lhs, RenderFlags rhs) noexcept {
    return static_cast<RenderFlags>(static_cast<std::uint32_t>(lhs) |
                                    static_cast<std::uint32_t>(rhs));
}

constexpr RenderFlags &operator|=(RenderFlags &lhs, RenderFlags rhs) noexcept {
    lhs = lhs | rhs;
    return lhs;
}

constexpr bool has_render_flag(RenderFlags value, RenderFlags flag) noexcept {
    return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(flag)) != 0;
}

struct RenderItem {
    OccurrenceId occurrence;
    GeometryId geometry;
    MaterialId material;
    std::uint32_t pickId = 0;
    std::uint32_t transformIndex = 0;
    RenderFlags flags = RenderFlags::Opaque;
};

struct InstanceBatch {
    GeometryId geometry;
    MaterialId material;
    std::vector<OccurrenceId> instances;
};

struct RenderUpdate {
    bool plan_rebuilt = false;
    bool geometry_rebuilt = false;
    std::size_t patched_instances = 0;
    std::size_t patched_visibility = 0;
    std::size_t patched_materials = 0;
    std::size_t rebuilt_batches = 0;
    std::size_t updated_geometry_resources = 0;
    std::size_t updated_material_resources = 0;
    std::size_t invalidated_items = 0;
    std::size_t patched_culling = 0;
    std::size_t culling_candidates = 0;
    std::size_t visible_items = 0;
    std::size_t culled_items = 0;
};

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct SubelementId {
    std::uint32_t value = 0;
    constexpr bool valid() const noexcept { return value != 0; }
};

struct PickResult {
    OccurrenceId occurrence;
    EntityId source;
    SubelementId subelement;
    Vec3 worldPosition;
    float depth = 0.0f;
};

class NKSRENDER_API GpuPickRequest {
  public:
    GpuPickRequest() = default;
    ~GpuPickRequest();
    GpuPickRequest(GpuPickRequest &&) noexcept;
    GpuPickRequest &operator=(GpuPickRequest &&) noexcept;
    GpuPickRequest(const GpuPickRequest &) = delete;
    GpuPickRequest &operator=(const GpuPickRequest &) = delete;

  private:
    friend class NativeKitGpuExecutor;
    struct State;
    std::unique_ptr<State> state_;
};

struct Ray {
    Vec3 origin;
    Vec3 direction;
};

class NKSRENDER_API SceneSpatialIndex {
  public:
    explicit SceneSpatialIndex(const SceneSnapshot &snapshot);
    ~SceneSpatialIndex();
    SceneSpatialIndex(SceneSpatialIndex &&) noexcept;
    SceneSpatialIndex &operator=(SceneSpatialIndex &&) noexcept;
    SceneSpatialIndex(const SceneSpatialIndex &) = delete;
    SceneSpatialIndex &operator=(const SceneSpatialIndex &) = delete;

    std::uint64_t source_revision() const noexcept;
    std::span<const OccurrenceId> query_bounds(const Bounds &) const;
    /** Returns snapshot occurrences whose bounds intersect all supplied planes. */
    std::span<const OccurrenceId>
    query_frustum(std::span<const std::array<float, 4>> planes) const;
    std::span<const OccurrenceId> query_ray(const Ray &) const;
    std::size_t query_result_count() const noexcept;
    OccurrenceId query_result(std::size_t index) const noexcept;
    PickResult pick_ray(const Ray &) const;

  private:
    struct State;
    std::unique_ptr<State> state_;
};

struct GpuCommand {
    OccurrenceId occurrence;
    GeometryId geometry;
    MaterialId material;
    std::uint32_t transformIndex = 0;
};

struct GpuExecutionStats {
    nkgpu_result result = NKGPU_OK;
    std::size_t geometry_resources_created = 0;
    std::size_t geometry_resources_updated = 0;
    std::size_t material_resources_created = 0;
    std::size_t material_resources_updated = 0;
    std::size_t instance_buffers_created = 0;
    std::size_t instance_records_updated = 0;
    std::size_t commands = 0;
    std::size_t draw_calls = 0;
};

class RenderPlan {
  public:
    static constexpr std::size_t max_clip_planes = 32;

    std::uint64_t source_revision() const noexcept { return source_revision_; }
    std::uint64_t view_signature() const noexcept { return view_signature_; }
    std::span<const RenderItem> items() const noexcept { return items_; }
    std::span<const WorldTransform> transforms() const noexcept { return transforms_; }
    std::span<const InstanceBatch> batches() const noexcept { return batches_; }
    std::size_t item_index(OccurrenceId occurrence) const noexcept {
        const auto found = item_by_occurrence_.find(occurrence);
        return found == item_by_occurrence_.end() ? invalid_item_index : found->second;
    }
    std::span<const std::size_t> items_for_source(EntityId source) const noexcept {
        const auto found = items_by_source_.find(source);
        return found == items_by_source_.end()
                   ? std::span<const std::size_t>{}
                   : std::span<const std::size_t>{found->second};
    }
    std::size_t compile_count() const noexcept { return compile_count_; }
    std::size_t visible_items() const noexcept { return visible_items_; }
    std::size_t culled_items() const noexcept { return culled_items_; }
    const std::array<float, 16> &view_projection() const noexcept { return view_projection_; }
    std::span<const std::array<float, 4>> clip_planes() const noexcept {
        return {clip_planes_.data(), clip_plane_count_};
    }

  private:
    struct BatchKey {
        GeometryId geometry;
        MaterialId material;

        friend bool operator==(const BatchKey &, const BatchKey &) = default;
    };

    struct BatchKeyHash {
        std::size_t operator()(const BatchKey &key) const noexcept {
            const auto geometry = std::hash<std::uint64_t>{}(key.geometry.value);
            const auto material = std::hash<std::uint64_t>{}(key.material.value);
            return geometry ^ (material + 0x9e3779b9u + (geometry << 6) + (geometry >> 2));
        }
    };

    static constexpr std::size_t invalid_item_index = static_cast<std::size_t>(-1);
    std::uint64_t source_revision_ = 0;
    std::uint64_t view_signature_ = 0;
    std::uint64_t presentation_signature_ = 0;
    std::vector<RenderItem> items_;
    std::vector<WorldTransform> transforms_;
    std::vector<EntityId> item_sources_;
    std::vector<InstanceBatch> batches_;
    std::unordered_map<OccurrenceId, std::size_t> item_by_occurrence_;
    std::unordered_map<EntityId, std::vector<std::size_t>> items_by_source_;
    std::unordered_map<OccurrenceId, std::vector<std::size_t>> items_by_parent_;
    std::unordered_map<GeometryId, std::vector<std::size_t>> items_by_geometry_;
    std::unordered_map<MaterialId, std::vector<std::size_t>> items_by_material_;
    std::unordered_map<GeometryId, std::vector<std::size_t>> batches_by_geometry_;
    std::unordered_map<MaterialId, std::vector<std::size_t>> batches_by_material_;
    std::unordered_map<BatchKey, std::size_t, BatchKeyHash> batch_by_key_;
    std::vector<std::size_t> item_batch_;
    std::vector<std::size_t> item_batch_position_;
    std::unordered_map<GeometryId, std::uint64_t> geometry_revisions_;
    std::unordered_map<MaterialId, std::uint64_t> material_revisions_;
    std::uint64_t geometry_resources_revision_ = 0;
    std::uint64_t material_resources_revision_ = 0;
    std::vector<OccurrenceId> view_override_occurrences_;
    std::vector<EntityId> view_source_policy_sources_;
    std::vector<EntityId> view_isolation_sources_;
    std::vector<OccurrenceId> view_isolation_occurrences_;
    bool view_global_policy_ = false;
    bool view_source_rules_only_ = false;
    bool view_isolation_only_ = false;
    OccurrenceId view_root_;
    std::array<float, 16> view_projection_ = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                                              0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    bool camera_enabled_ = false;
    std::array<std::array<float, 4>, max_clip_planes> clip_planes_{};
    std::uint32_t clip_plane_count_ = 0;
    std::size_t visible_items_ = 0;
    std::size_t culled_items_ = 0;
    std::size_t compile_count_ = 0;
    std::uint64_t culling_signature_ = 0;
    std::shared_ptr<SceneSpatialIndex> culling_index_;
    std::unordered_set<OccurrenceId> culling_dirty_occurrences_;
    std::unordered_set<OccurrenceId> culling_unbounded_occurrences_;
    std::unordered_set<OccurrenceId> culled_occurrences_;

    friend NKSRENDER_API RenderPlan compile(const SceneSnapshot &, const SceneView &);
    friend NKSRENDER_API RenderUpdate update(RenderPlan &, const SceneSnapshot &, const ChangeSet &,
                                             const SceneView &);
    friend NKSRENDER_API RenderUpdate refresh(RenderPlan &, const SceneSnapshot &,
                                              const SceneView &);
    friend void render_internal::rebuild_batches(RenderPlan &plan);
    friend std::size_t render_internal::move_item_batch(RenderPlan &plan, std::size_t item_index,
                                                        GeometryId geometry, MaterialId material);
    friend void render_internal::capture_view_policy(RenderPlan &plan, const SceneView &view);
    friend void render_internal::build_items(RenderPlan &plan, const SceneSnapshot &snapshot,
                                             const SceneView &view);
};

NKSRENDER_API RenderPlan compile(const SceneSnapshot &snapshot, const SceneView &view);
NKSRENDER_API RenderUpdate update(RenderPlan &plan, const SceneSnapshot &snapshot,
                                  const ChangeSet &changes, const SceneView &view);
NKSRENDER_API RenderUpdate refresh(RenderPlan &plan, const SceneSnapshot &snapshot,
                                   const SceneView &view);

NKSRENDER_API PickResult pick(const RenderPlan &, const SceneSnapshot &, std::uint32_t primitive,
                              Vec3 world_position, float depth);

/**
 * Executes the opaque triangle subset of a RenderPlan through NativeKit GPU.
 * Geometry payloads use GeometryPayload's object-local float3 vertices and
 * optional uint32 triangle indices. Constructing the executor without a
 * renderer retains the headless resource/command path.
 * A renderer must outlive the executor while GPU resources are cached.
 */
class NKSRENDER_API NativeKitGpuExecutor {
  public:
    NativeKitGpuExecutor();
    explicit NativeKitGpuExecutor(nkgpu_renderer renderer);
    ~NativeKitGpuExecutor();
    NativeKitGpuExecutor(NativeKitGpuExecutor &&) noexcept;
    NativeKitGpuExecutor &operator=(NativeKitGpuExecutor &&) noexcept;
    NativeKitGpuExecutor(const NativeKitGpuExecutor &) = delete;
    NativeKitGpuExecutor &operator=(const NativeKitGpuExecutor &) = delete;

    void set_renderer(nkgpu_renderer renderer) noexcept;
    nkgpu_renderer renderer() const noexcept;
    nkgpu_result last_result() const noexcept;

    GpuExecutionStats execute(const RenderPlan &, const SceneSnapshot &);
    /** Renders an ID-only pass and resolves one pixel to scene ownership. */
    nkgpu_result pick_pixel(const RenderPlan &, const SceneSnapshot &, std::uint32_t width,
                            std::uint32_t height, std::uint32_t x, std::uint32_t y,
                            PickResult *out_result);
    nkgpu_result begin_pick_pixel(const RenderPlan &, const SceneSnapshot &, std::uint32_t width,
                                  std::uint32_t height, std::uint32_t x, std::uint32_t y,
                                  std::shared_ptr<GpuPickRequest> &out_request);
    std::uint32_t poll_pick_pixel(GpuPickRequest &, const RenderPlan &current_plan,
                                  const SceneSnapshot &current_snapshot, PickResult *out_result,
                                  nkgpu_result *out_error);
    std::span<const GpuCommand> commands() const noexcept;

  private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace nkscene

#endif /* __cplusplus */

#endif /* NATIVEKIT_SCENE_RENDER_H */
