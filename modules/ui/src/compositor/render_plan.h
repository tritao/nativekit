#ifndef NATIVEKIT_UI_RENDER_PLAN_H
#define NATIVEKIT_UI_RENDER_PLAN_H

#include "display_list/display_list.h"

#include <array>
#include <cstdint>
#include <vector>

namespace nkui {

enum class RenderCommandKind : uint8_t {
    Path = 1,
    Image,
    GlyphBatch,
    CompositeTarget,
    StrokePath,
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
};

struct RenderPass {
    ResourceId target{};
    RenderTargetDescriptor target_descriptor{};
    bool load_existing = false;
    std::vector<RenderCommand> commands;
};

struct RenderDependency {
    ResourceId producer{};
    ResourceId consumer{};
};

struct RenderPlan {
    std::vector<RenderPass> passes;
    std::vector<RenderDependency> dependencies;
};

struct RenderPlanScheduleError {
    uint32_t pass_index = 0;
    const char *message = nullptr;
};

/**
 * Computes a stable execution order for a render plan without changing its
 * semantic command order inside any pass. External producers are intentionally
 * excluded because they are acquired by the render-plan executor.
 */
bool schedule_render_plan(const RenderPlan &plan, std::vector<uint32_t> &order,
                          RenderPlanScheduleError *error = nullptr);

} // namespace nkui

#endif
