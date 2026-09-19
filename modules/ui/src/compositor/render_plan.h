#ifndef NATIVEKIT_UI_RENDER_PLAN_H
#define NATIVEKIT_UI_RENDER_PLAN_H

#include "display_list/display_list.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace nkui {

enum class RenderCommandKind : uint8_t {
    Path = 1,
    Image,
    GlyphBatch,
    CompositeTarget,
    StrokePath,
    BoxShadow,
};

enum class RenderPassKind : uint8_t {
    Draw = 1,
    Effect,
    Mask,
    Raster,
};

enum class RenderTargetFormat : uint8_t {
    Rgba8 = 1,
};

enum RenderTargetUsage : uint32_t {
    RenderTargetColorAttachment = 1u << 0,
    RenderTargetSampled = 1u << 1,
};

/** Concrete properties used when a render-plan pass realizes a target. */
struct RenderTargetDescriptor {
    // Zero dimensions inherit the current window/frame dimensions at execution time.
    int width = 0;
    int height = 0;
    float logical_width = 0.0f;
    float logical_height = 0.0f;
    /** Logical-space origin represented by a bounded transient target. */
    float origin_x = 0.0f;
    float origin_y = 0.0f;
    RenderTargetFormat format = RenderTargetFormat::Rgba8;
    uint32_t sample_count = 1;
    uint32_t usage = RenderTargetColorAttachment | RenderTargetSampled;
    uint32_t generation = 0;
};

struct RenderCommand {
    RenderCommandKind kind{};
    ResourceId resource{};
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float opacity = 1.0f;
    std::array<float, 6> transform{1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    ResourceId paint{};
    CompositeMode composite = CompositeMode::SourceOver;
    bool has_scissor = false;
    float scissor_x = 0.0f;
    float scissor_y = 0.0f;
    float scissor_width = 0.0f;
    float scissor_height = 0.0f;
    float stroke_width = 1.0f;
    uint32_t line_cap = 0;
    uint32_t line_join = 4;
    float miter_limit = 10.0f;
    /** Set while commands originate from Haxe custom-paint display lists. */
    bool custom_payload = false;
    /** Optional source revision for layout-produced prepared resources. */
    uint64_t content_generation = 0;
    BoxShadowDescriptor box_shadow{};
};

struct RenderPass {
    ResourceId target{};
    RenderTargetDescriptor target_descriptor{};
    bool load_existing = false;
    std::vector<RenderCommand> commands;
    RenderPassKind kind = RenderPassKind::Draw;
    ResourceId input_target{};
    EffectDescriptor effect{};
    CustomEffectDescriptor custom_effect{};
    /** Optional source rectangle used by backdrop capture; frame compilers scale it to pixels. */
    bool has_input_rect = false;
    std::array<float, 4> input_rect{};
    /** True when this effect pass filters pixels already rendered below a node. */
    bool backdrop = false;
    MaskDescriptor mask{};
    /** Structural content/effect key; execution adds only reachable resource generations. */
    uint64_t cache_key = 0;
};

struct RenderDependency {
    ResourceId producer{};
    ResourceId consumer{};
};

struct RenderPlan {
    std::vector<RenderPass> passes;
    std::vector<RenderDependency> dependencies;
    uint32_t isolated_layers = 0;
    uint32_t bounded_layers = 0;
};

struct RenderPlanScheduleError {
    uint32_t pass_index = 0;
    const char *message = nullptr;
};

/** Placement and resource mapping used when a plan is embedded in another plan. */
struct RenderPlanEmbedOptions {
    ResourceId source_main_target{};
    ResourceId destination_main_target{};
    std::array<float, 6> placement{1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    float pixel_scale = 1.0f;
    const std::unordered_map<uint32_t, ResourceId> *target_remap = nullptr;
    /** Optional destination pass when several continuations share a target. */
    std::size_t destination_main_pass = static_cast<std::size_t>(-1);
    bool has_clip = false;
    std::array<float, 4> clip{};
};

struct RenderPlanEmbedError {
    const char *message = nullptr;
};

/**
 * Appends a local custom-paint plan to a destination plan.
 *
 * Custom draw commands in the source main pass are placed with `placement`.
 * Bounded intermediate targets retain their local drawing coordinates while
 * their logical origin is moved into the destination coordinate space. This
 * keeps rasterization local and makes the final composite responsible for the
 * parent transform.
 */
bool append_embedded_render_plan(const RenderPlan &source, const RenderPlanEmbedOptions &options,
                                 RenderPlan &destination, RenderPlanEmbedError *error = nullptr);

/** Scales effect, mask, and backdrop-region parameters for a device pixel ratio. */
bool scale_render_plan_parameters(RenderPass &pass, float pixel_scale);

/**
 * Computes a stable execution order for a render plan without changing its
 * semantic command order inside any pass. External producers are intentionally
 * excluded because they are acquired by the render-plan executor.
 */
bool schedule_render_plan(const RenderPlan &plan, std::vector<uint32_t> &order,
                          RenderPlanScheduleError *error = nullptr);

} // namespace nkui

#endif
