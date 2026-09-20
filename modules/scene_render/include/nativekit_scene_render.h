#ifndef NATIVEKIT_SCENE_RENDER_H
#define NATIVEKIT_SCENE_RENDER_H

/* ------------------------------------------------------------------------- */
/* Dependencies                                                              */
/* ------------------------------------------------------------------------- */

#include "nativekit_scene.hpp"

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
/* C++ linkage                                                               */
/* ------------------------------------------------------------------------- */

#ifdef __cplusplus

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

struct SceneView {
    bool include_invisible = false;
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
    std::vector<RenderItem> items_;
    std::vector<WorldTransform> transforms_;
    std::vector<InstanceBatch> batches_;
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

} // namespace nkscene

#endif /* __cplusplus */

#endif /* NATIVEKIT_SCENE_RENDER_H */
