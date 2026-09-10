#include "compositor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

namespace nkui {
namespace {

struct Layer {
    ResourceId parent_target{};
    ResourceId layer_target{};
    float opacity = 1.0f;
    bool isolated = false;
};

struct CanvasState {
    std::array<float, 6> transform{1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    float alpha = 1.0f;
    ResourceId paint{};
    CompositeMode composite = CompositeMode::SourceOver;
    bool has_scissor = false;
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

bool fail(CompositorError *error, uint32_t index, const char *message) {
    if (error)
        *error = {index, message};
    return false;
}

template <class T> T read(const uint8_t *record) {
    T value{};
    std::memcpy(&value, record, sizeof(value));
    return value;
}

RenderPass &continue_pass(RenderPlan &plan, ResourceId target) {
    const bool seen = [&] {
        for (const auto &pass : plan.passes)
            if (pass.target.value == target.value)
                return true;
        return false;
    }();
    plan.passes.push_back({target, seen, {}});
    return plan.passes.back();
}

void add_dependency(RenderPlan &plan, ResourceId producer, ResourceId consumer) {
    const auto found = std::find_if(plan.dependencies.begin(), plan.dependencies.end(),
                                    [producer, consumer](const RenderDependency &dependency) {
                                        return dependency.producer.value == producer.value &&
                                               dependency.consumer.value == consumer.value;
                                    });
    if (found == plan.dependencies.end())
        plan.dependencies.push_back({producer, consumer});
}

void apply_state(RenderCommand &command, const CanvasState &state) {
    command.opacity *= state.alpha;
    command.transform = state.transform;
    command.paint = state.paint;
    command.composite = state.composite;
    command.has_scissor = state.has_scissor;
    command.scissor_x = state.x;
    command.scissor_y = state.y;
    command.scissor_width = state.width;
    command.scissor_height = state.height;
}

bool intersect_clip(CanvasState &state, const ClipRectCommand &clip) {
    const auto &m = state.transform;
    const float corners[4][2] = {{clip.x, clip.y},
                                 {clip.x + clip.width, clip.y},
                                 {clip.x, clip.y + clip.height},
                                 {clip.x + clip.width, clip.y + clip.height}};
    float min_x = INFINITY, min_y = INFINITY, max_x = -INFINITY, max_y = -INFINITY;
    for (const auto &corner : corners) {
        const float x = corner[0] * m[0] + corner[1] * m[2] + m[4];
        const float y = corner[0] * m[1] + corner[1] * m[3] + m[5];
        min_x = std::min(min_x, x);
        min_y = std::min(min_y, y);
        max_x = std::max(max_x, x);
        max_y = std::max(max_y, y);
    }
    if (!std::isfinite(min_x) || !std::isfinite(min_y) || !std::isfinite(max_x) ||
        !std::isfinite(max_y))
        return false;
    if (state.has_scissor) {
        min_x = std::max(min_x, state.x);
        min_y = std::max(min_y, state.y);
        max_x = std::min(max_x, state.x + state.width);
        max_y = std::min(max_y, state.y + state.height);
    }
    state.has_scissor = true;
    state.x = min_x;
    state.y = min_y;
    state.width = std::max(0.0f, max_x - min_x);
    state.height = std::max(0.0f, max_y - min_y);
    return true;
}

} // namespace

ResourceId Compositor::allocate_transient_target() {
    if (!next_target_slot_)
        next_target_slot_ = 0x8000;
    return make_resource_id(ResourceKind::RenderTarget, 1, next_target_slot_++);
}

bool Compositor::compile(const DisplayList &display_list, ResourceId main_target, RenderPlan &plan,
                         CompositorError *error) {
    ValidationError validation{};
    if (!is_resource_id(main_target, ResourceKind::RenderTarget) ||
        !validate_display_list(display_list.data(), display_list.size(), &validation))
        return fail(error, validation.command_index, "invalid compositor input");
    plan = {};
    // Transient IDs are frame-local structural slots. Reusing them across compilations lets the
    // backend pool matching GPU targets instead of growing one target per transaction.
    next_target_slot_ = 0x8000;
    std::vector<Layer> layers;
    CanvasState state;
    std::vector<CanvasState> states;
    ResourceId current_target = main_target;
    RenderPass *pass = &continue_pass(plan, current_target);
    size_t offset = 0;
    uint32_t index = 0;
    while (offset < display_list.size()) {
        const auto header = read<CommandHeader>(display_list.data() + offset);
        const uint8_t *record = display_list.data() + offset;
        switch (header.opcode) {
        case CommandOpcode::SetTransform: {
            const auto value = read<SetTransformCommand>(record);
            std::copy(std::begin(value.matrix), std::end(value.matrix), state.transform.begin());
            break;
        }
        case CommandOpcode::SetGlobalAlpha: {
            state.alpha = read<SetGlobalAlphaCommand>(record).alpha;
            break;
        }
        case CommandOpcode::SetPaint:
            state.paint = read<SetPaintCommand>(record).paint;
            break;
        case CommandOpcode::SetCompositeMode:
            state.composite = read<SetCompositeModeCommand>(record).mode;
            break;
        case CommandOpcode::PushState:
            states.push_back(state);
            break;
        case CommandOpcode::PopState:
            state = states.back();
            states.pop_back();
            break;
        case CommandOpcode::ClipRect:
            if (!intersect_clip(state, read<ClipRectCommand>(record)))
                return fail(error, index, "clip transform overflow");
            break;
        case CommandOpcode::DrawPath: {
            const auto value = read<DrawResourceCommand>(record);
            pass->commands.push_back({RenderCommandKind::Path, value.resource});
            apply_state(pass->commands.back(), state);
            break;
        }
        case CommandOpcode::StrokePath: {
            const auto value = read<StrokePathCommand>(record);
            pass->commands.push_back({RenderCommandKind::StrokePath, value.path});
            auto &command = pass->commands.back();
            command.stroke_width = value.width;
            command.line_cap = value.line_cap;
            command.line_join = value.line_join;
            command.miter_limit = value.miter_limit;
            apply_state(command, state);
            break;
        }
        case CommandOpcode::DrawImage: {
            const auto value = read<DrawRectResourceCommand>(record);
            pass->commands.push_back({RenderCommandKind::Image, value.resource, value.x, value.y,
                                      value.width, value.height});
            apply_state(pass->commands.back(), state);
            break;
        }
        case CommandOpcode::DrawTextLayout: {
            const auto value = read<DrawRectResourceCommand>(record);
            pass->commands.push_back({RenderCommandKind::GlyphBatch, value.resource, value.x,
                                      value.y, value.width, value.height});
            apply_state(pass->commands.back(), state);
            break;
        }
        case CommandOpcode::DrawRenderTarget: {
            const auto value = read<DrawRectResourceCommand>(record);
            if (value.resource.value == current_target.value)
                return fail(error, index, "render target cannot sample itself");
            pass->commands.push_back({RenderCommandKind::CompositeTarget, value.resource, value.x,
                                      value.y, value.width, value.height});
            apply_state(pass->commands.back(), state);
            add_dependency(plan, value.resource, current_target);
            break;
        }
        case CommandOpcode::BeginLayer: {
            const auto value = read<BeginLayerCommand>(record);
            const bool isolated = value.opacity < 1.0f;
            const ResourceId parent_target = current_target;
            ResourceId layer_target = current_target;
            if (isolated) {
                layer_target = allocate_transient_target();
                current_target = layer_target;
                pass = &continue_pass(plan, current_target);
            }
            layers.push_back({parent_target, layer_target, value.opacity, isolated});
            break;
        }
        case CommandOpcode::EndLayer: {
            const Layer layer = layers.back();
            layers.pop_back();
            if (layer.isolated) {
                current_target = layer.parent_target;
                pass = &continue_pass(plan, current_target);
                pass->commands.push_back({RenderCommandKind::CompositeTarget, layer.layer_target,
                                          0.0f, 0.0f, 0.0f, 0.0f, layer.opacity});
                pass->commands.back().has_scissor = state.has_scissor;
                pass->commands.back().scissor_x = state.x;
                pass->commands.back().scissor_y = state.y;
                pass->commands.back().scissor_width = state.width;
                pass->commands.back().scissor_height = state.height;
                add_dependency(plan, layer.layer_target, current_target);
            }
            break;
        }
        default:
            break;
        }
        offset += header.size;
        ++index;
    }
    if (error)
        *error = {};
    return true;
}

} // namespace nkui
