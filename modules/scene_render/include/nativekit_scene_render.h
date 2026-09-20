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
typedef uint32_t nkscene_render_executor NK_HANDLE NK_HANDLE_DESTROY(nkscene_render_executor_destroy);

typedef struct nkscene_render_visibility_override {
    nkscene_occurrence_id occurrence;
    uint32_t visible NK_BOOL32;
} nkscene_render_visibility_override;

typedef struct nkscene_render_material_override {
    nkscene_occurrence_id occurrence;
    nkscene_material_id material;
} nkscene_render_material_override;

typedef struct nkscene_render_view {
    uint32_t struct_size NK_STRUCT_SIZE;
    nkscene_occurrence_id root;
    uint32_t include_invisible NK_BOOL32;
    const nkscene_render_visibility_override *visibility_overrides
        NK_BORROWED_ARRAY(visibility_override_count);
    uint32_t visibility_override_count;
    const nkscene_render_material_override *material_overrides
        NK_BORROWED_ARRAY(material_override_count);
    uint32_t material_override_count;
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
} nkscene_render_update;

typedef struct nkscene_render_pick_result {
    nkscene_occurrence_id occurrence;
    nkscene_entity_id source;
    uint32_t subelement;
    float world_position[3];
    float depth;
} nkscene_render_pick_result;

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

NKSRENDER_API nkscene_result NKS_CALL nkscene_render_plan_compile(
    nkscene_snapshot snapshot, const nkscene_render_view *view,
    nkscene_render_plan *out_plan NK_OUT NK_OWNED);
NKSRENDER_API void NKS_CALL nkscene_render_plan_destroy(nkscene_render_plan plan);
NKSRENDER_API nkscene_result NKS_CALL nkscene_render_plan_get_item_count(
    nkscene_render_plan plan, uint64_t *out_count NK_OUT);
NKSRENDER_API nkscene_result NKS_CALL nkscene_render_plan_update(
    nkscene_render_plan plan, nkscene_snapshot snapshot, nkscene_change_set changes,
    const nkscene_render_view *view, nkscene_render_update *out_update NK_INOUT);
NKSRENDER_API nkscene_result NKS_CALL nkscene_render_plan_pick(
    nkscene_render_plan plan, nkscene_snapshot snapshot, uint32_t primitive,
    const float world_position[3], float depth, nkscene_render_pick_result *out_result NK_OUT);
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

#ifdef __cplusplus
}

/* ------------------------------------------------------------------------- */
/* C++ linkage                                                               */
/* ------------------------------------------------------------------------- */

#include "nativekit_scene.hpp"

#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace nkscene {

struct SceneView;
class RenderPlan;
namespace render_internal {
void rebuild_batches(RenderPlan &plan);
void build_items(RenderPlan &plan, const SceneSnapshot &snapshot, const SceneView &view);
std::unordered_map<OccurrenceId, std::size_t> item_indices(const RenderPlan &plan);
} // namespace render_internal

struct VisibilityOverride {
    OccurrenceId occurrence;
    bool visible = true;
};

struct MaterialOverride {
    OccurrenceId occurrence;
    MaterialId material;
};

struct SceneView {
    /** Invalid means that the view contains every occurrence. */
    OccurrenceId root;
    /** Include scene-hidden occurrences as visible for inspection views. */
    bool include_invisible = false;
    /** Later entries replace earlier entries for the same occurrence. */
    std::vector<VisibilityOverride> visibility_overrides;
    std::vector<MaterialOverride> material_overrides;
};

enum class RenderFlags : std::uint32_t {
    None = 0,
    Hidden = 1u << 0,
    Opaque = 1u << 1
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
    std::uint64_t source_revision() const noexcept { return source_revision_; }
    std::span<const RenderItem> items() const noexcept { return items_; }
    std::span<const WorldTransform> transforms() const noexcept { return transforms_; }
    std::span<const InstanceBatch> batches() const noexcept { return batches_; }
    std::size_t compile_count() const noexcept { return compile_count_; }

private:
    std::uint64_t source_revision_ = 0;
    std::uint64_t view_signature_ = 0;
    std::vector<RenderItem> items_;
    std::vector<WorldTransform> transforms_;
    std::vector<InstanceBatch> batches_;
    std::unordered_map<GeometryId, std::uint64_t> geometry_revisions_;
    std::unordered_map<MaterialId, std::uint64_t> material_revisions_;
    std::size_t compile_count_ = 0;

    friend NKSRENDER_API RenderPlan compile(const SceneSnapshot &, const SceneView &);
    friend NKSRENDER_API RenderUpdate update(RenderPlan &, const SceneSnapshot &,
                                             const ChangeSet &, const SceneView &);
    friend void render_internal::rebuild_batches(RenderPlan &plan);
    friend void render_internal::build_items(RenderPlan &plan, const SceneSnapshot &snapshot,
                                             const SceneView &view);
    friend std::unordered_map<OccurrenceId, std::size_t> render_internal::item_indices(
        const RenderPlan &plan);
};

NKSRENDER_API RenderPlan compile(const SceneSnapshot &snapshot, const SceneView &view);
NKSRENDER_API RenderUpdate update(RenderPlan &plan, const SceneSnapshot &snapshot,
                                  const ChangeSet &changes, const SceneView &view);

NKSRENDER_API PickResult pick(const RenderPlan &, const SceneSnapshot &,
                              std::uint32_t primitive, Vec3 world_position, float depth);

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
    std::span<const GpuCommand> commands() const noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace nkscene

#endif /* __cplusplus */

#endif /* NATIVEKIT_SCENE_RENDER_H */
