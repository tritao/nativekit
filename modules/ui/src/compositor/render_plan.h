#ifndef NATIVEKIT_UI_RENDER_PLAN_H
#define NATIVEKIT_UI_RENDER_PLAN_H

#include "display_list/display_list.h"

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
