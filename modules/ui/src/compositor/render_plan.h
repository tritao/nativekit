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
};

struct RenderPass {
    ResourceId target{};
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

} // namespace nkui

#endif
